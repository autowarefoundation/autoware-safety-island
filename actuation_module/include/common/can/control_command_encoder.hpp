#ifndef COMMON__CAN__CONTROL_COMMAND_ENCODER_HPP_
#define COMMON__CAN__CONTROL_COMMAND_ENCODER_HPP_

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "autoware/autoware_msgs/messages.hpp"
#include "common/can/can_fd_frame.hpp"
#include "common/can/can_frame.hpp"
#include "common/can/control_command_output_mode.hpp"

namespace common::can
{

constexpr std::size_t kControlCommandCanFrameCount = 3U;
constexpr uint32_t kLateralCommandCanId = 0x100U;
constexpr uint32_t kLongitudinalCommandCanId = 0x101U;
constexpr uint32_t kCommandStatusCanId = 0x102U;
constexpr uint32_t kFdControlCommandCanId = 0x103U;
constexpr std::size_t kControlCommandFdDataLength =
  kControlCommandCanFrameCount * kCanMaxDataLength;

static_assert(
  kControlCommandFdDataLength <= kCanFdMaxDataLength,
  "CAN-FD control command payload must fit the CAN-FD data limit");

// Supervisor decision carried in the status frame. The values match
// supervision::Decision; the CAN layer keeps its own copy so it does not
// depend on the controller node.
constexpr uint8_t kCanDecisionNormal = 0U;
constexpr uint8_t kCanDecisionSiStop = 1U;
constexpr uint8_t kCanDecisionHold = 2U;

// Status byte 0: output mode in bits 0-1, supervisor decision in bits 4-5.
constexpr uint8_t kStatusModeMask = 0x03U;
constexpr uint8_t kStatusDecisionShift = 4U;
constexpr uint8_t kStatusDecisionMask = 0x03U;

// Status byte 1 (flags).
constexpr uint8_t kStatusFlagSteeringRateDefined = 0x01U;
constexpr uint8_t kStatusFlagAccelerationDefined = 0x02U;
constexpr uint8_t kStatusFlagJerkDefined = 0x04U;
constexpr uint8_t kStatusFlagCommandValid = 0x08U;
// Set when bytes 0 and 4-7 carry the decision, fault id and CRC below. A
// receiver must reject a status frame without it: the earlier layout put a
// 32-bit timestamp in bytes 4-7 and carried no integrity check.
constexpr uint8_t kStatusFlagSupervised = 0x10U;

// What the supervisor decided for this command, as the receiver needs it.
struct CommandSupervisionStatus
{
  uint8_t decision{kCanDecisionNormal};
  // Fault event behind an SI_STOP; 0 when no fault is latched.
  uint32_t fault_id{0U};
};

struct EncodedControlCommand
{
  bool ok{false};
  const char * error{"not encoded"};
  std::array<CanFrame, kControlCommandCanFrameCount> frames{};
  std::size_t count{0U};
};

struct EncodedControlCommandFd
{
  bool ok{false};
  const char * error{"not encoded"};
  CanFdFrame frame{};
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

// Producer timestamp in milliseconds, modulo 2^16 (wraps every 65.536 s).
// It is producer metadata only; receivers time out on their own clock.
inline uint16_t timestamp_ms_modulo(const builtin_interfaces_msg_Time & stamp)
{
  const uint64_t sec_ms = static_cast<uint64_t>(stamp.sec) * 1000ULL;
  const uint64_t nsec_ms = static_cast<uint64_t>(stamp.nanosec) / 1000000ULL;
  return static_cast<uint16_t>((sec_ms + nsec_ms) & 0xFFFFULL);
}

// One byte for the fault id: 0 means "no fault latched", so a latched fault
// maps into 1..255 and never collides with it.
inline uint8_t fault_id_byte(const uint32_t fault_id)
{
  return fault_id == 0U ? 0U : static_cast<uint8_t>(((fault_id - 1U) % 255U) + 1U);
}

// CRC-8 SAE J1850: polynomial 0x1D, initial value 0xFF, final XOR 0xFF.
// This is the running update, so several buffers can be chained; the caller
// applies the final XOR. The check value for "123456789" is 0x4B.
inline uint8_t crc8_j1850_update(const uint8_t * data, const std::size_t size, uint8_t crc = 0xFFU)
{
  for (std::size_t index = 0U; index < size; ++index) {
    crc = static_cast<uint8_t>(crc ^ data[index]);
    for (int bit = 0; bit < 8; ++bit) {
      crc = static_cast<uint8_t>((crc & 0x80U) != 0U ? (crc << 1U) ^ 0x1DU : (crc << 1U));
    }
  }
  return crc;
}

// CRC of one command: the lateral payload, the longitudinal payload and the
// first seven status bytes, in that order. Covering all three frames lets the
// receiver detect a command assembled from frames of different cycles.
inline uint8_t command_crc(
  const CanFrame & lateral, const CanFrame & longitudinal, const CanFrame & status)
{
  uint8_t crc = crc8_j1850_update(lateral.data.data(), kCanMaxDataLength);
  crc = crc8_j1850_update(longitudinal.data.data(), kCanMaxDataLength, crc);
  crc = crc8_j1850_update(status.data.data(), kCanMaxDataLength - 1U, crc);
  return static_cast<uint8_t>(crc ^ 0xFFU);
}

}  // namespace detail

inline EncodedControlCommand encode_control_command(
  const ControlMsg & msg,
  const ControlCommandOutputMode mode,
  const uint16_t sequence,
  const CommandSupervisionStatus & supervision = {})
{
  EncodedControlCommand encoded{};

  if (!output_mode_uses_can(mode)) {
    encoded.ok = true;
    encoded.error = nullptr;
    encoded.count = 0U;
    return encoded;
  }

  if (supervision.decision > kCanDecisionHold) {
    encoded.error = "invalid supervisor decision";
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

  uint8_t flags = kStatusFlagCommandValid | kStatusFlagSupervised;
  if (msg.lateral.is_defined_steering_tire_rotation_rate) {
    flags |= kStatusFlagSteeringRateDefined;
  }
  if (msg.longitudinal.is_defined_acceleration) {
    flags |= kStatusFlagAccelerationDefined;
  }
  if (msg.longitudinal.is_defined_jerk) {
    flags |= kStatusFlagJerkDefined;
  }

  CanFrame status{};
  status.id = kCommandStatusCanId;
  status.dlc = 8U;
  status.data[0] = static_cast<uint8_t>(
    (static_cast<uint8_t>(mode) & kStatusModeMask) |
    ((supervision.decision & kStatusDecisionMask) << kStatusDecisionShift));
  status.data[1] = flags;
  detail::write_u16_le(status, 2U, sequence);
  detail::write_u16_le(status, 4U, detail::timestamp_ms_modulo(msg.stamp));
  status.data[6] = detail::fault_id_byte(supervision.fault_id);
  status.data[7] = detail::command_crc(lateral, longitudinal, status);

  encoded.frames[0] = lateral;
  encoded.frames[1] = longitudinal;
  encoded.frames[2] = status;
  encoded.count = kControlCommandCanFrameCount;
  encoded.ok = true;
  encoded.error = nullptr;
  return encoded;
}

// CAN-FD packing reuses the classic payload bytes one-to-one so both formats
// share the same scaling, sequence, and status contract.
inline EncodedControlCommandFd encode_control_command_fd(
  const ControlMsg & msg,
  const ControlCommandOutputMode mode,
  const uint16_t sequence,
  const CommandSupervisionStatus & supervision = {})
{
  EncodedControlCommandFd encoded{};

  const auto classic = encode_control_command(msg, mode, sequence, supervision);
  if (!classic.ok) {
    encoded.error = classic.error;
    return encoded;
  }
  if (classic.count == 0U) {
    encoded.ok = true;
    encoded.error = nullptr;
    return encoded;
  }

  for (std::size_t index = 0U; index < classic.count; ++index) {
    if (classic.frames[index].dlc != kCanMaxDataLength || classic.frames[index].extended) {
      encoded.error = "classic frames are not CAN-FD packable";
      return encoded;
    }
  }

  CanFdFrame frame{};
  frame.id = kFdControlCommandCanId;
  frame.length = static_cast<uint8_t>(kControlCommandFdDataLength);
  frame.brs = false;
  for (std::size_t index = 0U; index < classic.count; ++index) {
    std::memcpy(
      frame.data.data() + index * kCanMaxDataLength,
      classic.frames[index].data.data(),
      kCanMaxDataLength);
  }

  encoded.frame = frame;
  encoded.count = 1U;
  encoded.ok = true;
  encoded.error = nullptr;
  return encoded;
}

}  // namespace common::can

#endif  // COMMON__CAN__CONTROL_COMMAND_ENCODER_HPP_
