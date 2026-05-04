#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "autoware/autoware_msgs/messages.hpp"
#include "common/can/control_command_can_output.hpp"
#include "common/can/control_command_encoder.hpp"
#include "common/can/control_command_output_mode.hpp"
#include "common/logger/logger.hpp"
#include "platform/platform_can.h"

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

#if defined(PLATFORM_FREERTOS)
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
#endif

int main()
{
  log_info("=== Starting CAN output tests ===");
  test_output_mode_helpers();
  test_encoder_payloads();
  test_dds_only_encoding_has_no_frames();
  test_non_finite_values_are_rejected();
#if defined(PLATFORM_FREERTOS)
  test_freertos_can_output_records_frames();
#endif
  log_info("CAN output tests passed");
#if defined(PLATFORM_FREERTOS)
  std::exit(0);
#else
  return 0;
#endif
}
