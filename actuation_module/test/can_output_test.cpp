#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "autoware/autoware_msgs/messages.hpp"
#include "common/can/control_command_can_output.hpp"
#include "common/can/control_command_decoder.hpp"
#include "common/can/control_command_encoder.hpp"
#include "common/can/control_command_output_mode.hpp"
#include "common/logger/logger.hpp"
#include "platform/platform_can.h"

#if defined(PLATFORM_ZEPHYR)
  #include <zephyr/device.h>
  #include <zephyr/drivers/can.h>
  #include <zephyr/kernel.h>
#endif

using common::logger::log_error;
using common::logger::log_info;

#define ASSERT_MSG(condition, message) \
  do { \
    if (!(condition)) { \
      log_error("Assertion failed: %s", message); \
      assert(false && message); \
    } \
  } while (0)

static ControlMsg make_sample_control_msg()
{
  ControlMsg msg{};
  msg.stamp.sec = 12;
  msg.stamp.nanosec = 345000000;
  msg.lateral.stamp = msg.stamp;
  msg.lateral.steering_tire_angle = 0.125F;
  msg.lateral.steering_tire_rotation_rate = -0.5F;
  msg.lateral.is_defined_steering_tire_rotation_rate = true;
  msg.longitudinal.stamp = msg.stamp;
  msg.longitudinal.velocity = 12.25F;
  msg.longitudinal.acceleration = -1.5F;
  msg.longitudinal.jerk = 0.0F;
  msg.longitudinal.is_defined_acceleration = true;
  msg.longitudinal.is_defined_jerk = false;
  return msg;
}

static int32_t read_i32_le(const common::can::CanFrame & frame, const std::size_t offset)
{
  const uint32_t raw =
    static_cast<uint32_t>(frame.data[offset]) |
    (static_cast<uint32_t>(frame.data[offset + 1]) << 8) |
    (static_cast<uint32_t>(frame.data[offset + 2]) << 16) |
    (static_cast<uint32_t>(frame.data[offset + 3]) << 24);
  return static_cast<int32_t>(raw);
}

static uint16_t read_u16_le(const common::can::CanFrame & frame, const std::size_t offset)
{
  return static_cast<uint16_t>(
    static_cast<uint16_t>(frame.data[offset]) |
    (static_cast<uint16_t>(frame.data[offset + 1]) << 8));
}

static uint32_t read_u32_le(const common::can::CanFrame & frame, const std::size_t offset)
{
  return
    static_cast<uint32_t>(frame.data[offset]) |
    (static_cast<uint32_t>(frame.data[offset + 1]) << 8) |
    (static_cast<uint32_t>(frame.data[offset + 2]) << 16) |
    (static_cast<uint32_t>(frame.data[offset + 3]) << 24);
}

static void test_output_mode_helpers()
{
  using common::can::ControlCommandOutputMode;
  ASSERT_MSG(common::can::output_mode_uses_dds(ControlCommandOutputMode::DDS_ONLY), "DDS_ONLY uses DDS");
  ASSERT_MSG(!common::can::output_mode_uses_can(ControlCommandOutputMode::DDS_ONLY), "DDS_ONLY skips CAN");
  ASSERT_MSG(!common::can::output_mode_uses_dds(ControlCommandOutputMode::CAN_ONLY), "CAN_ONLY skips DDS");
  ASSERT_MSG(common::can::output_mode_uses_can(ControlCommandOutputMode::CAN_ONLY), "CAN_ONLY uses CAN");
  ASSERT_MSG(common::can::output_mode_uses_dds(ControlCommandOutputMode::DDS_AND_CAN), "DDS_AND_CAN uses DDS");
  ASSERT_MSG(common::can::output_mode_uses_can(ControlCommandOutputMode::DDS_AND_CAN), "DDS_AND_CAN uses CAN");
}

