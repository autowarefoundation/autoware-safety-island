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

#if defined(X5H_DIAG_TASK_TABLE) && (X5H_DIAG_TASK_TABLE)
/* Task 33's one-shot GIC evidence line ("R3f" below) needs GICR_Type (via
   core_cr52.h), R_GIC_GetRedistID()/R_GIC_GetPriorityMask(), and the tick's
   PPI number. Gated so the default build's include graph is unchanged. */
#include "cmsis_rcar_gen5.h"
#include "drivers/gic/gic.h"
#include "interrupts.h"
#endif

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

// The exported face of the four writers above, declared in x5h_diag.h. See
// that declaration for why Task 19's x5h_cdds_process.c shares these rather
// than rolling its own. Wrappers rather than a rename so that this file's own
// ~40 call sites stay untouched.
void x5h_diag_puts(const char *s) { diag_puts(s); }
void x5h_diag_put_u32(uint32_t v) { diag_put_u32(v); }
void x5h_diag_put_i32(int32_t v) { diag_put_i32(v); }
void x5h_diag_put_hex32(uint32_t v) { diag_put_hex32(v); }

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

// 5 s -- the same numeric value the default build's plain liveness beacon
// already runs at, though the default build never compiles this scalar in
// at all (the whole R3c block below is gated out of it). Task 26 had
// tightened this to 1 s under X5H_DIAG_TASK_TABLE (diagnostic builds only,
// off by default), reasoning that a 5 s beacon cannot reliably bracket the
// ~6.5 s death window the controller-liveness probe (see "R3c" below) is
// chasing. Task 28 reverses that on board evidence: the bug did NOT
// reproduce under the 1 s cadence (~4-5.5 % duty, stage8) but DID reproduce
// at the ~1 % duty of the original stage7 diagnostic image. Reproduction
// now outranks bracketing precision -- a probe whose own overhead perturbs
// the timing enough to mask the bug is worse than one with coarser
// bracketing that can still see it. The `ntasks` scalar
// diag_put_controller_probe() prints every beacon (see "R3c" below, diag
// build only) remains the arbiter, now sampled once per 5 s beacon instead
// of once per 1 s beacon. With the revert, both builds share this one
// unconditional value (the pre-revert #if/#else had become two identical
// branches, collapsed since); if a diagnostic build ever needs a different
// cadence again, re-split it on the same X5H_DIAG_TASK_TABLE macro the
// rest of the feature is gated on, so the default image's object code
// stays untouched.
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
// everything including rpmsg_poll_task, once every beacon period, and
// preempts all of it for the duration.
//
// Accepted, not overlooked. It is the price of R3's central requirement:
// the watermark has to come from a task that outranks whatever is starving
// the image, or it cannot be sampled at the moment it matters. Two things
// bound the damage -- the scan shrinks as the launcher's stack fills, so it
// is cheapest exactly when the reading is most interesting, and at the 5 s
// period (Task 28: now the same in the diagnostic build as in the default
// one, see X5H_DIAG_BEACON_PERIOD_TICKS above) the duty cycle is small
// against the ~6.7 Hz (0.15 s ctrl_period, controller_node.cpp) control
// cycle.
// But it is real: if a board session ever sees timing-dependent behaviour
// that changes when the beacon is present, this is the mechanism to suspect
// first, and lengthening X5H_DIAG_BEACON_PERIOD_TICKS is the cheapest test.
// ---- R3b: the optional task table (Task 23) ----
//
// OFF unless X5H_DIAG_TASK_TABLE is defined non-zero at build time, so the
// shipping image is unchanged. It exists for one specific question that the
// beacon above cannot answer and that a CycloneDDS discovery trace cannot
// answer either: WHICH of the CycloneDDS worker threads stopped making
// progress, and whether it stopped because it is BLOCKED or because
// something else is holding the CPU.
//
// Why that question is the one worth a console line. The leading explanation
// for the once-per-boot DDS defect is that one CycloneDDS worker stops
// draining its queue, after which discovery dies SILENTLY and PERMANENTLY:
// ddsi_receive.c:2542 passes ddsi_dqueue_is_full(gv->builtins_dqueue) into
// ddsi_reorder_rsample(), whose delivery-queue-full branch
// (ddsi_radmin.c:1942-1947) returns DDSI_REORDER_REJECT -- and SPDP is
// best-effort, so a rejected participant announcement is simply gone: no
// NACK, no retransmit, no second chance. Everything else on the board keeps
// working, because the lwIP tcpip thread (4), rpmsg_poll_task (5) and this
// beacon (31) all sit above the CycloneDDS workers: ICMP still answers,
// rx_ok/tx_ok still climb, the beacon still prints, and DDS is dead. That
// signature is indistinguishable from several other faults using the
// counters the beacon already prints; it is immediately distinguishable in a
// task table.
//
// Three fields carry that:
//   state  FreeRTOS's own eTaskState for the task -- R(unning), r(eady),
//          B(locked), S(uspended), D(eleted). The expected wedge signature is
//          a worker reading 'B' forever with a frozen rt (below) -- blocked
//          on a queue nobody is draining -- NOT a worker spinning.
//   rt     the per-task run-time counter. configGENERATE_RUN_TIME_STATS is 1
//          in this BSP and its counter is real, not a stub -- common/ARM_CR52/
//          port.c:180-182 returns R_UTILS_GetTimerCounter() minus the value
//          latched at scheduler start. Read the DELTA between two dumps, not
//          the absolute value. Two different readings come out of it: a
//          worker whose delta is zero across an interval in which DDS traffic
//          was offered has stopped; and, separately, a single task
//          accumulating nearly the whole interval while its siblings
//          accumulate nothing would be CPU-bound starvation (see the priority
//          note below), which is a different fault with a different fix.
//   hwm    stack high-water mark in words, per task. The 16 KiB default DDS
//          worker stack (threads.c:372) is the other standing suspicion on
//          this port, and this is where a worker running out of it becomes
//          visible before configCHECK_FOR_STACK_OVERFLOW == 2 fires.
//
// A platform hazard this table also happens to test, stated carefully
// because an earlier draft of this comment stated it WRONGLY. Every
// CycloneDDS ddsrt worker -- recv, dq.builtins, dq.user, tev, gc -- is
// created at the priority of the task that created it, because
// cyclonedds/src/ddsrt/src/threads/freertos/threads.c:404-411 falls back to
// uxTaskPriorityGet(NULL) whenever attr->schedPriority is 0, which is what
// ddsi always passes. They are all created from the Controller constructor
// on actuation_task, so they are all TIED at ACTUATION_TASK_PRIORITY (2).
// What that tie does NOT mean: it does not mean one worker that stops
// blocking freezes the others. configUSE_TIME_SLICING == 0 removes only the
// TICK-driven switch (rcar_bsp's Source/tasks.c:4810-4841); task selection
// itself still uses listGET_OWNER_OF_NEXT_ENTRY (tasks.c:178-193 on this
// build, which has configUSE_PORT_OPTIMISED_TASK_SELECTION == 0), and that
// macro's own comment says it "indexes through the list, so the tasks of the
// same priority get an equal share of the processor time". Equal-priority
// READY tasks therefore DO round-robin on every context switch. So the tie
// only bites against a CPU-BOUND worker, and no spinning site has been
// identified anywhere in this stack -- every candidate blocks on a condition
// variable or a semaphore, and a blocked task yields. Keep the tie in view
// as a hazard; do not treat it as the explanation.
//
// Cost, stated rather than discovered. uxTaskGetSystemState() runs the whole
// walk inside vTaskSuspendAll(), and it calls prvTaskCheckFreeStackSpace()
// for every task -- the same byte-at-a-time scan whose cost this file's
// beacon comment already works through, now paid once per task rather than
// once per beacon. That is why this is sampled every
// X5H_DIAG_TASK_TABLE_EVERY beacons rather than every beacon, and why it is
// off by default.
//
// TWO CONSOLE ARTEFACTS THIS PRODUCES. Both are instrumentation, not signal,
// and both are cheap to misread as evidence in a one-shot log:
//
//   1. A burst of rx_drop_input_err immediately after a table. Printing a
//      table is ~900 B of BUSY-POLLED UART at 115200 (~80 ms) issued from
//      this beacon task at priority 31 -- above tcpip_thread (4) and above
//      rpmsg_poll_task (5). Nothing drains the RPMsg virtqueue into lwIP for
//      that whole 80 ms, so inbound frames pile up and netif->input rejects
//      them. That is the beacon's own footprint. Only drops that are NOT
//      adjacent to a table carry information.
//   2. Spliced lines. _write() (rcar_bsp/.../drivers/serial/serial.c:249)
//      takes no task-level mutex, so this priority-31 task can preempt a
//      priority-2 CycloneDDS thread part-way through a trace line and
//      interleave its own output into it -- e.g. an "x5h-diag: task ..."
//      fragment appearing in the middle of an "SPDP ST0 ... NEW". Grep the
//      log tolerantly (match on the token, not on whole lines anchored at
//      both ends); do not conclude a discovery line is absent because it did
//      not match a strict pattern.
//
// The scheduler-lock objection that disqualified mallinfo() from
// diag_put_resources() does NOT transfer here, and the difference is worth
// being explicit about. That objection was specific: mallinfo() walks
// NEWLIB's arena, whose chunk headers are corrupted by exactly the stack
// overflow the beacon exists to report, so the walk can loop forever with
// the scheduler suspended and turn one failure into a false report of
// another. uxTaskGetSystemState() walks FREERTOS's own ready/blocked/
// suspended lists instead. If those were corrupt the scheduler would already
// have failed and there would be no beacon to print anything at all, so this
// walk cannot be the thing that converts a live image into a silent one.
#if defined(X5H_DIAG_TASK_TABLE) && (X5H_DIAG_TASK_TABLE)

