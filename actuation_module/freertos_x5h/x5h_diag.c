// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 18: the C half of this image's diagnostic surface.
//
// READ x5h_diag.h FIRST. It is the canonical rationale for all of this --
// the three surviving wedge candidates, why each is silent by
// construction, the rule that nothing here may allocate or take the
// scheduler lock, the two gates the output path passes through, and what
// silence means once this image is flashed. None of that is repeated here;
// four copies of one argument is four chances for it to drift, which is
// the failure mode this branch's review rounds kept turning up.
//
// What IS below is implementation detail that belongs with the code: the
// non-allocating console primitives, the per-field reasoning in
// diag_put_resources(), the exception report's register handling, and the
// beacon's priority and cost.

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* R_SERIAL_PutChar(): the polled, non-allocating console write this whole
   file is built on. */
#include "serial/r_serial.h"

/* RPMSG_POLL_TASK_PRIORITY, and TCPIP_THREAD_PRIO through the lwip/opt.h
   that header includes. The beacon's priority is asserted against the poll
   task by name below, never against a remembered number -- same reason
   rpmsg_transport.h gives for holding that macro in one place. */
#include "rpmsg_transport.h"

#include "x5h_diag.h"

// ============================================================================
// Non-allocating console primitives
// ============================================================================

static void diag_putc(char c)
{
    (void)R_SERIAL_PutChar((unsigned char)c);
}

// Translates '\n' to "\r\n" the way the BSP's own outbyte() does, so lines
// land correctly on a raw serial capture. R_SERIAL_PutChar() itself does
// not do this translation (only R_SERIAL_PutString()/_write() do).
static void diag_puts(const char *s)
{
    if (s == NULL) {
        s = "<null>";
    }
    while (*s != '\0') {
        if (*s == '\n') {
            diag_putc('\r');
        }
        diag_putc(*s);
        s++;
    }
}

static void diag_put_hex32(uint32_t v)
{
    static const char hexdigits[] = "0123456789abcdef";
    int shift;

    diag_puts("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        diag_putc(hexdigits[(v >> shift) & 0xFU]);
    }
}

static void diag_put_u32(uint32_t v)
{
    char buf[10];
    int n = 0;

    if (v == 0U) {
        diag_putc('0');
        return;
    }
    while (v != 0U) {
        buf[n] = (char)('0' + (int)(v % 10U));
        n++;
        v /= 10U;
    }
    while (n > 0) {
        n--;
        diag_putc(buf[n]);
    }
}

static void diag_put_i32(int32_t v)
{
    if (v < 0) {
        diag_putc('-');
        // Negated in unsigned space so INT32_MIN does not overflow.
        diag_put_u32((uint32_t)0U - (uint32_t)v);
    } else {
        diag_put_u32((uint32_t)v);
    }
}

// ============================================================================
// Shared state: the task whose stack the beacon and R4's mark report on
// ============================================================================

// Set by x5h_diag_start_beacon() and cleared by x5h_diag_clear_launcher().
// Read by the beacon task and by actuation_diag_mark(). No lock: this is a
// single aligned 32-bit pointer, written twice in the whole life of the
// image (once before the scheduler starts, once by the launcher itself just
// before it deletes itself) and only ever read. `volatile` so the beacon's
// loop cannot cache it across iterations.
static TaskHandle_t volatile s_launcher = NULL;

// ============================================================================
// Heap headroom (R3/R4)
// ============================================================================
//
// The linker symbols heap_useNewlib.c itself uses (common/linker/
// lscript_vram2.ld's .heap output section, whose length is _HEAP_SIZE =
// 0x500000). Declared exactly the way that file declares them -- as `char`
// objects whose ADDRESS is the value -- so the bounds reported below are
// the allocator's own, not a second pair derived independently and free to
// disagree with it.
extern char HeapBase, HeapLimit;