static void test_encoder_payloads()
{
  using common::can::ControlCommandOutputMode;
  const auto msg = make_sample_control_msg();
  const auto encoded = common::can::encode_control_command(msg, ControlCommandOutputMode::DDS_AND_CAN, 42);

  ASSERT_MSG(encoded.ok, "encoding succeeds");
  ASSERT_MSG(encoded.count == 3, "encoding produces three frames");

  ASSERT_MSG(encoded.frames[0].id == 0x100U, "lateral frame id");
  ASSERT_MSG(encoded.frames[0].dlc == 8U, "lateral frame dlc");
  ASSERT_MSG(read_i32_le(encoded.frames[0], 0) == 125000, "steering angle scale");
  ASSERT_MSG(read_i32_le(encoded.frames[0], 4) == -500000, "steering rate scale");

  ASSERT_MSG(encoded.frames[1].id == 0x101U, "longitudinal frame id");
  ASSERT_MSG(encoded.frames[1].dlc == 8U, "longitudinal frame dlc");
  ASSERT_MSG(read_i32_le(encoded.frames[1], 0) == 12250, "velocity scale");
  ASSERT_MSG(read_i32_le(encoded.frames[1], 4) == -1500, "acceleration scale");

  ASSERT_MSG(encoded.frames[2].id == 0x102U, "status frame id");
  ASSERT_MSG(encoded.frames[2].dlc == 8U, "status frame dlc");
  ASSERT_MSG(encoded.frames[2].data[0] == 2U, "status output mode");
  ASSERT_MSG(encoded.frames[2].data[1] == 0x0BU, "status flags");
  ASSERT_MSG(read_u16_le(encoded.frames[2], 2) == 42U, "status sequence");
  ASSERT_MSG(read_u32_le(encoded.frames[2], 4) == 12345U, "status timestamp milliseconds");
}

static void test_dds_only_encoding_has_no_frames()
{
  using common::can::ControlCommandOutputMode;
  const auto encoded = common::can::encode_control_command(make_sample_control_msg(), ControlCommandOutputMode::DDS_ONLY, 0);
  ASSERT_MSG(encoded.ok, "DDS_ONLY encoding succeeds");
  ASSERT_MSG(encoded.count == 0U, "DDS_ONLY produces no frames");
}

static void test_non_finite_values_are_rejected()
{
  using common::can::ControlCommandOutputMode;
  auto msg = make_sample_control_msg();
  msg.longitudinal.velocity = std::numeric_limits<float>::quiet_NaN();

  const auto encoded = common::can::encode_control_command(msg, ControlCommandOutputMode::CAN_ONLY, 0);
  ASSERT_MSG(!encoded.ok, "non-finite command is rejected");
  ASSERT_MSG(encoded.count == 0U, "rejected command produces no frames");
}

static void assert_frame_equals(
  const common::can::CanFrame & actual,
  const common::can::CanFrame & expected,
  const char * message)
{
  ASSERT_MSG(actual.id == expected.id, message);
  ASSERT_MSG(actual.dlc == expected.dlc, message);
  ASSERT_MSG(actual.extended == expected.extended, message);
  for (std::size_t index = 0U; index < expected.dlc; ++index) {
    ASSERT_MSG(actual.data[index] == expected.data[index], message);
  }
}

#if defined(PLATFORM_FREERTOS_CAN_MOCK)
static void test_freertos_can_output_records_frames()
{
  using common::can::ControlCommandCanOutput;
  using common::can::ControlCommandOutputMode;

  common::can::platform::reset_recorded_can_frames();

  ControlCommandCanOutput output;
  ASSERT_MSG(output.init(), "FreeRTOS CAN mock initializes");
  ASSERT_MSG(output.send(make_sample_control_msg(), ControlCommandOutputMode::DDS_AND_CAN), "first CAN send succeeds");
  ASSERT_MSG(output.send(make_sample_control_msg(), ControlCommandOutputMode::DDS_AND_CAN), "second CAN send succeeds");

  ASSERT_MSG(common::can::platform::recorded_can_frame_count() == 6U, "two sends record six frames");
  ASSERT_MSG(common::can::platform::recorded_can_frame_at(0).id == 0x100U, "first recorded frame id");
  ASSERT_MSG(common::can::platform::recorded_can_frame_at(1).id == 0x101U, "second recorded frame id");
  ASSERT_MSG(common::can::platform::recorded_can_frame_at(2).id == 0x102U, "third recorded frame id");
  ASSERT_MSG(read_u16_le(common::can::platform::recorded_can_frame_at(2), 2) == 0U, "first send sequence");
  ASSERT_MSG(read_u16_le(common::can::platform::recorded_can_frame_at(5), 2) == 1U, "second send sequence");
}