// offsetof(), used only by R3d below (diag_put_controller_raw_words()) to
// derive a TCB member offset from the public StaticTask_t stand-in without
// touching FreeRTOS kernel sources. Gated here, not at file scope, so this
// build-only dependency cannot affect the default build's translation unit.
#include <stddef.h>

// x5h_emutls_task_cleanup(), used only by R3e below. Gated for the same
// reason as <stddef.h> above.
#include "x5h_emutls.h"

// Static, not an automatic: the beacon runs on configMINIMAL_STACK_SIZE (256
// words == 1 KiB) and sizeof(TaskStatus_t) is ~40 bytes here, so 24 of them
// is ~1 KiB -- the whole beacon stack -- if it were a local.
#define X5H_DIAG_TASK_TABLE_SLOTS 24
//
// THE COUPLING, stated explicitly so it cannot be silently re-broken. This
// constant is a beacon-count, not a time: the table cadence it produces is
// X5H_DIAG_TASK_TABLE_EVERY * X5H_DIAG_BEACON_PERIOD_TICKS, and the second
// factor is itself conditioned on X5H_DIAG_TASK_TABLE (see that definition
// above) -- the same 10 s cadence this constant was originally sized
// against at a 5 s beacon period, which Task 28 restores after Task 26's
// interim 1 s/EVERY=3 (effectively 3 s) diagnostic-build cadence. This
// constant and the table code it gates exist only inside this
// X5H_DIAG_TASK_TABLE block; the default build has no table, no cadence,
// and no version of this constant at all -- there is nothing on the
// default-build side for "the same cadence" to be relative to. Retuning
// the beacon period without also re-checking this constant silently
// retunes the table cadence too; that is exactly the trap review round 1
// on Task 26 flagged, so it is spelled out here rather than left for the
// next person to rediscover by noticing the tables arriving at an
// unexpected rate.
//
// Sized by the shortest window that has to yield a USABLE reading, not by
// what feels cheap. rt is only meaningful as a delta between two dumps, and
// the highest-value observation in the original (Task 23) board session was
// a 30 s window (peers stopped, waiting to see whether the 10 s participant
// lease expires). At one table per 30 s that window can contain exactly one
// table and therefore no delta at all -- the measurement would be missing
// from precisely the interval it was built for. At 10 s it contained three.
//
// Task 26's narrower ~6.5 s controller death window is why this constant
// stayed at EVERY=2 rather than reverting to this diagnostic build's own
// previous EVERY=3 alongside the beacon period. The real argument is a
// landing-probability one, not a "how many tables fit" one -- at most one
// table fits inside a 6.5 s window at EITHER cadence (two tables 10 s or
// 15 s apart cannot both land inside a 6.5 s span), so the choice is about
// how often a table lands in the window AT ALL, not how many. For a
// boot-relative window at a random phase against the table's own
// (also boot-relative) cadence, that landing probability is approximately
// window / cadence: ~65 % at EVERY=2's 10 s cadence (6.5 / 10) versus ~43 %
// at EVERY=3's 15 s cadence (6.5 / 15) -- EVERY=2 gives roughly 1.5x the
// chance of a boot's window containing a table boundary at all, which is
// the argument for keeping it at 2 rather than 3 now that the beacon period
// itself is 5 s. This is weaker than Task 26's original 1 s-period claim of
// "2-3 tables reliably inside the window" (a coverage guarantee, not a
// probability), which depended on the 1 s beacon period Task 28 reverted
// (see X5H_DIAG_BEACON_PERIOD_TICKS above) and does not survive that
// reversion. diag_put_controller_probe()'s `ntasks` scalar (see "R3c"
// below), printed every beacon at the same 5 s period, is what actually
// brackets the death window reliably now; the table is corroboration when
// its timing happens to cooperate, not the primary signal.
//
// Affordable at the current cadence: one table is ~15 tasks x ~60 chars ~=
// 900 B, and the console is 115200 8N1 (~11.5 kB/s), so a table is ~78 ms
// every 10 s -- ~0.78 % duty on its own, existing ONLY in the diagnostic
// build (the default build's table duty is 0 %, not a shared figure --
// the table code and its call site are both inside this
// X5H_DIAG_TASK_TABLE block). Combined with the per-beacon controller
// probe (see "R3c" below, ~6.1 ms every 5 s, ~0.12 %) and the beacon line
// both builds already pay (~10.4 ms every 5 s, ~0.21 %), the diagnostic
// build's own total console duty is ~1.1-1.2 % (cross-checked two
// independent ways at branch time); the table alone is roughly 70 % of
// that total and
// the table-plus-probe delta over the default build (~0.90 %) is roughly
// 87 % table. Well under the ~4-5.5 % this file produced at the 1 s beacon
// period Task 26 shipped and Task 28 has now reverted. See the artefact
// note below for what that ~78 ms does to the rest of the image while it
// happens.
#define X5H_DIAG_TASK_TABLE_EVERY 2
// M5 (review round 1): the beacon below gates the table on
// `(n % X5H_DIAG_TASK_TABLE_EVERY) == 1u`, which is never true if this
// constant is ever set to 1 (n % 1 is always 0) -- silently disabling the
// table entirely rather than printing it every beacon as "every 1st beacon"
// would suggest. Guarded here rather than in the beacon's own modulo test,
// so the failure is caught at the definition that would introduce it.
_Static_assert(X5H_DIAG_TASK_TABLE_EVERY >= 2,
               "X5H_DIAG_TASK_TABLE_EVERY must be at least 2: the beacon "
               "loop's `(n % X5H_DIAG_TASK_TABLE_EVERY) == 1u` gate is never "
               "true when this is 1, which would silently disable the task "
               "table instead of printing it every beacon.");
