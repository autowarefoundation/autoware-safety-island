#ifndef PLATFORM__CAN_H_
#define PLATFORM__CAN_H_

#if defined(PLATFORM_ZEPHYR)
  #include "platform/zephyr/zephyr_can.h"
#elif defined(PLATFORM_FREERTOS_CAN_MOCK)
  #include "platform/freertos/freertos_can_mock.h"
#elif defined(PLATFORM_FREERTOS_POSIX)
  #include "platform/freertos/freertos_can_socketcan.h"
#elif defined(PLATFORM_FREERTOS)
  #include "platform/freertos/freertos_can_stub.h"
#else
  #error "No platform defined. Define PLATFORM_ZEPHYR or PLATFORM_FREERTOS."
#endif

#endif  // PLATFORM__CAN_H_