static void test_failed_batch_does_not_advance_sequence()
{
  using common::can::ControlCommandCanOutput;
  using common::can::ControlCommandOutputMode;

  common::can::platform::reset_recorded_can_frames();
  ControlCommandCanOutput output;
  ASSERT_MSG(output.init(), "mock initializes");

  auto nan_msg = make_sample_control_msg();
  nan_msg.longitudinal.velocity = std::numeric_limits<float>::quiet_NaN();
  ASSERT_MSG(!output.send(nan_msg, ControlCommandOutputMode::CAN_ONLY), "encode failure is reported");

  common::can::platform::mock_send_fail_from = 1U;
  ASSERT_MSG(!output.send(make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY), "mid-batch failure is reported");
  ASSERT_MSG(common::can::platform::recorded_can_frame_count() == 1U, "failed batch recorded the first frame only");

  common::can::platform::mock_send_fail_from = std::numeric_limits<std::size_t>::max();
  ASSERT_MSG(output.send(make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY), "retry after failure succeeds");
  ASSERT_MSG(common::can::platform::recorded_can_frame_count() == 4U, "retry records three more frames");
  ASSERT_MSG(read_u16_le(common::can::platform::recorded_can_frame_at(3), 2) == 0U, "sequence is unchanged after failed batch");
}
#endif

static void test_decoder_round_trip_and_watchdog()
{
  using common::can::ControlCommandDecoder;
  using common::can::ControlCommandOutputMode;
  using common::can::DecoderEvent;

  const auto encoded = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 7U);
  ASSERT_MSG(encoded.ok, "sample command encodes");

  ControlCommandDecoder decoder;
  ASSERT_MSG(decoder.feed(encoded.frames[0], 0.0) == DecoderEvent::Stored, "lateral stored");
  ASSERT_MSG(decoder.feed(encoded.frames[1], 0.0) == DecoderEvent::Stored, "longitudinal stored");
  ASSERT_MSG(decoder.feed(encoded.frames[2], 0.0) == DecoderEvent::Accepted, "status commits");
  ASSERT_MSG(decoder.has_command(), "decoder has a command");
  ASSERT_MSG(decoder.command().sequence == 7U, "decoded sequence");
  ASSERT_MSG(std::fabs(decoder.command().steering_tire_angle - 0.125F) < 1e-6F, "decoded steer");
  ASSERT_MSG(std::fabs(decoder.command().velocity - 12.25F) < 1e-4F, "decoded velocity");

  common::can::CanFrame unknown{};
  unknown.id = 0x200U;
  unknown.dlc = 8U;
  ASSERT_MSG(decoder.feed(unknown, 0.1) == DecoderEvent::Ignored, "unknown id ignored");

  const auto next = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 9U);
  ASSERT_MSG(decoder.feed(next.frames[0], 0.2) == DecoderEvent::Stored, "gap lateral stored");
  ASSERT_MSG(decoder.feed(next.frames[1], 0.2) == DecoderEvent::Stored, "gap longitudinal stored");
  ASSERT_MSG(decoder.feed(next.frames[2], 0.2) == DecoderEvent::Rejected, "sequence gap rejected");

  const auto contiguous = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 8U);
  ASSERT_MSG(decoder.feed(contiguous.frames[0], 0.3) == DecoderEvent::Stored, "contiguous lateral stored");
  ASSERT_MSG(decoder.feed(contiguous.frames[1], 0.3) == DecoderEvent::Stored, "contiguous longitudinal stored");
  ASSERT_MSG(decoder.feed(contiguous.frames[2], 0.3) == DecoderEvent::Accepted, "contiguous sequence accepted");

  ASSERT_MSG(decoder.poll_watchdog(0.7, 0.5) == DecoderEvent::Ignored, "watchdog quiet inside timeout");
  ASSERT_MSG(decoder.poll_watchdog(0.81, 0.5) == DecoderEvent::SafeStop, "watchdog fires");
  ASSERT_MSG(decoder.in_safe_stop(), "decoder is in safe stop");

  const auto after_stop = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 40U);
  ASSERT_MSG(decoder.feed(after_stop.frames[0], 1.0) == DecoderEvent::Stored, "rebaseline lateral");
  ASSERT_MSG(decoder.feed(after_stop.frames[1], 1.0) == DecoderEvent::Stored, "rebaseline longitudinal");
  ASSERT_MSG(decoder.feed(after_stop.frames[2], 1.0) == DecoderEvent::Accepted, "rebaseline accepts any sequence");
}