static TaskStatus_t s_task_status[X5H_DIAG_TASK_TABLE_SLOTS];

static char diag_task_state_char(eTaskState st)
{
    switch (st) {
        case eRunning:   return 'R';
        case eReady:     return 'r';
        case eBlocked:   return 'B';
        case eSuspended: return 'S';
        case eDeleted:   return 'D';
        default:         return '?';
    }
}

static void diag_put_task_table(void)
{
    configRUN_TIME_COUNTER_TYPE total = 0;
    UBaseType_t n =
        uxTaskGetSystemState(s_task_status, X5H_DIAG_TASK_TABLE_SLOTS, &total);

    if (n == 0) {
        // uxTaskGetSystemState() returns 0 -- and fills in nothing -- when the
        // array is too small for the number of tasks. Say so rather than
        // printing an empty table that reads as "no tasks".
        diag_puts("x5h-diag: tasks OVERFLOW slots=");
        diag_put_u32((uint32_t)X5H_DIAG_TASK_TABLE_SLOTS);
        diag_puts(" (raise X5H_DIAG_TASK_TABLE_SLOTS)\n");
        return;
    }

    for (UBaseType_t i = 0; i < n; i++) {
        const TaskStatus_t *t = &s_task_status[i];
        diag_puts("x5h-diag: task ");
        diag_puts(t->pcTaskName != NULL ? t->pcTaskName : "?");
        diag_puts(" state=");
        {
            char s[2];
            s[0] = diag_task_state_char(t->eCurrentState);
            s[1] = '\0';
            diag_puts(s);
        }
        diag_puts(" prio=");
        diag_put_u32((uint32_t)t->uxCurrentPriority);
        diag_puts(" hwm=");
        diag_put_u32((uint32_t)t->usStackHighWaterMark);
        // Printed as two 32-bit halves because the counter is 64-bit
        // (configRUN_TIME_COUNTER_TYPE is unsigned long long here) and the
        // console writers above are 32-bit only. Hex, so the two halves
        // concatenate into the real value by inspection.
        diag_puts(" rt=");
        diag_put_hex32((uint32_t)((uint64_t)t->ulRunTimeCounter >> 32));
        diag_puts(":");
        diag_put_hex32((uint32_t)((uint64_t)t->ulRunTimeCounter & 0xffffffffu));
        diag_puts("\n");
    }
    diag_puts("x5h-diag: task-total rt=");
    diag_put_hex32((uint32_t)((uint64_t)total >> 32));
    diag_puts(":");
    diag_put_hex32((uint32_t)((uint64_t)total & 0xffffffffu));
    diag_puts("\n");
}

