// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 18: the diagnostic surface that makes a silent CR52 wedge talk.
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
//      `b <self>` spins (rcar_bsp/.../common/asm_vectors.S:29-44) -- which
//      also explains the interrupts stopping, since UND/ABT/FIQ entry
//      architecturally masks IRQ;
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
// Everything here prints through the BSP serial driver's own
// R_SERIAL_PutChar() (a polled SCIF write; see rcar_bsp/.../drivers/serial/
// serial.c and scif.c) and formats integers by hand. Nothing on any path in
// this header allocates -- deliberately, because two of the three surviving
// candidates are reached from inside the allocator, and because printf()
// takes __malloc_lock (vTaskSuspendAll) and allocates its stdio buffer on
// first use.
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

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

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