static void test_decoder_sequence_wrap()
{
  using common::can::ControlCommandDecoder;
  using common::can::ControlCommandOutputMode;
  using common::can::DecoderEvent;

  ControlCommandDecoder decoder;
  const auto last = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 65535U);
  ASSERT_MSG(decoder.feed(last.frames[0], 0.0) == DecoderEvent::Stored, "wrap start lateral");
  ASSERT_MSG(decoder.feed(last.frames[1], 0.0) == DecoderEvent::Stored, "wrap start longitudinal");
  ASSERT_MSG(decoder.feed(last.frames[2], 0.0) == DecoderEvent::Accepted, "wrap start accepted");

  const auto wrapped = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 0U);
  ASSERT_MSG(decoder.feed(wrapped.frames[0], 0.1) == DecoderEvent::Stored, "wrap lateral");
  ASSERT_MSG(decoder.feed(wrapped.frames[1], 0.1) == DecoderEvent::Stored, "wrap longitudinal");
  ASSERT_MSG(decoder.feed(wrapped.frames[2], 0.1) == DecoderEvent::Accepted, "65535 wraps to 0");
}

static void test_decoder_frame_assembly()
{
  using common::can::ControlCommandDecoder;
  using common::can::ControlCommandOutputMode;
  using common::can::DecoderEvent;

  const auto encoded = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 0U);
  ASSERT_MSG(encoded.ok, "sample encodes");

  ControlCommandDecoder reorder;
  ASSERT_MSG(reorder.feed(encoded.frames[1], 0.0) == DecoderEvent::Stored, "longitudinal first");
  ASSERT_MSG(reorder.feed(encoded.frames[0], 0.0) == DecoderEvent::Stored, "lateral second");
  ASSERT_MSG(reorder.feed(encoded.frames[2], 0.0) == DecoderEvent::Accepted, "reordered commit");

  ControlCommandDecoder duplicate_decoder;
  common::can::CanFrame duplicate = encoded.frames[0];
  duplicate.data[0] = static_cast<uint8_t>(duplicate.data[0] + 1U);
  ASSERT_MSG(duplicate_decoder.feed(encoded.frames[0], 0.0) == DecoderEvent::Stored, "first lateral");
  ASSERT_MSG(duplicate_decoder.feed(duplicate, 0.0) == DecoderEvent::Stored, "duplicate lateral replaces");
  ASSERT_MSG(duplicate_decoder.feed(encoded.frames[1], 0.0) == DecoderEvent::Stored, "longitudinal");
  ASSERT_MSG(duplicate_decoder.feed(encoded.frames[2], 0.0) == DecoderEvent::Accepted, "commit after duplicate");

  common::can::CanFrame bad = encoded.frames[0];
  bad.dlc = 7U;
  ControlCommandDecoder dlc_decoder;
  ASSERT_MSG(dlc_decoder.feed(bad, 0.0) == DecoderEvent::Ignored, "bad DLC ignored");
  ASSERT_MSG(dlc_decoder.feed(encoded.frames[1], 0.0) == DecoderEvent::Stored, "longitudinal stored");
  ASSERT_MSG(dlc_decoder.feed(encoded.frames[2], 0.0) == DecoderEvent::Rejected, "commit without lateral");

  common::can::CanFrame extended = encoded.frames[0];
  extended.extended = true;
  ControlCommandDecoder ext_decoder;
  ASSERT_MSG(ext_decoder.feed(extended, 0.0) == DecoderEvent::Ignored, "extended ignored");
  ASSERT_MSG(ext_decoder.feed(encoded.frames[1], 0.0) == DecoderEvent::Stored, "longitudinal only");
  ASSERT_MSG(ext_decoder.feed(encoded.frames[2], 0.0) == DecoderEvent::Rejected, "commit without valid lateral");
}

