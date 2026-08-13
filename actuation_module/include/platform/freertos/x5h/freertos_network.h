// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__FREERTOS__X5H__NETWORK_H_
#define PLATFORM__FREERTOS__X5H__NETWORK_H_

#include "common/logger/logger.hpp"
#include "platform/freertos/x5h/lwip_init.h"

// Selected by platform_network.h when PLATFORM_FREERTOS_X5H is defined.
// Brings up lwIP over the RPMsg netif (static IP) before returning.
static inline int configure_network(void) {
    common::logger::log_info("FreeRTOS X5H: bringing up lwIP over rpmsg-eth\n");
    return lwip_bring_up_blocking();
}

#endif  // PLATFORM__FREERTOS__X5H__NETWORK_H_