// ---- R3c: controller liveness probe (Task 26) ----
//
// The table above answers WHICH CycloneDDS worker stopped. This answers a
// narrower, prior question a board session raised: the CR52's controller
// task (pthread_create()'s "pthread", spawned from Node::spin() in
// common/node/node.hpp, priority 1) disappears from that same table at
// ~192 s and never returns, and no reachable vTaskDelete() call and no fault
// path explains it. Four shapes remain -- deleted after all, a clobbered
// ready-list header, a stray xStateListItem unlink, or an overwritten TCB --
// and the probes in this file split them between themselves: the ntasks
// scalar below already answers deleted-vs-unlinked on its own (see the
// uxTaskGetNumberOfTasks() paragraph further down), and the raw
// scheduler-structure words ("R3d" below) are what discriminate the
// remaining three, which differ only in the link words themselves. Only
// the mechanism is described here.
//
// s_controller_task is set once, by x5h_diag_set_controller_task() (called
// from Node::spin() right after pthread_create() succeeds, guarded by this
// same macro), and only ever read here. No lock, for the identical reason
// s_launcher above has none: one aligned pointer, written once before
// anything could race it.
static TaskHandle_t volatile s_controller_task = NULL;

void x5h_diag_set_controller_task(TaskHandle_t controller)
{
    s_controller_task = controller;
}

// FAULT-SAFETY ORDERING, the point of this whole function. Once the
// controller task is truly gone, s_controller_task is a stashed handle into
// a TCB that may be deleted, unlinked, or overwritten -- exactly the shape
// of bug this firmware has prior form for (a free() of a borrowed TCB
// pointer, fixed 2026-08-14, still fresh enough to take seriously here).
// eTaskGetState(), pcTaskGetName() and uxTaskPriorityGet() (tasks.c) all
// dereference that TCB directly, and none of the four candidate shapes above
// can be ruled safe to read in advance -- that is exactly the ambiguity this
// probe exists to resolve, so it cannot also be assumed away first.
//
// uxTaskGetNumberOfTasks() carries none of that risk (tasks.c's own
// comment: "A critical section is not required because the variables are of
// type BaseType_t" -- a single global read, no pointer involved), and on
// its own it already answers deleted-vs-unlinked: 13 if the controller was
// really deleted out of this image's 14-task boot population, 14 if it was
// merely unlinked. So it is put on the wire FIRST and UNCONDITIONALLY,
// before the controller handle is touched at all. R_SERIAL_PutChar() is a
// synchronous polled UART write with no software buffer or queue (see this
// file's own output-path notes in x5h_diag.h) -- but "no queue" is not "no
// latency". CORRECTED (review round 1 on this task): an earlier revision of
// this comment claimed the ntasks bytes have "already left the UART" by the
// time diag_put_u32() returns. They have not, necessarily -- the BSP's own
// uart_rcar_poll_out() waits for TX FIFO SPACE and then writes the byte into
// it, so up to a FIFO's worth of bytes can still be sitting in the hardware
// FIFO, not yet shifted out onto the wire, when this function returns. What
// actually keeps that safe is not the print call itself but the fault path
// on the other side of it: R1's replacement exception vectors
// (x5h_diag_exception_report()) halt in a spin loop rather than resetting
// the core, so if the controller probe below then faults, the FIFO the
// ntasks line is sitting in is left alone and drains on its own -- nothing
// here needs the write to be complete, only for nothing downstream to ever
// reset the UART or the core before it finishes draining. Only the
// corroborating detail is lost to a fault here, not the capture, and losing
// detail costs far less than losing the one flash this diagnostic gets.
//
// This does not reintroduce the scheduler-lock hazard diag_put_resources()
// and the R3b comment above both rule out elsewhere in this file. Unlike
// mallinfo() (an unbounded arena walk under vTaskSuspendAll()),
// eTaskGetState() and uxTaskPriorityGet() each wrap exactly one struct read
// in taskENTER_CRITICAL()/taskEXIT_CRITICAL() -- portDISABLE_INTERRUPTS()
// around a fixed handful of field reads, not a walk with unbounded length.
// There is nothing here for corrupted state to loop forever in; the worst
// case is a data abort taken with IRQ masked, which lands in R1 exactly as
// any other fault would, not a permanently suspended scheduler.
//
// IMPORTANT, and specific to this probe alone: this file's other name
// prints (diag_put_task_table() above, diag_put_resources()'s launcher
// name) all pass a plain, unbounded diag_puts() over pcTaskGetName(), and
// that is fine there -- every handle those two ever see comes straight out
// of uxTaskGetSystemState()'s own walk of FreeRTOS's live ready/blocked/
// suspended lists, so the TCB it points at is, by construction, one the
// scheduler still considers valid. This probe is different on purpose: the
// whole point of s_controller_task is to read a handle the scheduler may no
// longer be able to vouch for at all, and "overwritten TCB" is one of the
// four shapes it exists to distinguish. pcTaskGetName() returns
// &pxTCB->pcTaskName[0], a plain configMAX_TASK_NAME_LEN-byte char array
// (FreeRTOSConfig.h) with no length prefix; if the bytes at and after it
// have been scribbled over there is no guarantee of a NUL anywhere nearby,
// and this file's normal diag_puts() would walk off the end of the array
// and keep going through whatever memory follows until it happened to find
// a zero byte -- or didn't, and this priority-31 task (above every other
// task in the image, per X5H_DIAG_BEACON_PRIORITY above) then never returns
// to sleep, at 115200 baud, forever. Unlike a fault -- which halts and is
// therefore an acceptable outcome here, see above -- an unterminated walk
// never halts, so it would starve the rest of the image and destroy exactly
// the corroborating evidence (later task tables, the level-3 discovery
// trace, later ntasks deltas) this diagnostic is flashed to collect.
// diag_puts_bounded() below is the fix: at most configMAX_TASK_NAME_LEN
// characters, full stop, whether or not a NUL ever turns up.
static void diag_puts_bounded(const char *s, uint32_t max_len)
{
    uint32_t i;

    if (s == NULL) {
        diag_puts("<null>");
        return;
    }
    for (i = 0; i < max_len && s[i] != '\0'; i++) {
        // Same '\n' -> "\r\n" translation diag_puts() does above, for the
        // same reason: a garbage name byte that happens to be '\n' should
        // not be allowed to corrupt a raw serial capture's line structure.
        if (s[i] == '\n') {
            diag_putc('\r');
        }
        diag_putc(s[i]);
    }
}

