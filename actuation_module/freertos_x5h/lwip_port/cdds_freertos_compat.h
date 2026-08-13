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
// all (grepped -- zero matches) because it does not need one: every task's
// FPU context is saved/restored unconditionally as part of the normal
// context switch (configUSE_TASK_FPU_SUPPORT; see
// actuation_module/include/platform/freertos/x5h/pthread.h's own header
// comment, which already reached this same conclusion for our hand-written
// thread wrapper). So on this port, plain xTaskCreate() already gives
// CycloneDDS's DDS threads a safe FPU context -- no lazy-restore path, no
// TLS[0] convention, nothing extra to reserve.
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