// Declared here rather than pulled in from <unistd.h>: this directory
// compiles C at -std=c99 (rcar_bsp/.../CMakeLists.txt's CMAKE_C_FLAGS),
// which defines __STRICT_ANSI__ and so hides sbrk() behind newlib's
// __MISC_VISIBLE feature gate -- confirmed by an -Wimplicit-function-
// declaration warning on the first build of this file. The prototype is
// copied verbatim from the definition this call actually resolves to,
// heap_useNewlib.c's own `char * sbrk(int incr)`, not from newlib's
// (differently-typed) declaration.
extern char *sbrk(int incr);

// The current break, obtained without allocating. sbrk(0) reaches
// heap_useNewlib.c's _sbrk_r(reent, 0), which with incr == 0 cannot fail
// (its only failure test is `currentHeapEnd + incr > &HeapLimit`), adds
// nothing to the break, and returns it unchanged. It does take and release
// vTaskSuspendAll(), which is why this is only ever called from task
// context -- never from the exception handlers below.
static uint32_t diag_heap_break(void)
{
    return (uint32_t)(uintptr_t)sbrk(0);
}

// Emits the launcher stack watermark and heap headroom shared by R3's
// beacon and R4's pre-DDS mark, so the two are always directly comparable.
//
// Two heap numbers, both derived from one sbrk(0) against the allocator's
// own linker bounds. heap_used + sbrk_free is _HEAP_SIZE by construction,
// so the pair self-checks against lscript_vram2.ld:
//   heap_used  the break minus HeapBase -- everything sbrk() has ever
//              handed to newlib, whether newlib still has it out on loan or
//              has it back on its own free list.
//   sbrk_free  HeapLimit minus the break -- heap that has never been handed
//              out at all. This is exactly the figure R3 asks for: the
//              linker's own bounds against the current break.
//
// REMOVED, and deliberately not to be reinstated: a third field carrying
// xPortGetFreeHeapSize(). It is a strictly better number in the abstract
// -- fordblks plus the untouched remainder is what a failing malloc()
// actually competes for -- and it is unusable here, because obtaining it
// means mallinfo() walking newlib's arena chunk by chunk under
// __malloc_lock, i.e. under vTaskSuspendAll().
//
// The reason that is disqualifying rather than merely unfortunate: the
// walk is unsafe in precisely the scenario the beacon exists to report.
// Candidate 3 (an actuation_task stack overflow) corrupts malloc chunk
// headers almost by definition -- that stack is a pvPortMalloc block, it
// grows down toward pxStack[0], and newlib's header sits immediately below
// the returned pointer. A corrupted size field sends the walk into a loop
// with the scheduler suspended. The operator would then see beacons stop
// dead with no exception and no abort line, which the run-book reads as a
// never-resumed vTaskSuspendAll() -- candidate 2. A diagnostic that turns
// candidate 3 into a confident report of candidate 2 is worse than one
// that stays quiet about a number it cannot safely obtain.
//
// More simply: a beacon whose whole job is to prove the scheduler is alive
// must not take the global scheduler lock to say so. If allocation failure
// does turn out to be the wedge, R2's abort()/_exit() lines announce it
// directly, and sbrk_free still bounds how much heap was ever available.
static void diag_put_resources(void)
{
    TaskHandle_t launcher = s_launcher;
    uint32_t brk = diag_heap_break();

    diag_puts(" launcher=");
    if (launcher != NULL) {
        diag_puts(pcTaskGetName(launcher));
        diag_puts(" stack_hwm_words=");
        diag_put_u32((uint32_t)uxTaskGetStackHighWaterMark(launcher));
    } else {
        diag_puts("<none> stack_hwm_words=-");
    }

    diag_puts(" heap_brk=");
    diag_put_hex32(brk);
    diag_puts(" heap_used=");
    diag_put_u32((uint32_t)((uintptr_t)brk - (uintptr_t)&HeapBase));
    diag_puts(" sbrk_free=");
    diag_put_u32((uint32_t)((uintptr_t)&HeapLimit - (uintptr_t)brk));
}

