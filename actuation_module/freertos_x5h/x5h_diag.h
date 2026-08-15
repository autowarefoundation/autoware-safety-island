// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 18: the diagnostic surface that makes a silent CR52 wedge talk.
//
// THIS FILE IS THE CANONICAL RATIONALE for the whole diagnostic surface --
// what it is for, what it may and may not do, and how to read what it
// prints. x5h_diag.c, x5h_diag_vectors.S and README.md's "Diagnostics"
// section all point here rather than restating any of it. That is a
// deliberate response to this tree's own recurring failure mode: the
// review rounds on this branch were spent on comments that had drifted
// from the code they described, and four copies of one argument is four
// chances to drift. Implementation notes that are genuinely local to a
// file stay in that file; the argument lives here, once.
//
// This is instrumentation, not a fix. On the board the actuation image
// prints its way through transport bring-up and lwIP, reaches
// "controller -> Creating DDS domain with raw config", and then goes
// permanently silent -- no assert line, no stack-overflow line, no
// malloc-failed line, and no further RPMsg kicks to Linux. Every failure
// path in this image that would have PRINTED something was checked against
// captured console output and ruled out, which leaves only paths that are
// silent by construction:
//
//   1. a CPU exception trapped in one of the vendor vector table's bare
//      `b <self>` spins -- rcar_bsp/.../common/asm_vectors.S, where
//      undefined_handler (29-30), prefetch_handler (37-38), abort_handler
//      (40-41), reserved_handler (43-44) and fiq_handler (49-50) are each
//      an unconditional branch to themselves. This also explains the
//      interrupts stopping, since UND/ABT/FIQ entry architecturally masks
//      IRQ;
//   2. newlib's abort()/_exit() path, where -lnosys's `_exit` is a bare
//      while(1) with no output (ddsrt_malloc() calls abort() on allocation
//      failure);
//   3. a stack overflow deep in the CycloneDDS init chain that corrupts
//      context before FreeRTOS's printing hook can run, landing in (1).
//
// The three requirements below map one-to-one onto those, plus a beacon
// that removes the ambiguity that made "silence" unreadable in the first
// place:
//
//   R1  x5h_diag_install_vectors() -- an exception vector table whose
//       UND/PABT/DABT/reserved/FIQ handlers print the exception name, the
//       offset-corrected faulting PC, SPSR, the fault-status/address
//       registers, and the running task's name, then halt.
//   R2  strong abort()/_exit() definitions (in x5h_diag.c) that pre-empt
//       the -lnosys stubs and announce themselves.
//   R3  x5h_diag_start_beacon() -- a never-deleted 5 s liveness beacon
//       above every network task, carrying the launcher's stack watermark
//       and heap headroom. After this image, silence means dead.
//   R4  actuation_diag_mark() -- the same watermark/headroom pair printed
//       immediately before dds_create_domain_with_rawconfig(), giving a
//       known-good "just before the cliff" reading to compare the beacon's
//       last line against.
//
// ---- the one rule everything here obeys: never touch the allocator, and
// ---- never take the scheduler lock
//
// Not a style preference. Every one of these three constraints is a
// property of a specific wedge candidate:
//
//   - Two of the three candidates are reached from INSIDE the allocator.
//     heap_useNewlib.c's __malloc_lock() is vTaskSuspendAll() -- a global
//     scheduler lock, not a priority -- and ddsrt_malloc()'s failure path
//     calls abort(). A diagnostic that allocates re-enters the machinery
//     it is reporting on.
//   - printf() on this port allocates its stdio buffer on first use and
//     takes __malloc_lock() around it. The exception handlers can run with
//     the scheduler already suspended and with a corrupted context, where
//     neither is safe.
//   - Candidate 3 makes newlib's own heap metadata untrustworthy, not just
//     the heap contents. actuation_task's 128 KiB stack is a pvPortMalloc
//     block; the stack grows down toward pxStack[0]; newlib's chunk header
//     sits immediately below the pointer it returned. An overflow of that
//     stack corrupts a malloc chunk header almost by definition. So any
//     diagnostic that WALKS the arena (mallinfo(), and therefore
//     xPortGetFreeHeapSize()) can loop forever on a corrupted size field --
//     with the scheduler suspended, because it took __malloc_lock to do it.
//     Nothing here calls it. See diag_put_resources() in x5h_diag.c for
//     what is reported instead and why it is sufficient.
//
// A beacon whose job is to prove the scheduler is alive must not take the
// global scheduler lock in order to say so. sbrk(0) is the one exception
// and it is bounded: it suspends and resumes around three assignments, and
// with incr == 0 it cannot even fail.
//
// ---- the output path
//
// Everything prints through the BSP serial driver's own R_SERIAL_PutChar()
// and formats integers by hand. That call reaches the wire through two
// gates, both verified rather than assumed, and both worth naming because
// either one closing is a second way for this entire surface to emit
// nothing at all:
//
//   - log_sync (serial.c). False unless R_SERIAL_AMP_LogSync() is called,
//     and nothing in this repo calls it -- only its own declaration and
//     definition match a tree-wide grep. So R_SERIAL_PutChar() skips the
//     MFIS lock acquire and goes straight to console_putc().
//   - is_log_enable (scif.c:99, tested at scif.c:391). Initialised to
//     SCIF_LOG_STATE_ON and only ever cleared by R_SERIAL_SetLogState(),
//     which SystemInit() calls exactly once, and only when
//     &__CONFIG_SILENT_CONSOLE__ == 1 (system_rcar_gen5.c:258-261). That
//     weak linker symbol is defined as 0 in lscript_common.ld:17, so the
//     branch is not taken and the console stays on.
//
// Past those, console_putc() -> uart_rcar_poll_out() is a polled SCIF
// register write: no buffer, no lock, no allocation.
//
// ---- what silence means after this image
//
// The consequence a board session will actually observe, stated plainly
// because it is the whole point: the beacon cannot REPORT candidate 2. If
// a task suspends the scheduler and never resumes it, no task of any
// priority runs, this one included. The beacon detects that case by
// STOPPING. Before this image, a quiet console was ambiguous between
// "wedged" and "nothing was logging"; after it, silence means dead, and
// the exception vectors are what distinguish a fault from a scheduler
// lock. README.md's "Diagnostics" section turns that into a run-book.
//
// This header is included from x5h_diag_vectors.S as well as from C/C++,
// so everything that is not a bare #define sits behind __ASSEMBLER__.