// ---- R3d: raw scheduler-structure words (Task 30) ----
//
// diag_put_controller_probe() below distinguishes deleted-vs-unlinked from
// ntasks alone, but cannot tell a clobbered ready-list header, a stray
// xStateListItem unlink, and an overwritten TCB apart from each other --
// those three shapes differ only in the raw link words themselves. This
// adds them: the controller TCB's own xStateListItem (pxNext, pxPrevious,
// pxContainer) and the priority-1 ready list header (the controller's own
// priority) those pointers should agree with when nothing is corrupted.
//
// Reaching both without touching FreeRTOS kernel sources (forbidden by this
// task's brief) needed two different techniques, derived and evidenced at
// branch time; everything load-bearing about them is inlined here:
//
// TCB offset of xStateListItem: StaticTask_t (FreeRTOS.h) is the PUBLIC
// struct FreeRTOS ships so applications can size a TCB without seeing the
// private tskTaskControlBlock definition in tasks.c. Its own doc comment
// guarantees its size/alignment match the real struct "no matter how the
// values in FreeRTOSConfig.h are set", and tasks.c's own
// configASSERT(sizeof(StaticTask_t) == sizeof(TCB_t)) enforces exactly that
// every time this build creates a task -- including the controller itself
// (pthread_create() -> xTaskCreateStatic()). StaticTask_t's second field,
// `StaticListItem_t xDummy3[2]`, mirrors the real struct's `xStateListItem;
// xEventListItem;` pair under the identical #if conditions, so
// offsetof(StaticTask_t, xDummy3) IS offsetof(TCB_t, xStateListItem) --
// computed by the compiler, not guessed, and correct regardless of which of
// this branch's two disagreeing FreeRTOSConfig.h copies wins
// (configMAX_TASK_NAME_LEN 16 vs. 32), since this
// offset never reads that macro at all.
_Static_assert(sizeof(ListItem_t) == sizeof(((StaticTask_t *)0)->xDummy3[0]),
    "StaticTask_t's documented size contract (FreeRTOS.h, above struct "
    "xSTATIC_TCB) no longer holds -- xDummy3[0] no longer stands in for "
    "ListItem_t, so the TCB offset derived from it cannot be trusted");
// Second, independent check on the same offset: read directly out of this
// build's own DWARF (`arm-none-eabi-readelf --debug-dump=info`, struct
// tskTaskControlBlock, member xStateListItem's
// DW_AT_data_member_location) rather than trusted only on the paragraph
// above. Both numbers agree at 4 for this build (pxTopOfStack alone
// precedes it here: no MPU wrapper, no multi-core affinity field in this
// config); re-run that readelf against the built ELF to re-confirm.
_Static_assert(offsetof(StaticTask_t, xDummy3) == 4,
    "offsetof(StaticTask_t, xDummy3) no longer matches the DWARF-confirmed "
    "offsetof(TCB_t, xStateListItem) == 4 (derivation in the comment "
    "above) -- the TCB layout changed (a new field ahead of xStateListItem, "
    "most likely); re-derive both before trusting "
    "diag_put_controller_raw_words()");
//
// pxReadyTasksLists[1] address: two INDEPENDENT derivations, compared
// against each other rather than either one trusted alone -- see "HONESTY
// ON THE RUNTIME PATH" below for why that comparison matters, not just the
// mechanics of getting an address.
//
// (1) Build-time literal. `static List_t pxReadyTasksLists[configMAX_PRIORITIES]`
// (tasks.c) has internal linkage, so no `extern` declaration anywhere else
// can bind to it -- the "declare it extern if the symbol is global" path
// this task's brief offers does not apply to THIS symbol (it does apply to
// pxCurrentTCB below). The literal is read from this exact build's own
// linked symbol table (`arm-none-eabi-nm -nS`), the same known-address
// mechanism Task 29 used for this identical symbol, and validated by a
// build-measure-embed-rebuild convergence check: build, read the symbol's
// address with `nm -n <elf> | grep pxReadyTasksLists`, update the literal,
// rebuild, and repeat until the address is stable across a rebuild that
// embeds it. It is NOT a portable constant, and the
// single most likely thing to move it is THIS FILE'S OWN statics: on this
// build, x5h_diag.c.o's `s_launcher`/`s_controller_task` link at a LOWER
// address than tasks.c.o's pxReadyTasksLists (verified with `nm` on this
// build at branch time), i.e. ahead of it in the one contiguous region
// lscript_vram2.ld places .data/.bss in -- so any static THIS FILE adds or
// resizes (e.g. widening s_task_status[X5H_DIAG_TASK_TABLE_SLOTS] above)
// shifts this literal. Re-derive with `nm` whenever this file's statics
// change -- which is exactly what derivation (2) below now checks for at
// run time, so a stale literal is caught instead of silently believed.
#define X5H_PXREADYTASKSLISTS_ADDR 0x11890ddcUL

// (2) Run-time derivation, no literal involved. pxCurrentTCB IS a global
// here (tasks.c:446 declares it without `static` when configNUMBER_OF_CORES
// == 1, which this build is; confirmed by `nm`: capital `B pxCurrentTCB`)
// -- the brief's "declare it extern if the symbol is global" path applies
// to this one. FreeRTOS's own round-robin bookkeeping keeps a RUNNING task
// linked in its own ready list (only unlinked when it stops running), so
// for as long as this code is executing ON the beacon task, pxCurrentTCB's
// xStateListItem.pxContainer -- via the identical offsetof(StaticTask_t,
// xDummy3) technique above -- equals &pxReadyTasksLists[uxTaskPriorityGet(NULL)],
// true regardless of what the literal above says. If a multi-core config
// is ever adopted, tasks.c's real pxCurrentTCB stops being a plain global
// (it becomes an array, pxCurrentTCBs[]); this extern would then fail to
// LINK, a loud build break rather than a silent wrong read.
extern TaskHandle_t volatile pxCurrentTCB;