static uint32_t diag_uptime_ms(void)
{
    return (uint32_t)(((uint64_t)xTaskGetTickCount() * 1000ULL)
                      / (uint64_t)configTICK_RATE_HZ);
}

// ============================================================================
// R1 -- exception vectors that print
// ============================================================================

// Defined in x5h_diag_vectors.S. Declared as an array so the symbol's
// address is the table address without a further indirection.
extern uint32_t x5h_diag_vector_table[];

static const char *const kExcNames[] = {
    "Undefined Instruction",
    "Prefetch Abort",
    "Data Abort",
    "Reserved",
    "FIQ",
};
_Static_assert(sizeof(kExcNames) / sizeof(kExcNames[0]) == X5H_DIAG_EXC_COUNT,
               "kExcNames must have exactly one entry per X5H_DIAG_EXC_* code "
               "in x5h_diag.h -- the assembly stubs in x5h_diag_vectors.S "
               "index this table by those codes, so a mismatch is an "
               "out-of-bounds read taken while already handling a fault.");

// How the faulting address in each report was derived from the exception
// mode's LR, spelled out in the output rather than left for the reader to
// remember. Indexed by the same X5H_DIAG_EXC_* codes.
static const char *const kExcPcNotes[] = {
    "LR-4",
    "LR-4",
    "LR-8",
    "LR raw, architecture defines no offset for this vector",
    "LR-4",
};
_Static_assert(sizeof(kExcPcNotes) / sizeof(kExcPcNotes[0]) == X5H_DIAG_EXC_COUNT,
               "kExcPcNotes must have exactly one entry per X5H_DIAG_EXC_* "
               "code in x5h_diag.h, for the same reason as kExcNames.");

// Called from the assembly entry stubs with r0/r1/r2 as documented in
// x5h_diag_vectors.S. Never returns.
//
// One case this deliberately does not try to survive: a second fault taken
// while this function is running. Entry re-banks LR and SPSR of the mode
// being entered and the mode's stack pointer is not saved anywhere, so a
// Data Abort inside the Data Abort handler overwrites the state being
// reported and restarts here with the second fault's values. The report is
// therefore a best-effort first shot, which is the right trade for the
// wedge this exists to catch -- a first fault reported once beats a
// nesting-safe handler that is harder to trust.
//
// The fault-status and fault-address registers are read here rather than
// passed in from assembly, and they are still the faulting access's own
// values when we get to them.
//
// The reason is NOT that nothing has touched memory in between -- plenty
// has: the vector's own `ldr pc, [pc, #24]` literal load, the stub's `ldr`,
// and this function's prologue pushes are all memory accesses. It is that
// DFSR/DFAR and IFSR/IFAR are only written when an access FAULTS. A
// successful load or store leaves them entirely alone, so any number of
// them can run between the exception and this point without disturbing the
// record. Only a second fault would overwrite it -- which is the nested
// case called out below. Reading them from C rather than the stub keeps
// each entry stub to four instructions.
//
// All four are printed on every exception, not just the pair that is
// architecturally meaningful for the exception at hand (DFSR/DFAR for a
// Data Abort, IFSR/IFAR for a Prefetch Abort). Read them accordingly: on a
// Data Abort the IFSR/IFAR values are whatever the last prefetch fault left
// there, and vice versa, and on an Undefined Instruction or FIQ neither
// pair says anything about this exception. They are printed anyway because
// a stale value costs one line and is occasionally the thing that explains
// the sequence, whereas a value that was never printed cannot be recovered
// from a capture afterwards.
void x5h_diag_exception_report(uint32_t kind, uint32_t pc, uint32_t spsr)
    __attribute__((noreturn));

