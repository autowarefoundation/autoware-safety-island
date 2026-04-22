// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__CLOCK_H_
#define PLATFORM__FREERTOS__CLOCK_H_

#include "common/logger/logger.hpp"

// On the FreeRTOS POSIX simulator, the host system clock is already correct.
// SNTP synchronization is not needed. On real FreeRTOS hardware (Phase 5),
// this should be replaced with an actual SNTP or NTP client.
static inline int platform_init_clock_via_sntp(void) {
    common::logger::log_info("FreeRTOS sim: SNTP disabled, using host system clock\n");
    return 0;
}

#endif  // PLATFORM__FREERTOS__CLOCK_H_
