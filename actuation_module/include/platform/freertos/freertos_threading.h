// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__THREADING_H_
#define PLATFORM__FREERTOS__THREADING_H_

#include <cstddef>

// On the FreeRTOS POSIX simulator, thread stacks are regular memory.
// On real FreeRTOS hardware (Phase 5), xTaskCreate allocates stacks internally.
// These macros provide source-level compatibility with the Zephyr call sites.
// Note: Call sites already use "static K_THREAD_STACK_DEFINE(...)" so the
// macro must NOT include "static" to avoid "static static" duplication.
#if defined(PLATFORM_FREERTOS_S32Z2)
// On the S32Z2 the only K_THREAD_STACK_DEFINE caller is the controller node
// thread, whose stack is large (>=512 KiB) because the MPC/Eigen control cycle
// is stack-heavy (it overflowed the previous 256 KiB). NXP's linker puts
// .sram_data (incl. these statics) in the 512 KiB int_sram_dram, which has only
// ~100 KiB free, so the stack cannot grow there. Place it in a dedicated NOLOAD
// .node_stack section that node_stack_in_sram.ld maps into the ~7 MiB int_sram
// code region (same approach as ucHeap; NOLOAD keeps it out of the ELF). The
// fill pattern is applied by FreeRTOS's create-time stack paint.
#define K_THREAD_STACK_DEFINE(name, size) \
    char __attribute__((aligned(16), section(".node_stack"))) name[size]
#else
#define K_THREAD_STACK_DEFINE(name, size) \
    char __attribute__((aligned(16))) name[size]
#endif

#define K_THREAD_STACK_SIZEOF(name) sizeof(name)

#endif  // PLATFORM__FREERTOS__THREADING_H_