#ifndef X5H_DIAG_H
#define X5H_DIAG_H

// ---- exception kind codes ----
//
// Passed in r0 from each vector entry stub in x5h_diag_vectors.S to
// x5h_diag_exception_report() in x5h_diag.c. Defined once, here, so the
// assembly stubs and the C name table cannot drift apart; x5h_diag.c pins
// the table's length against X5H_DIAG_EXC_COUNT with a _Static_assert.
#define X5H_DIAG_EXC_UNDEF 0
#define X5H_DIAG_EXC_PABT  1
#define X5H_DIAG_EXC_DABT  2
#define X5H_DIAG_EXC_RESV  3
#define X5H_DIAG_EXC_FIQ   4
#define X5H_DIAG_EXC_COUNT 5

#ifndef __ASSEMBLER__

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- the non-allocating console primitives, shared ----
//
// Thin exported wrappers over x5h_diag.c's file-static writers, added for
// Task 19's x5h_cdds_process.c. Exported rather than duplicated because the
// "never allocate, never take the scheduler lock" rule above is a property of
// THESE implementations: a second hand-rolled set of console writers would be
// a second thing to keep inside that rule, and this tree has already paid for
// duplicated rationale drifting from its code. They format by hand and reach
// the wire through R_SERIAL_PutChar(); x5h_diag_puts() also translates '\n' to
// "\r\n". x5h_diag_puts(NULL) prints "<null>" rather than faulting, which
// matters when the thing being reported may itself be a bad pointer.
void x5h_diag_puts(const char *s);
void x5h_diag_put_u32(uint32_t v);
void x5h_diag_put_i32(int32_t v);
void x5h_diag_put_hex32(uint32_t v);

// R1. Repoints VBAR at this module's own vector table (x5h_diag_vectors.S)
// and prints the old and new bases. Call it as the first statement of
// main(): boot.S has already programmed VBAR to the vendor table and
// entered SYS mode with I and F masked, and SystemInit() has already
// brought up the console, so this is the earliest point at which a
// replacement table can be installed AND report that it was.
//
// Nothing under rcar_bsp/ is touched to achieve this -- the vendor tree is
// frozen and asm_vectors.S stays byte-identical to the vendor sample. The
// only change to the running system is the value in VBAR.
void x5h_diag_install_vectors(void);

// R3. Creates the never-deleted liveness beacon task. `launcher` is the
// task whose stack high-water mark each beacon line reports -- on the
// actuation image that is actuation_task, the task that runs the DDS
// creation chain. Returns pdPASS on success.
BaseType_t x5h_diag_start_beacon(TaskHandle_t launcher);

// Detaches the beacon from `launcher`. MUST be called by the launcher task
// itself immediately before it deletes itself: after vTaskDelete() the
// handle is freed by the idle task, and uxTaskGetStackHighWaterMark() on it
// would be a use-after-free. Subsequent beacon lines report no launcher.
void x5h_diag_clear_launcher(void);

// R4. Prints one tagged diagnostic line carrying the same launcher stack
// watermark and heap headroom the beacon reports. Declared weak (with no
// definition) in include/common/dds/dds.hpp so every other platform's build
// resolves it to 0 and skips the call; this target defines it strongly, so
// in this image it always runs.
void actuation_diag_mark(const char *tag);

#ifdef __cplusplus
}
#endif

#endif  // __ASSEMBLER__

#endif  // X5H_DIAG_H
