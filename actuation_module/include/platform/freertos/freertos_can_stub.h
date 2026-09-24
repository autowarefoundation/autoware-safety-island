#ifndef PLATFORM__FREERTOS__CAN_STUB_H_
#define PLATFORM__FREERTOS__CAN_STUB_H_

#include <cstddef>

#include "common/can/can_fd_frame.hpp"
#include "common/can/can_frame.hpp"
#include "common/logger/logger.hpp"

namespace common::can::platform
{

inline bool can_init()
{
  common::logger::log_error("CAN output is not supported on this FreeRTOS target");
  return false;
}

inline bool can_fd_active()
{
  return false;
}

inline bool can_send(const CanFrame &)
{
  return false;
}

inline bool can_send_batch(const CanFrame *, const std::size_t)
{
  return false;
}

inline bool can_send_fd(const CanFdFrame &)
{
  return false;
}

}  // namespace common::can::platform

#endif  // PLATFORM__FREERTOS__CAN_STUB_H_
