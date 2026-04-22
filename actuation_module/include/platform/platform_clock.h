// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__CLOCK_H_
#define PLATFORM__CLOCK_H_

// Each platform backend must provide:
//   static int platform_init_clock_via_sntp(void);

#if defined(PLATFORM_ZEPHYR)
  #include "platform/zephyr/zephyr_clock.h"
#elif defined(PLATFORM_FREERTOS)
  #include "platform/freertos/freertos_clock.h"
#else
  #error "No platform defined. Define PLATFORM_ZEPHYR or PLATFORM_FREERTOS."
#endif

#endif  // PLATFORM__CLOCK_H_
