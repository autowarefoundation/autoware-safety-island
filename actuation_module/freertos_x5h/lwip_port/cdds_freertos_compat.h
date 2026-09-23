// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// FreeRTOS task-creation compatibility shim for building CycloneDDS's own
// target library (scripts/build-cdds-target.sh's standalone cross-build).
//
// cyclonedds/src/ddsrt/src/threads/freertos/threads.c (ddsrt_thread_create())
// unconditionally calls xTaskCreateFpu(), not plain xTaskCreate(). That
// function comes from NXP RTD's own FreeRTOS port (used by the S32Z2
// target): it is xTaskCreate() plus reserving a per-task FPU-context slot in
// TLS[0], needed because that port restores FPU state lazily, on first use,
// from whatever the TLS[0] pointer references -- xTaskCreate() alone leaves
// it NULL. R-Car BSP's own FreeRTOS port (rcar_bsp/) has no such function at
// all (grepped -- zero matches).
//
// CORRECTED (review round 1; the previous revision of this comment claimed
// the R-Car port gives every task an FPU context "for free" -- that was
// false): common/ARM_CR52/port.c's FPU-context behaviour is controlled by
// configUSE_TASK_FPU_SUPPORT, which is NOT set anywhere in
// rcar_bsp/.../FreeRTOSConfig.h, so it silently took FreeRTOS.h's own
// #ifndef default of 1 -- lazy, opt-in, requiring a vPortTaskUsesFPU() call
// that nothing in this codebase ever made. Plain xTaskCreate() alone did
// NOT give CycloneDDS's DDS threads a safe FPU context under that default.
//
// This is now correct because CMakeLists.txt explicitly forces
// configUSE_TASK_FPU_SUPPORT=2 on the freertos_bsp target (see that file's
// "FPU context for every task" section), which changes
// pxPortInitialiseStack() -- in the SAME port.c -- to unconditionally
// reserve FPU register space and mark every newly created task as already
// having a live FPU context, with no per-task opt-in required. Under that
// setting, plain xTaskCreate() is a correct, complete replacement for
// xTaskCreateFpu() for CycloneDDS's threads. This macro alias would be
// silently wrong again if that CMake definition were ever removed or
// weakened back to =1.
//
// xTaskCreate()'s six parameters (pxTaskCode, pcName, uxStackDepth,
// pvParameters, uxPriority, pxCreatedTask -- see
// rcar_bsp/FreeRTOS/Source/include/task.h) are exactly the six arguments
// threads.c passes to xTaskCreateFpu(), in the same order, so a plain
// function-like macro alias is a correct, complete replacement here -- not a
// partial shim papering over a signature mismatch.
//
// Force-included (-include) only for the standalone CycloneDDS cross-build;
// not needed by the main actuation_x5h link, since libddsc.a is linked
// there as a pre-built static library, not compiled from source (see
// CMakeLists.txt's "Pre-built CycloneDDS target library" section).
#ifndef CDDS_FREERTOS_COMPAT_H_
#define CDDS_FREERTOS_COMPAT_H_

#define xTaskCreateFpu xTaskCreate

#endif  // CDDS_FREERTOS_COMPAT_H_
