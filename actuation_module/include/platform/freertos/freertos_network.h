// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__NETWORK_H_
#define PLATFORM__FREERTOS__NETWORK_H_

#include "common/logger/logger.hpp"

// On the FreeRTOS POSIX simulator, the Linux host handles networking.
// No network configuration is needed -- CycloneDDS uses native sockets.
// On real FreeRTOS hardware (Phase 5), this will be replaced with lwIP setup.
static inline int configure_network(void) {
    common::logger::log_info("FreeRTOS sim: using host network (no configuration needed)\n");
    return 0;
}

#endif  // PLATFORM__FREERTOS__NETWORK_H_
