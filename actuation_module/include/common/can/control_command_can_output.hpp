#ifndef COMMON__CAN__CONTROL_COMMAND_CAN_OUTPUT_HPP_
#define COMMON__CAN__CONTROL_COMMAND_CAN_OUTPUT_HPP_

#include <cstddef>
#include <cstdint>
#include <pthread.h>

#include "autoware/autoware_msgs/messages.hpp"
#include "common/can/control_command_encoder.hpp"
#include "common/can/control_command_output_mode.hpp"
#include "common/logger/logger.hpp"
#include "platform/platform_can.h"

namespace common::can
{

class ControlCommandCanOutput
{
public:
  ControlCommandCanOutput() = default;
  ControlCommandCanOutput(const ControlCommandCanOutput &) = delete;
  ControlCommandCanOutput & operator=(const ControlCommandCanOutput &) = delete;

  bool init()
  {
    LockGuard lock(mutex_);
    initialized_ = platform::can_init();
    return initialized_;
  }

  bool initialized() const
  {
    LockGuard lock(mutex_);
    return initialized_;
  }

  bool send(const ControlMsg & msg, const ControlCommandOutputMode mode)
  {
    LockGuard lock(mutex_);

    if (!output_mode_uses_can(mode)) {
      return true;
    }

    if (!initialized_) {
      common::logger::log_error("CAN output is not initialized");
      return false;
    }

    const uint16_t sequence = sequence_;
    ++sequence_;
    const auto encoded = encode_control_command(msg, mode, sequence);
    if (!encoded.ok) {
      common::logger::log_error("CAN control command encoding failed: %s", encoded.error);
      return false;
    }

    for (std::size_t index = 0U; index < encoded.count; ++index) {
      if (!platform::can_send(encoded.frames[index])) {
        common::logger::log_error(
          "CAN frame send failed for frame id=0x%03x",
          static_cast<unsigned int>(encoded.frames[index].id));
        return false;
      }
    }

    return true;
  }

private:
  class LockGuard
  {
  public:
    explicit LockGuard(pthread_mutex_t & mutex)
    : mutex_(mutex)
    {
      pthread_mutex_lock(&mutex_);
    }

    ~LockGuard()
    {
      pthread_mutex_unlock(&mutex_);
    }

    LockGuard(const LockGuard &) = delete;
    LockGuard & operator=(const LockGuard &) = delete;

  private:
    pthread_mutex_t & mutex_;
  };

  mutable pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
  bool initialized_{false};
  uint16_t sequence_{0U};
};

}  // namespace common::can

#endif  // COMMON__CAN__CONTROL_COMMAND_CAN_OUTPUT_HPP_