// HONESTY ON THE RUNTIME PATH: pxCurrentTCB is not immune to the very
// corruption this probe exists to catch. It is a plain global living in
// the same tasks.c static-storage region as pxReadyTasksLists -- on this
// build `nm` places pxCurrentTCB just 4 bytes before it (both addresses
// read from this build's `nm` at branch time) -- so a wild write wide
// enough to reach one can
// reach the other. Agreement between the two derivations is therefore
// meaningful evidence neither has been touched; disagreement tells us ONLY
// that something is inconsistent, not which side (if either) is still
// correct. That is why diag_put_controller_raw_words() below prints BOTH
// raw addresses on a mismatch and skips the four fields that would require
// picking one to dereference, instead of silently preferring either.
static List_t *diag_derive_rdy1_runtime(void)
{
    TaskHandle_t self = pxCurrentTCB;
    ListItem_t *self_item =
        (ListItem_t *)((uint8_t *)self + offsetof(StaticTask_t, xDummy3));
    UBaseType_t self_prio = uxTaskPriorityGet(NULL);

    // self_item->pxContainer == &pxReadyTasksLists[self_prio] (see above);
    // step back to index 1 in pointer arithmetic only -- no loop, no walk,
    // output stays fully bounded either way.
    return self_item->pxContainer - (self_prio - 1U);
}

// Fault-safety ordering, re-checked against the actual control flow below
// (round 2 -- an earlier version of this comment claimed an ordering the
// code did not yet have; the code now matches it). ctlitem's four fields
// derive only from `controller`, the independently-stashed handle
// diag_put_controller_probe() already dereferences safely above (same risk
// class as its existing state/name/prio line, unchanged by this task).
// rdy1_lit is a compile-time literal plus pointer arithmetic -- no
// dereference, cannot fault. diag_derive_rdy1_runtime(), by contrast, ends
// in a genuine dereference through pxCurrentTCB ("HONESTY ON THE RUNTIME
// PATH" above), and x5h_diag_exception_report() is noreturn, so a fault
// there would abandon everything not yet on the wire. That call is
// therefore placed AFTER ctlitem's four fields are printed, not before --
// so a fault deriving rdy1_rt costs only the rdy1 fields, never ctlitem's,
// which are the ones that discriminate the stray-unlink shape.
static void diag_put_controller_raw_words(TaskHandle_t controller)
{
    ListItem_t *ctlitem =
        (ListItem_t *)((uint8_t *)controller + offsetof(StaticTask_t, xDummy3));
    List_t *rdy1_lit = &((List_t *)X5H_PXREADYTASKSLISTS_ADDR)[1];
    List_t *rdy1_rt;

    diag_puts("x5h-diag: ctlitem=");
    diag_put_hex32((uint32_t)(uintptr_t)ctlitem);
    diag_puts(" next=");
    diag_put_hex32((uint32_t)(uintptr_t)ctlitem->pxNext);
    diag_puts(" prev=");
    diag_put_hex32((uint32_t)(uintptr_t)ctlitem->pxPrevious);
    diag_puts(" cont=");
    diag_put_hex32((uint32_t)(uintptr_t)ctlitem->pxContainer);

    // Only now, with ctlitem's fields already on the wire, do we touch
    // pxCurrentTCB -- see the function comment above.
    rdy1_rt = diag_derive_rdy1_runtime();

    if (rdy1_lit != rdy1_rt) {
        // Disagreement: print both raw candidates and stop. Dereferencing
        // either one here would be silently picking a side -- see
        // "HONESTY ON THE RUNTIME PATH" above.
        diag_puts(" rdy1=stale lit=");
        diag_put_hex32((uint32_t)(uintptr_t)rdy1_lit);
        diag_puts(" rt=");
        diag_put_hex32((uint32_t)(uintptr_t)rdy1_rt);
        diag_puts("\n");
        return;
    }

    diag_puts(" rdy1=");
    diag_put_hex32((uint32_t)(uintptr_t)rdy1_lit);
    diag_puts(" n=");
    diag_put_u32((uint32_t)rdy1_lit->uxNumberOfItems);
    diag_puts(" idx=");
    diag_put_hex32((uint32_t)(uintptr_t)rdy1_lit->pxIndex);
    diag_puts(" endn=");
    diag_put_hex32((uint32_t)(uintptr_t)rdy1_lit->xListEnd.pxNext);
    diag_puts(" endp=");
    diag_put_hex32((uint32_t)(uintptr_t)rdy1_lit->xListEnd.pxPrevious);
    diag_puts("\n");
}

static void diag_put_controller_probe(void)
{
    TaskHandle_t controller = s_controller_task;

    diag_puts("x5h-diag: ntasks=");
    diag_put_u32((uint32_t)uxTaskGetNumberOfTasks());
    diag_puts("\n");

    if (controller == NULL) {
        // Not yet stashed (before Node::spin() has run) or never will be on
        // this image (netif_only_x5h never constructs a Controller at all).
        // Mirrors the existing rule exactly: unset means print the <unset>
        // variant of every line this function owns and touch nothing.
        diag_puts("x5h-diag: controller=<unset>\n");
        diag_puts("x5h-diag: ctlitem=<unset>\n");
        return;
    }

    diag_puts("x5h-diag: controller state=");
    {
        char s[2];
        s[0] = diag_task_state_char(eTaskGetState(controller));
        s[1] = '\0';
        diag_puts(s);
    }
    diag_puts(" name=");
    diag_puts_bounded(pcTaskGetName(controller), (uint32_t)configMAX_TASK_NAME_LEN);
    diag_puts(" prio=");
    diag_put_u32((uint32_t)uxTaskPriorityGet(controller));
    diag_puts("\n");

    diag_put_controller_raw_words(controller);
}

