// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__NETWORK_H_
#define PLATFORM__NETWORK_H_

#if defined(PLATFORM_ZEPHYR)
  #include "platform/zephyr/zephyr_network.h"
#elif defined(PLATFORM_FREERTOS)
  #include "platform/freertos/freertos_network.h"
#else
  #error "No platform defined. Define PLATFORM_ZEPHYR or PLATFORM_FREERTOS."
#endif

#endif  // PLATFORM__NETWORK_H_
