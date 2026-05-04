#ifndef PLATFORM__ZEPHYR__CAN_H_
#define PLATFORM__ZEPHYR__CAN_H_

#include <atomic>
#include <cstring>
#include <errno.h>

#include "common/can/can_frame.hpp"
#include "common/logger/logger.hpp"
#include "platform/platform_config.h"

#if defined(CONFIG_CONTROL_CMD_CAN_OUTPUT) && CONFIG_CONTROL_CMD_CAN_OUTPUT
  #include <zephyr/device.h>
  #include <zephyr/devicetree.h>
  #include <zephyr/drivers/can.h>
  #include <zephyr/kernel.h>

  #if !DT_HAS_CHOSEN(zephyr_canbus)
    #error "CAN output selected but no chosen zephyr,canbus node is configured"
  #endif
#endif

namespace common::can::platform
{

#if defined(CONFIG_CONTROL_CMD_CAN_OUTPUT) && CONFIG_CONTROL_CMD_CAN_OUTPUT

inline const struct device * can_device()
{
  static const struct device * device = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
  return device;
}

inline bool can_init()
{
  const struct device * device = can_device();
  if (!device_is_ready(device)) {
    common::logger::log_error("Zephyr CAN device is not ready");
    return false;
  }

  const int ret = ::can_start(device);
  if (ret != 0 && ret != -EALREADY) {
    common::logger::log_error("Zephyr CAN start failed: %d", ret);
    return false;
  }

  common::logger::log_info("Zephyr CAN initialized");
  return true;
}

enum class CanSendState : uint8_t
{
  Idle = 0U,
  Waiting = 1U,
  TimedOut = 2U,
};

struct CanSendContext
{
  struct k_sem done;
  std::atomic<CanSendState> state{CanSendState::Idle};
  int error{0};
};

inline CanSendContext & can_send_context()
{
  static CanSendContext context{};
  return context;
}

inline void can_tx_done_callback(const struct device *, const int error, void * user_data)
{
  auto * context = static_cast<CanSendContext *>(user_data);
  context->error = error;
  k_sem_give(&context->done);
}

inline bool can_send(const CanFrame & frame)
{
  if (frame.dlc > kCanMaxDataLength || frame.dlc > CAN_MAX_DLC) {
    common::logger::log_error("Zephyr CAN invalid DLC: %u", static_cast<unsigned int>(frame.dlc));
    return false;
  }

  if (frame.extended) {
    if (frame.id > CAN_MAX_EXT_ID) {
      common::logger::log_error(
        "Zephyr CAN extended id out of range: 0x%08x", static_cast<unsigned int>(frame.id));
      return false;
    }
  } else if (frame.id > CAN_MAX_STD_ID) {
    common::logger::log_error(
      "Zephyr CAN standard id out of range: 0x%08x", static_cast<unsigned int>(frame.id));
    return false;
  }

  struct can_frame zephyr_frame{};
  zephyr_frame.id = frame.id;
  zephyr_frame.dlc = frame.dlc;
  zephyr_frame.flags = frame.extended ? CAN_FRAME_IDE : 0U;
  std::memcpy(zephyr_frame.data, frame.data.data(), frame.dlc);

  auto & context = can_send_context();
  if (context.state.load(std::memory_order_acquire) == CanSendState::TimedOut) {
    if (k_sem_take(&context.done, K_NO_WAIT) == 0) {
      context.state.store(CanSendState::Idle, std::memory_order_release);
    } else {
      common::logger::log_error("Zephyr CAN previous send is still pending");
      return false;
    }
  }

  CanSendState expected_state = CanSendState::Idle;
  if (!context.state.compare_exchange_strong(
      expected_state, CanSendState::Waiting, std::memory_order_acq_rel)) {
    common::logger::log_error("Zephyr CAN previous send is still pending");
    return false;
  }

  context.error = 0;
  k_sem_init(&context.done, 0, 1);

  const int ret = ::can_send(can_device(), &zephyr_frame, K_MSEC(10), can_tx_done_callback, &context);
  if (ret != 0) {
    context.state.store(CanSendState::Idle, std::memory_order_release);
    common::logger::log_error("Zephyr CAN send failed for id=0x%03x: %d", static_cast<unsigned int>(frame.id), ret);
    return false;
  }

  const int wait_ret = k_sem_take(&context.done, K_MSEC(10));
  if (wait_ret != 0) {
    context.state.store(CanSendState::TimedOut, std::memory_order_release);
    common::logger::log_error(
      "Zephyr CAN send timed out for id=0x%03x", static_cast<unsigned int>(frame.id));
    return false;
  }

  const int tx_error = context.error;
  context.state.store(CanSendState::Idle, std::memory_order_release);
  if (tx_error != 0) {
    common::logger::log_error(
      "Zephyr CAN send completion failed for id=0x%03x: %d",
      static_cast<unsigned int>(frame.id), tx_error);
    return false;
  }

  return true;
}

#else

inline bool can_init()
{
  return false;
}

inline bool can_send(const CanFrame &)
{
  return false;
}

#endif

}  // namespace common::can::platform

#endif  // PLATFORM__ZEPHYR__CAN_H_
