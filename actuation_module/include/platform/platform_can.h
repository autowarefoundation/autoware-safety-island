#ifndef PLATFORM__CAN_H_
#define PLATFORM__CAN_H_

#include "platform/platform_config.h"

#if defined(PLATFORM_ZEPHYR)
  #if defined(CONFIG_CONTROL_CMD_CAN_TRANSPORT_UDP_TUNNEL) && \
    CONFIG_CONTROL_CMD_CAN_TRANSPORT_UDP_TUNNEL
    #include "platform/zephyr/zephyr_can_udp_tunnel.h"
  #else
    #include "platform/zephyr/zephyr_can.h"
  #endif
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