void x5h_diag_exception_report(uint32_t kind, uint32_t pc, uint32_t spsr)
{
    uint32_t dfsr = 0;
    uint32_t dfar = 0;
    uint32_t ifsr = 0;
    uint32_t ifar = 0;

    __asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r" (dfsr));
    __asm__ volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r" (ifsr));
    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r" (dfar));
    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r" (ifar));

    diag_puts("\n*** X5H EXCEPTION: ");
    if (kind < (uint32_t)X5H_DIAG_EXC_COUNT) {
        diag_puts(kExcNames[kind]);
    } else {
        diag_puts("unknown kind=");
        diag_put_u32(kind);
    }
    diag_puts(" ***\n");

    diag_puts("  PC=");
    diag_put_hex32(pc);
    if (kind < (uint32_t)X5H_DIAG_EXC_COUNT) {
        diag_puts(" (");
        diag_puts(kExcPcNotes[kind]);
        diag_puts(")");
    }
    diag_puts(" SPSR=");
    diag_put_hex32(spsr);
    diag_puts("\n");

    diag_puts("  DFSR=");
    diag_put_hex32(dfsr);
    diag_puts(" DFAR=");
    diag_put_hex32(dfar);
    diag_puts(" IFSR=");
    diag_put_hex32(ifsr);
    diag_puts(" IFAR=");
    diag_put_hex32(ifar);
    diag_puts("\n");

    // pcTaskGetName(NULL) resolves to pxCurrentTCB, which configASSERT()s
    // non-NULL -- and pxCurrentTCB is NULL until vTaskStartScheduler() has
    // run. Guarding on xTaskGetSchedulerState() (INCLUDE_xTaskGetSchedulerState
    // is 1 in rcar_bsp's FreeRTOSConfig.h) keeps this safe for a fault taken
    // during early startup, which is exactly when a fault is most likely and
    // least expected. taskSCHEDULER_SUSPENDED still means pxCurrentTCB is
    // valid, so only the NOT_STARTED case is excluded. Neither call
    // allocates.
    diag_puts("  task=");
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        diag_puts("<scheduler not started>");
    } else {
        diag_puts(pcTaskGetName(NULL));
    }
    diag_puts("\n*** halted ***\n");

    for (;;) {
    }
}

void x5h_diag_install_vectors(void)
{
    uint32_t old_vbar = 0;
    uint32_t base = (uint32_t)(uintptr_t)x5h_diag_vector_table;

    __asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r" (old_vbar));

    // VBAR[4:0] are RES0 on ARMv8-R AArch32: a table that is not 32-byte
    // aligned cannot be addressed and programming one is UNPREDICTABLE.
    // x5h_diag_vectors.S's .balign 32 makes this unreachable; it is checked
    // anyway because the failure mode of getting it wrong is the whole
    // image, and because the correct response is to keep the vendor table
    // (silent fault vectors, i.e. today's behaviour) rather than to gamble.
    if ((base & 0x1FU) != 0U) {
        diag_puts("x5h-diag: ERROR vector table at ");
        diag_put_hex32(base);
        diag_puts(" is not 32-byte aligned; keeping the vendor vectors\n");
        return;
    }

    // No interrupt masking around the write, and none is needed -- for an
    // architectural reason rather than an assumption about the CPSR state
    // this happens to be called with. The swap is a single MCR: there is no
    // intermediate value of VBAR and no half-installed table. An exception
    // taken before it vectors through the vendor table, one taken after
    // vectors through this one, and both tables route SVC and IRQ to the
    // same two handlers (see x5h_diag_vectors.S), so either outcome is
    // correct. The DSB/ISB pair only orders the change against subsequent
    // instruction fetch on this PE.
    __asm__ volatile ("mcr p15, 0, %0, c12, c0, 0" : : "r" (base) : "memory");
    __asm__ volatile ("dsb sy" ::: "memory");
    __asm__ volatile ("isb sy" ::: "memory");

    diag_puts("x5h-diag: exception vectors installed, VBAR ");
    diag_put_hex32(old_vbar);
    diag_puts(" -> ");
    diag_put_hex32(base);
    diag_puts("\n");
}

