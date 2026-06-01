// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__S32Z2__NETWORK_H_
#define PLATFORM__FREERTOS__S32Z2__NETWORK_H_

#include "common/logger/logger.hpp"
#include "platform/freertos/s32z2/lwip_init.h"

// Selected by platform_network.h when PLATFORM_FREERTOS_S32Z2 is defined.
// Brings up lwIP + NETC and configures the interface (static IP) before
// returning.
static inline int configure_network(void) {
    common::logger::log_info("FreeRTOS S32Z2: bringing up lwIP + NETC\n");
    return lwip_bring_up_blocking();
}

#endif  // PLATFORM__FREERTOS__S32Z2__NETWORK_H_
