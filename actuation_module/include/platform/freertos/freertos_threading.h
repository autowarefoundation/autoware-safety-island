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
#define K_THREAD_STACK_DEFINE(name, size) \
    char __attribute__((aligned(16))) name[size]

#define K_THREAD_STACK_SIZEOF(name) sizeof(name)

#endif  // PLATFORM__FREERTOS__THREADING_H_
