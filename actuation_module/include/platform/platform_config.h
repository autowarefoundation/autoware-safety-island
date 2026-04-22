// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__CONFIG_H_
#define PLATFORM__CONFIG_H_

#if defined(PLATFORM_ZEPHYR)
  #include "platform/zephyr/zephyr_config.h"
#elif defined(PLATFORM_FREERTOS)
  #include "freertos_config_generated.h"
#else
  #error "No platform defined. Define PLATFORM_ZEPHYR or PLATFORM_FREERTOS."
#endif

#endif  // PLATFORM__CONFIG_H_