// ============================================================================
// R2 -- abort() and _exit() announce themselves
// ============================================================================
//
// Both of these are supplied by the toolchain today: _exit by -lnosys
// (rcar_bsp/.../CMakeLists.txt's add_link_options()), where it is a bare
// while(1) with no output, and abort() by libc, which ends in that same
// _exit. That is the silent tail of wedge candidate 2:
// ddsrt_malloc() (cyclonedds/src/ddsrt/src/heap/freertos/heap.c) calls
// abort() when pvPortMalloc() returns NULL, and dds.hpp's own error paths
// call exit(). Neither prints anything as shipped.
//
// These definitions live in an ordinary object file linked directly into
// the executable, not in an archive, so they pre-empt both the libnosys and
// the libc members without any --wrap or --defsym: the linker resolves the
// reference here and never pulls the archive member in.

void abort(void)
{
    diag_puts("\n*** X5H: abort() called -- halting ***\n");
    for (;;) {
    }
}

void _exit(int status)
{
    diag_puts("\n*** X5H: _exit(status=");
    diag_put_i32((int32_t)status);
    diag_puts(") -- halting ***\n");
    for (;;) {
    }
}

// ============================================================================
// R3 -- the permanent liveness beacon
// ============================================================================
//
// The ambiguity this removes: the only other periodic printer in this image
// is rpmsg_vdev_heartbeat_task, and rpmsg_transport_init() deletes it the
// moment platform_create_rpmsg_vdev() returns (rpmsg_transport.c). From
// that point on the console is quiet whenever nothing is being logged, so
// "silent" and "wedged" look identical. This task never stops and is never
// deleted.
//
// Priority: configMAX_PRIORITIES - 1, above every task this firmware
// creates, and the task blocks in vTaskDelay() -- a real tick-driven wait
// serviced by the tick interrupt -- rather than spinning. That pairing is
// the same one rpmsg_transport.c's RPMSG_VDEV_HEARTBEAT_PRIORITY comment
// (lines 416-495) works through, and it matters for the same reason: the
// only "yield" available inside the code being observed is libmetal's
// metal_cpu_yield(), which on this build (libmetal's generic processor
// backend) expands to nothing at all. A busy spin never hands the CPU over
// voluntarily, so a beacon that is not strictly above it, or that waits by
// spinning rather than blocking, is invisible for the whole spin.
//
// This value is deliberately the same as RPMSG_VDEV_HEARTBEAT_PRIORITY, and
// they are the only two tasks in the image at this level. Stated rather
// than glossed, because an equal-priority pair under
// configUSE_TIME_SLICING == 0 is normally a hazard: here it is not, because
// neither task ever holds the CPU -- each does a bounded amount of console
// output and then blocks in vTaskDelay() -- and because they only coexist
// during the vdev DRIVER_OK wait, after which the heartbeat is deleted. The
// assertion below covers the half that is checkable from this file;
// RPMSG_VDEV_HEARTBEAT_PRIORITY is file-static to rpmsg_transport.c and is
// not visible here to assert against.
#define X5H_DIAG_BEACON_PRIORITY (configMAX_PRIORITIES - 1)

_Static_assert(X5H_DIAG_BEACON_PRIORITY > RPMSG_POLL_TASK_PRIORITY,
               "X5H_DIAG_BEACON_PRIORITY must stay strictly above "
               "RPMSG_POLL_TASK_PRIORITY (rpmsg_transport.h), which is "
               "itself above the lwIP tcpip thread and every task the "
               "actuation launcher creates. The beacon exists to keep "
               "printing while something below it is starving the rest of "
               "the image; at or below the poll task it would go quiet for "
               "the same reasons everything else does, and its silence "
               "would then carry no information at all.");
_Static_assert(X5H_DIAG_BEACON_PRIORITY < configMAX_PRIORITIES,
               "X5H_DIAG_BEACON_PRIORITY must be a legal FreeRTOS priority "
               "(strictly below configMAX_PRIORITIES). xTaskCreate() clamps "
               "an out-of-range priority silently rather than failing, so "
               "without this the beacon could end up somewhere other than "
               "the top of the image with nothing to say so.");