// ---- R3f: one-shot GIC tick-priority evidence line (Task 33) ----
//
// One line, once per boot, before the first beacon line:
//
//   x5h-diag: gic mpidr=0x........ aff0=N rd=0x........
//     tickprio_pre=0xNN tickprio_post=0xNN pmr=0xNN
//
// (a single line on the wire; wrapped here for width). Every field is
// fixed-format and bounded; nothing repeats. What each one is:
//
//  - mpidr, aff0 (= MPIDR & 0xFF): read on this core at boot. aff0 is the
//    index the pre-Task-33 Irq_SetPriority() applied to the already-per-core
//    gic_rdist base (see vendor_patched/interrupts.c), so aff0 != 0 is the
//    precondition for the tick PPI's priority write having missed this
//    core's frame on every image before the fix. This value has never been
//    observed on this hardware.
//  - tickprio_pre: GICR_IPRIORITYR[30] (the tick PPI) read from this core's
//    own SGI/PPI frame in main(), after Irq_Setup() seeds gic_rdist and
//    before vTaskStartScheduler() -> vConfigureTickInterrupt() programs the
//    tick priority. It is the value the byte held before THIS boot wrote
//    it -- i.e. the priority the tick actually ran at, all run long, on
//    every earlier image whose Irq_SetPriority() never reached this frame.
//  - tickprio_post: the same byte re-read at beacon start, after
//    vConfigureTickInterrupt() ran through the corrected accessor --
//    IPRIORITY(24) back means the fixed write lands where this read looks.
//  - pmr: ICC_PMR read between taskENTER_CRITICAL()/taskEXIT_CRITICAL(),
//    i.e. the masking threshold a critical section actually programs
//    (port.c: configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT).
//    GICv3 only signals an interrupt whose priority is numerically BELOW
//    PMR, so a critical section holds the tick out only while
//    tickprio >= pmr.
//
// Frame derivation -- deliberately NOT the indexing being measured:
// gic_rdist (seeded by Irq_Setup() with this core's own GICR frame address)
// indexed by R_GIC_GetRedistID(), which returns the index whose GICR_TYPER
// affinity field -- hardware ground truth burned into each redistributor --
// matches this core's own MPIDR Aff0. That is the same derivation the
// board-proven Irq_Enable() path uses, and it is self-correcting: whatever
// index the TYPER match selects IS this PE's frame, regardless of any
// table arithmetic. If the lookup fails, rd stays 0xffffffff, neither
// priority byte is ever dereferenced, and both print as 0xff -- the rd
// field on the same line is what disambiguates that sentinel from a
// genuine 0xff.
//
// R_GIC_GetRedistID()'s internal scan is bounded by gic_max_rd, the frame
// count Irq_Setup() already walked to at boot -- not an unbounded walk.
static uint32_t s_gic_mpidr;
static uint32_t s_gic_rd = 0xFFFFFFFFu;
static uint8_t s_gic_tickprio_pre = 0xFFu;

// gic.c defines this without a header declaration of its own; the type
// matches its definition (GICR_Type* gic_rdist).
extern GICR_Type *gic_rdist;

void x5h_diag_gic_capture_boot(void)
{
    uint32_t mpidr;
    uint32_t rd;

    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r" (mpidr));
    s_gic_mpidr = mpidr;

    // Same affinity argument Irq_Enable() passes (MPIDR Aff1|Aff0).
    rd = R_GIC_GetRedistID(mpidr & 0xFFFFu);
    if (rd == 0xFFFFFFFFu) {
        return;  // statics keep their "unreadable" sentinels
    }
    s_gic_rd = rd;
    s_gic_tickprio_pre =
        gic_rdist[rd].sgi_ppi.GICR_IPRIORITYR[R_OS_BSP_GENERIC_ARM_TIMER_IRQNUM];
}

static void diag_put_hex8(uint8_t v)
{
    static const char hexdigits[] = "0123456789abcdef";

    diag_puts("0x");
    diag_putc(hexdigits[(v >> 4) & 0xFU]);
    diag_putc(hexdigits[v & 0xFU]);
}

static void diag_put_gic_oneshot(void)
{
    uint8_t tickprio_post = 0xFFu;
    uint32_t pmr;

    if (s_gic_rd != 0xFFFFFFFFu) {
        tickprio_post =
            gic_rdist[s_gic_rd].sgi_ppi.GICR_IPRIORITYR[R_OS_BSP_GENERIC_ARM_TIMER_IRQNUM];
    }
    taskENTER_CRITICAL();
    pmr = R_GIC_GetPriorityMask();
    taskEXIT_CRITICAL();

    diag_puts("x5h-diag: gic mpidr=");
    diag_put_hex32(s_gic_mpidr);
    diag_puts(" aff0=");
    diag_put_u32(s_gic_mpidr & 0xFFu);
    diag_puts(" rd=");
    diag_put_hex32(s_gic_rd);
    diag_puts(" tickprio_pre=");
    diag_put_hex8(s_gic_tickprio_pre);
    diag_puts(" tickprio_post=");
    diag_put_hex8(tickprio_post);
    diag_puts(" pmr=");
    diag_put_hex8((uint8_t)(pmr & 0xFFu));
    diag_puts("\n");
}

