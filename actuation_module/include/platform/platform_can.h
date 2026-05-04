#ifndef PLATFORM__CAN_H_
#define PLATFORM__CAN_H_

#if defined(PLATFORM_ZEPHYR)
  #include "platform/zephyr/zephyr_can.h"
#elif defined(PLATFORM_FREERTOS)
  #include "platform/freertos/freertos_can.h"
#else
  #error "No platform defined. Define PLATFORM_ZEPHYR or PLATFORM_FREERTOS."
#endif

#endif  // PLATFORM__CAN_H_