#if defined(PLATFORM_ZEPHYR) && defined(CONFIG_CONTROL_CMD_CAN_OUTPUT) && CONFIG_CONTROL_CMD_CAN_OUTPUT
CAN_MSGQ_DEFINE(zephyr_can_rx_msgq, 8);

static common::can::CanFrame to_common_can_frame(const struct can_frame & frame)
{
  common::can::CanFrame out{};
  out.id = frame.id;
  out.dlc = frame.dlc;
  out.extended = (frame.flags & CAN_FRAME_IDE) != 0U;
  for (std::size_t index = 0U; index < frame.dlc; ++index) {
    out.data[index] = frame.data[index];
  }
  return out;
}

static void add_zephyr_can_filter(const uint32_t id)
{
  struct can_filter filter{};
  filter.id = id;
  filter.mask = CAN_STD_ID_MASK;
  filter.flags = CAN_FILTER_DATA;

  const int filter_id = ::can_add_rx_filter_msgq(
    common::can::platform::can_device(), &zephyr_can_rx_msgq, &filter);
  ASSERT_MSG(filter_id >= 0, "Zephyr CAN filter registration succeeds");
}

static void test_zephyr_can_output_loopback()
{
  using common::can::ControlCommandCanOutput;
  using common::can::ControlCommandOutputMode;

  const struct device * device = common::can::platform::can_device();
  ASSERT_MSG(device_is_ready(device), "Zephyr CAN device is ready");
  ASSERT_MSG(::can_set_mode(device, CAN_MODE_LOOPBACK) == 0, "Zephyr CAN loopback mode is set");

  add_zephyr_can_filter(common::can::kLateralCommandCanId);
  add_zephyr_can_filter(common::can::kLongitudinalCommandCanId);
  add_zephyr_can_filter(common::can::kCommandStatusCanId);

  ControlCommandCanOutput output;
  ASSERT_MSG(output.init(), "Zephyr CAN output initializes");
  ASSERT_MSG(output.send(make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY), "first Zephyr CAN send succeeds");
  ASSERT_MSG(output.send(make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY), "second Zephyr CAN send succeeds");

  const auto expected_first = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 0U);
  const auto expected_second = common::can::encode_control_command(
    make_sample_control_msg(), ControlCommandOutputMode::CAN_ONLY, 1U);
  ASSERT_MSG(expected_first.ok, "first expected frame set encodes");
  ASSERT_MSG(expected_second.ok, "second expected frame set encodes");

  struct can_frame received{};
  for (std::size_t index = 0U; index < expected_first.count; ++index) {
    ASSERT_MSG(k_msgq_get(&zephyr_can_rx_msgq, &received, K_MSEC(500)) == 0, "first Zephyr CAN frame is received");
    assert_frame_equals(to_common_can_frame(received), expected_first.frames[index], "first Zephyr CAN frame matches");
  }

  for (std::size_t index = 0U; index < expected_second.count; ++index) {
    ASSERT_MSG(k_msgq_get(&zephyr_can_rx_msgq, &received, K_MSEC(500)) == 0, "second Zephyr CAN frame is received");
    assert_frame_equals(to_common_can_frame(received), expected_second.frames[index], "second Zephyr CAN frame matches");
  }
}
#endif

int main()
{
  log_info("=== Starting CAN output tests ===");
  test_output_mode_helpers();
  test_encoder_payloads();
  test_dds_only_encoding_has_no_frames();
  test_non_finite_values_are_rejected();
  test_decoder_round_trip_and_watchdog();
  test_decoder_sequence_wrap();
  test_decoder_frame_assembly();
#if defined(PLATFORM_FREERTOS_CAN_MOCK)
  test_freertos_can_output_records_frames();
  test_failed_batch_does_not_advance_sequence();
#elif defined(PLATFORM_ZEPHYR)
  #if defined(CONFIG_CONTROL_CMD_CAN_OUTPUT) && CONFIG_CONTROL_CMD_CAN_OUTPUT
  test_zephyr_can_output_loopback();
  #else
  log_error("Zephyr CAN output test requires CONFIG_CONTROL_CMD_CAN_OUTPUT");
  return 1;
  #endif
#endif
  log_info("CAN output tests passed");
#if defined(PLATFORM_FREERTOS)
  std::exit(0);
#else
  return 0;
#endif
}