#define X5H_DIAG_BEACON_PERIOD_TICKS pdMS_TO_TICKS(5000)

// configMINIMAL_STACK_SIZE (256 words) matches what
// rpmsg_vdev_heartbeat_task already uses for the same shape of work. This
// task's call depth is strictly smaller than that one's: no printf, no
// varargs, no stdio -- just the byte-at-a-time console writers above, plus
// uxTaskGetStackHighWaterMark() and sbrk(0).
#define X5H_DIAG_BEACON_STACK_WORDS configMINIMAL_STACK_SIZE

// The beacon is not a free observer, and a timing-sensitive wedge deserves
// to have the cost written down rather than discovered.
//
// uxTaskGetStackHighWaterMark() is a byte-wise scan, not a stored counter.
// With portSTACK_GROWTH == -1 (portmacro.h:81) it starts at pxTCB->pxStack
// and prvTaskCheckFreeStackSpace() (tasks.c:6305-6313) walks upward one
// byte at a time for as long as the byte still holds tskSTACK_FILL_BYTE
// (0xA5, memset over the whole stack at creation because
// tskSET_NEW_STACKS_TO_KNOWN_VALUE is 1 here). Its cost is therefore
// proportional to the UNUSED part of the stack it is asked about. On
// actuation_task that is a 32768-word (131072-byte) allocation, so early
// beacons -- when the launcher has barely touched its stack -- scan close
// to the whole 128 KiB. That runs at configMAX_PRIORITIES - 1, above
// everything including rpmsg_poll_task, once every 5 s, and preempts all of
// it for the duration.
//
// Accepted, not overlooked. It is the price of R3's central requirement:
// the watermark has to come from a task that outranks whatever is starving
// the image, or it cannot be sampled at the moment it matters. Two things
// bound the damage -- the scan shrinks as the launcher's stack fills, so it
// is cheapest exactly when the reading is most interesting, and 5 s between
// samples makes the duty cycle small against the ~1.7 Hz control cycle.
// But it is real: if a board session ever sees timing-dependent behaviour
// that changes when the beacon is present, this is the mechanism to suspect
// first, and lengthening X5H_DIAG_BEACON_PERIOD_TICKS is the cheapest test.
static void x5h_diag_beacon_task(void *pv)
{
    uint32_t n = 0;

    (void)pv;
    for (;;) {
        n++;
        diag_puts("x5h-diag: beacon #");
        diag_put_u32(n);
        diag_puts(" uptime_ms=");
        diag_put_u32(diag_uptime_ms());
        diag_put_resources();
        diag_puts("\n");
        vTaskDelay(X5H_DIAG_BEACON_PERIOD_TICKS);
    }
}

BaseType_t x5h_diag_start_beacon(TaskHandle_t launcher)
{
    s_launcher = launcher;
    return xTaskCreate(x5h_diag_beacon_task, "x5h_beacon",
                       X5H_DIAG_BEACON_STACK_WORDS, NULL,
                       X5H_DIAG_BEACON_PRIORITY, NULL);
}

void x5h_diag_clear_launcher(void)
{
    s_launcher = NULL;
}

// ============================================================================
// R4 -- bracket the DDS call
// ============================================================================
//
// Called from include/common/dds/dds.hpp immediately before
// dds_create_domain_with_rawconfig(), the last line the board ever prints
// from. It reports exactly the same pair the beacon does, so the reading
// taken at the edge of the cliff can be compared directly against the
// beacon's last line before the fall -- which is what makes wedge candidate
// 3 (stack overflow inside the ddsi_init()/ddsi_start() chain) visible as a
// trend rather than only after it has already killed the core.
void actuation_diag_mark(const char *tag)
{
    diag_puts("x5h-diag: mark ");
    diag_puts(tag);
    diag_puts(" uptime_ms=");
    diag_put_u32(diag_uptime_ms());
    diag_put_resources();
    diag_puts("\n");
}