// ---- R3e: boot-time TLS self-test (Task 31) ----
//
// One-shot, before the first beacon line: two short-lived tasks write
// distinct values to the same __thread variable and cross-check. Under the
// per-task emutls runtime (x5h_emutls.c) each task sees its own instance,
// initialized from the template; under the single-slot libgcc runtime this
// image shipped with until Task 31, the second task to run reads the first
// task's value instead of the template, and each task's final readback sees
// the other's write. The verdict line is on-hardware evidence about the TLS
// runtime itself, independent of anything DDS later does with it.
//
// Ordering: the beacon and both test tasks outrank everything the actuation
// launcher runs (the _Static_assert below anchors that the same way the
// beacon's own priority assert does), and the launcher -- the task every
// DDS call in this image descends from -- has not run one instruction until
// all three block or finish. So the verdict is printed before the DDS stack
// can begin to exist.
//
// The test tasks allocate: their TLS storage, through the runtime under
// test. That is the subject of the test, not a breach of this file's
// no-allocation rule -- that rule exists because diagnostics run in wedged
// or faulting contexts (x5h_diag.h), and this runs once, at boot, on a
// healthy scheduler. The beacon's steady-state loop stays allocation-free,
// and nothing here touches its period, the probe, or any periodic output.
#define X5H_TLS_SELFTEST_PRIORITY (X5H_DIAG_BEACON_PRIORITY - 1)
_Static_assert(X5H_TLS_SELFTEST_PRIORITY > RPMSG_POLL_TASK_PRIORITY,
               "the TLS self-test tasks must outrank the network stack and "
               "the actuation launcher below it, or the launcher could "
               "start the DDS stack before the TLS verdict is printed");

#define X5H_TLS_SELFTEST_STACK_WORDS configMINIMAL_STACK_SIZE

// Template-initialized probe. The value is arbitrary but must be nonzero:
// a zero template would make template-initialization indistinguishable
// from the zero-fill an uninitialized __thread variable gets.
#define X5H_TLS_PROBE_TEMPLATE 0x7A5C0FFEU
static __thread uint32_t s_tls_probe = X5H_TLS_PROBE_TEMPLATE;

static StaticTask_t s_tls_test_tcb[2];
static StackType_t s_tls_test_stack[2][X5H_TLS_SELFTEST_STACK_WORDS];
static TaskHandle_t s_tls_test_peer[2];  // [i] = the OTHER task's handle
static TaskHandle_t s_tls_test_runner;
// Per-task findings, composed into the verdict's detail word (task 0 in
// bits 7:0, task 1 in bits 15:8):
//   0x01 first read != template
//   0x02 final readback != this task's own write
//   0x04 peer handshake timed out
static volatile uint32_t s_tls_test_flags[2];

static void diag_tls_selftest_task(void *pv)
{
    uintptr_t self = (uintptr_t)pv;
    uint32_t mine = (self == 0U) ? 0x000A5A5AU : 0x005A0A0AU;
    uint32_t flags = 0U;

    if (s_tls_probe != X5H_TLS_PROBE_TEMPLATE) {
        flags |= 0x01U;
    }
    s_tls_probe = mine;
    xTaskNotifyGive(s_tls_test_peer[self]);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) == 0U) {
        flags |= 0x04U;
    }
    // Both tasks have written by now (or the handshake failed); a shared
    // slot would leave the later writer's value visible to both.
    if (s_tls_probe != mine) {
        flags |= 0x02U;
    }
    s_tls_test_flags[self] = flags;
    xTaskNotifyGive(s_tls_test_runner);
    // Free this task's own TLS storage before self-deleting -- also the
    // one boot-time exercise of the cleanup path.
    x5h_emutls_task_cleanup(NULL);
    vTaskDelete(NULL);
}

static void diag_run_tls_selftest(void)
{
    uint32_t detail = 0U;
    TaskHandle_t t0;
    TaskHandle_t t1;

    s_tls_test_runner = xTaskGetCurrentTaskHandle();
    t0 = xTaskCreateStatic(diag_tls_selftest_task, "tls_test0",
                           X5H_TLS_SELFTEST_STACK_WORDS, (void *)0,
                           X5H_TLS_SELFTEST_PRIORITY,
                           s_tls_test_stack[0], &s_tls_test_tcb[0]);
    t1 = xTaskCreateStatic(diag_tls_selftest_task, "tls_test1",
                           X5H_TLS_SELFTEST_STACK_WORDS, (void *)1,
                           X5H_TLS_SELFTEST_PRIORITY,
                           s_tls_test_stack[1], &s_tls_test_tcb[1]);
    if (t0 == NULL || t1 == NULL) {
        diag_puts("x5h-diag: TLS_SELFTEST_FAIL detail=create\n");
        return;
    }
    // Both test tasks sit READY below this task's priority until the takes
    // below block, so both peer handles are published before either runs.
    s_tls_test_peer[0] = t1;
    s_tls_test_peer[1] = t0;

    if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(2000)) == 0U ||
        ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(2000)) == 0U) {
        detail |= 0x10000U;
    }
    detail |= s_tls_test_flags[0] | (s_tls_test_flags[1] << 8);

    if (detail == 0U) {
        diag_puts("x5h-diag: TLS_SELFTEST_PASS\n");
    } else {
        diag_puts("x5h-diag: TLS_SELFTEST_FAIL detail=");
        diag_put_hex32(detail);
        diag_puts("\n");
    }
}
#endif  // X5H_DIAG_TASK_TABLE

static void x5h_diag_beacon_task(void *pv)
{
    uint32_t n = 0;

    (void)pv;
#if defined(X5H_DIAG_TASK_TABLE) && (X5H_DIAG_TASK_TABLE)
    // One-shot GIC evidence line first -- see "R3f". Ahead of the TLS
    // self-test because that test can block for seconds on its handshake
    // timeouts, and this is the decisive datum of the boot.
    diag_put_gic_oneshot();
    // One-shot TLS verdict ahead of the first beacon line -- see "R3e".
    diag_run_tls_selftest();
#endif
    for (;;) {
        n++;
        diag_puts("x5h-diag: beacon #");
        diag_put_u32(n);
        diag_puts(" uptime_ms=");
        diag_put_u32(diag_uptime_ms());
        diag_put_resources();
        diag_puts("\n");
#if defined(X5H_DIAG_TASK_TABLE) && (X5H_DIAG_TASK_TABLE)
        // Every period (Task 26): the decisive ntasks scalar plus, when it
        // is safe to have reached this far, the controller's own state/name/
        // priority. Ahead of the every-2nd-beacon full table below, though
        // the two are independent and their relative order does not affect
        // the fault-safety property diag_put_controller_probe() itself
        // documents.
        diag_put_controller_probe();
        if ((n % X5H_DIAG_TASK_TABLE_EVERY) == 1u) {
            diag_put_task_table();
        }
#endif
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
