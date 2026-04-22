// Copyright (c) 2025, Arm Limited.
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM__THREADING_H_
#define PLATFORM__THREADING_H_

#if defined(PLATFORM_ZEPHYR)
  #include "platform/zephyr/zephyr_threading.h"
#elif defined(PLATFORM_FREERTOS)
  #include "platform/freertos/freertos_threading.h"
#else
  #error "No platform defined. Define PLATFORM_ZEPHYR or PLATFORM_FREERTOS."
#endif

#endif  // PLATFORM__THREADING_H_
