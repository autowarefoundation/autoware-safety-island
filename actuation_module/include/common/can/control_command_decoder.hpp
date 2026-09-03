#ifndef COMMON__CAN__CONTROL_COMMAND_DECODER_HPP_
#define COMMON__CAN__CONTROL_COMMAND_DECODER_HPP_

#include <cstddef>
#include <cstdint>

#include "common/can/can_frame.hpp"
#include "common/can/control_command_encoder.hpp"

namespace common::can
{

enum class DecoderEvent : uint8_t
{
  Ignored = 0U,
  Stored = 1U,
  Accepted = 2U,
  Rejected = 3U,
  SafeStop = 4U,
};

struct DecodedControlCommand
{
  float steering_tire_angle{0.0F};
  float steering_tire_rotation_rate{0.0F};
  bool steering_rate_defined{false};
  float velocity{0.0F};
  float acceleration{0.0F};
  bool acceleration_defined{false};
  bool jerk_defined{false};
  uint16_t sequence{0U};
  uint8_t output_mode{0U};
};

class ControlCommandDecoder
{
public:
  DecoderEvent feed(const CanFrame & frame, const double now_monotonic_sec)
  {
    note_time(now_monotonic_sec);

    if (frame.extended || frame.dlc != 8U) {
      return DecoderEvent::Ignored;
    }

    if (frame.id == kLateralCommandCanId) {
      pending_lateral_ = frame;
      has_lateral_ = true;
      return DecoderEvent::Stored;
    }
    if (frame.id == kLongitudinalCommandCanId) {
      pending_longitudinal_ = frame;
      has_longitudinal_ = true;
      return DecoderEvent::Stored;
    }
    if (frame.id != kCommandStatusCanId) {
      return DecoderEvent::Ignored;
    }

    if (!has_lateral_ || !has_longitudinal_) {
      clear_pending();
      return DecoderEvent::Rejected;
    }

    const uint16_t sequence = read_u16_le(frame, 2U);
    if (has_expected_sequence_ && sequence != expected_sequence_) {
      clear_pending();
      return DecoderEvent::Rejected;
    }

    command_ = decode_cycle(pending_lateral_, pending_longitudinal_, frame, sequence);
    has_command_ = true;
    in_safe_stop_ = false;
    has_expected_sequence_ = true;
    expected_sequence_ = static_cast<uint16_t>(sequence + 1U);
    anchor_time_ = now_monotonic_sec;
    has_anchor_time_ = true;
    clear_pending();
    return DecoderEvent::Accepted;
  }

  DecoderEvent poll_watchdog(const double now_monotonic_sec, const double timeout_sec)
  {
    note_time(now_monotonic_sec);
    if (in_safe_stop_) {
      return DecoderEvent::SafeStop;
    }
    if (!has_anchor_time_) {
      return DecoderEvent::Ignored;
    }
    if ((now_monotonic_sec - anchor_time_) <= timeout_sec) {
      return DecoderEvent::Ignored;
    }

    in_safe_stop_ = true;
    has_expected_sequence_ = false;
    clear_pending();
    return DecoderEvent::SafeStop;
  }

  bool has_command() const
  {
    return has_command_;
  }

  const DecodedControlCommand & command() const
  {
    return command_;
  }

  bool in_safe_stop() const
  {
    return in_safe_stop_;
  }

private:
  static int32_t read_i32_le(const CanFrame & frame, const std::size_t offset)
  {
    const uint32_t raw =
      static_cast<uint32_t>(frame.data[offset]) |
      (static_cast<uint32_t>(frame.data[offset + 1U]) << 8U) |
      (static_cast<uint32_t>(frame.data[offset + 2U]) << 16U) |
      (static_cast<uint32_t>(frame.data[offset + 3U]) << 24U);
    return static_cast<int32_t>(raw);
  }

  static uint16_t read_u16_le(const CanFrame & frame, const std::size_t offset)
  {
    return static_cast<uint16_t>(
      static_cast<uint16_t>(frame.data[offset]) |
      (static_cast<uint16_t>(frame.data[offset + 1U]) << 8U));
  }

  static DecodedControlCommand decode_cycle(
    const CanFrame & lateral,
    const CanFrame & longitudinal,
    const CanFrame & status,
    const uint16_t sequence)
  {
    DecodedControlCommand decoded{};
    decoded.steering_tire_angle =
      static_cast<float>(static_cast<double>(read_i32_le(lateral, 0U)) / 1000000.0);
    decoded.steering_tire_rotation_rate =
      static_cast<float>(static_cast<double>(read_i32_le(lateral, 4U)) / 1000000.0);
    decoded.velocity =
      static_cast<float>(static_cast<double>(read_i32_le(longitudinal, 0U)) / 1000.0);
    decoded.acceleration =
      static_cast<float>(static_cast<double>(read_i32_le(longitudinal, 4U)) / 1000.0);
    decoded.output_mode = status.data[0];
    decoded.steering_rate_defined = (status.data[1] & 0x01U) != 0U;
    decoded.acceleration_defined = (status.data[1] & 0x02U) != 0U;
    decoded.jerk_defined = (status.data[1] & 0x04U) != 0U;
    decoded.sequence = sequence;
    return decoded;
  }

  void note_time(const double now_monotonic_sec)
  {
    if (!has_anchor_time_) {
      anchor_time_ = now_monotonic_sec;
      has_anchor_time_ = true;
    }
  }

  void clear_pending()
  {
    has_lateral_ = false;
    has_longitudinal_ = false;
    pending_lateral_ = {};
    pending_longitudinal_ = {};
  }

  bool has_lateral_{false};
  bool has_longitudinal_{false};
  CanFrame pending_lateral_{};
  CanFrame pending_longitudinal_{};
  DecodedControlCommand command_{};
  bool has_command_{false};
  bool in_safe_stop_{false};
  bool has_expected_sequence_{false};
  uint16_t expected_sequence_{0U};
  bool has_anchor_time_{false};
  double anchor_time_{0.0};
};

}  // namespace common::can

#endif  // COMMON__CAN__CONTROL_COMMAND_DECODER_HPP_
