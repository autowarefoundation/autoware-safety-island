#ifndef COMMON__CAN__CONTROL_COMMAND_ENCODER_HPP_
#define COMMON__CAN__CONTROL_COMMAND_ENCODER_HPP_

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "autoware/autoware_msgs/messages.hpp"
#include "common/can/can_frame.hpp"
#include "common/can/control_command_output_mode.hpp"

namespace common::can
{

constexpr std::size_t kControlCommandCanFrameCount = 3U;
constexpr uint32_t kLateralCommandCanId = 0x100U;
constexpr uint32_t kLongitudinalCommandCanId = 0x101U;
constexpr uint32_t kCommandStatusCanId = 0x102U;

struct EncodedControlCommand
{
  bool ok{false};
  const char * error{"not encoded"};
  std::array<CanFrame, kControlCommandCanFrameCount> frames{};
  std::size_t count{0U};
};

namespace detail
{

inline void write_i32_le(CanFrame & frame, const std::size_t offset, const int32_t value)
{
  const uint32_t raw = static_cast<uint32_t>(value);
  frame.data[offset] = static_cast<uint8_t>(raw & 0xFFU);
  frame.data[offset + 1U] = static_cast<uint8_t>((raw >> 8U) & 0xFFU);
  frame.data[offset + 2U] = static_cast<uint8_t>((raw >> 16U) & 0xFFU);
  frame.data[offset + 3U] = static_cast<uint8_t>((raw >> 24U) & 0xFFU);
}

inline void write_u16_le(CanFrame & frame, const std::size_t offset, const uint16_t value)
{
  frame.data[offset] = static_cast<uint8_t>(value & 0xFFU);
  frame.data[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline void write_u32_le(CanFrame & frame, const std::size_t offset, const uint32_t value)
{
  frame.data[offset] = static_cast<uint8_t>(value & 0xFFU);
  frame.data[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  frame.data[offset + 2U] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  frame.data[offset + 3U] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

inline bool scale_to_i32(const float value, const double scale, int32_t & output)
{
  if (!std::isfinite(value)) {
    return false;
  }

  const double scaled = static_cast<double>(value) * scale;
  constexpr double min_i32 = static_cast<double>(std::numeric_limits<int32_t>::min());
  constexpr double max_i32 = static_cast<double>(std::numeric_limits<int32_t>::max());
  if (scaled < min_i32 - 0.5 || scaled > max_i32 + 0.5) {
    return false;
  }

  const int64_t rounded = static_cast<int64_t>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
  if (rounded < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
      rounded > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }

  output = static_cast<int32_t>(rounded);
  return true;
}

inline uint32_t timestamp_ms_modulo(const builtin_interfaces_msg_Time & stamp)
{
  const uint64_t sec_ms = static_cast<uint64_t>(stamp.sec) * 1000ULL;
  const uint64_t nsec_ms = static_cast<uint64_t>(stamp.nanosec) / 1000000ULL;
  return static_cast<uint32_t>(sec_ms + nsec_ms);
}

}  // namespace detail

inline EncodedControlCommand encode_control_command(
  const ControlMsg & msg,
  const ControlCommandOutputMode mode,
  const uint16_t sequence)
{
  EncodedControlCommand encoded{};

  if (!output_mode_uses_can(mode)) {
    encoded.ok = true;
    encoded.error = nullptr;
    encoded.count = 0U;
    return encoded;
  }

  int32_t steering_angle{0};
  int32_t steering_rate{0};
  int32_t velocity{0};
  int32_t acceleration{0};

  if (!detail::scale_to_i32(msg.lateral.steering_tire_angle, 1000000.0, steering_angle)) {
    encoded.error = "invalid steering_tire_angle";
    return encoded;
  }
  if (!detail::scale_to_i32(msg.lateral.steering_tire_rotation_rate, 1000000.0, steering_rate)) {
    encoded.error = "invalid steering_tire_rotation_rate";
    return encoded;
  }
  if (!detail::scale_to_i32(msg.longitudinal.velocity, 1000.0, velocity)) {
    encoded.error = "invalid velocity";
    return encoded;
  }
  if (!detail::scale_to_i32(msg.longitudinal.acceleration, 1000.0, acceleration)) {
    encoded.error = "invalid acceleration";
    return encoded;
  }

  CanFrame lateral{};
  lateral.id = kLateralCommandCanId;
  lateral.dlc = 8U;
  detail::write_i32_le(lateral, 0U, steering_angle);
  detail::write_i32_le(lateral, 4U, steering_rate);

  CanFrame longitudinal{};
  longitudinal.id = kLongitudinalCommandCanId;
  longitudinal.dlc = 8U;
  detail::write_i32_le(longitudinal, 0U, velocity);
  detail::write_i32_le(longitudinal, 4U, acceleration);

  uint8_t validity_flags = 0U;
  if (msg.lateral.is_defined_steering_tire_rotation_rate) {
    validity_flags |= 0x01U;
  }
  if (msg.longitudinal.is_defined_acceleration) {
    validity_flags |= 0x02U;
  }
  if (msg.longitudinal.is_defined_jerk) {
    validity_flags |= 0x04U;
  }
  validity_flags |= 0x08U;

  CanFrame status{};
  status.id = kCommandStatusCanId;
  status.dlc = 8U;
  status.data[0] = static_cast<uint8_t>(mode);
  status.data[1] = validity_flags;
  detail::write_u16_le(status, 2U, sequence);
  detail::write_u32_le(status, 4U, detail::timestamp_ms_modulo(msg.stamp));

  encoded.frames[0] = lateral;
  encoded.frames[1] = longitudinal;
  encoded.frames[2] = status;
  encoded.count = kControlCommandCanFrameCount;
  encoded.ok = true;
  encoded.error = nullptr;
  return encoded;
}

}  // namespace common::can

#endif  // COMMON__CAN__CONTROL_COMMAND_ENCODER_HPP_
