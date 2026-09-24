#include <cassert>
#include <cstdlib>
#include <cstring>

#include "autoware/autoware_msgs/messages.hpp"
#include "common/can/control_command_can_output.hpp"
#include "common/logger/logger.hpp"

#define ASSERT_MSG(condition, message) \
  do { \
    if (!(condition)) { \
      common::logger::log_error("Assertion failed: %s", message); \
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

// Fail-closed check for an interface below the CAN-FD MTU (run with the vcan
// MTU lowered by run-vcan-roundtrip.sh).
static int expect_fd_init_failure()
{
  setenv("SAFETY_ISLAND_CAN_IFACE", "vcan0", 1);
  setenv("SAFETY_ISLAND_CAN_FORMAT", "fd", 1);
  common::can::ControlCommandCanOutput output;
  ASSERT_MSG(!output.init(), "CAN-FD init fails closed below the FD MTU");
  common::logger::log_info("low-MTU CAN-FD init failed closed");
  return 0;
}

int main(int argc, char ** argv)
{
  if (argc > 1 && std::strcmp(argv[1], "expect-fd-init-failure") == 0) {
    common::logger::log_info("=== Starting SocketCAN low-MTU FD check ===");
    return expect_fd_init_failure();
  }

  common::logger::log_info("=== Starting SocketCAN vcan sender ===");

  unsetenv("SAFETY_ISLAND_CAN_IFACE");
  ASSERT_MSG(!common::can::platform::can_init(), "missing iface fails init");

  setenv("SAFETY_ISLAND_CAN_IFACE", "does_not_exist", 1);
  ASSERT_MSG(!common::can::platform::can_init(), "missing device fails init");

  setenv("SAFETY_ISLAND_CAN_IFACE", "vcan0", 1);

  common::can::ControlCommandCanOutput output;
  ASSERT_MSG(output.init(), "SocketCAN TX initializes on vcan0");
  ASSERT_MSG(
    output.send(make_sample_control_msg(), common::can::ControlCommandOutputMode::CAN_ONLY),
    "encoded command is sent");

  const auto fd_encoded = common::can::encode_control_command_fd(
    make_sample_control_msg(), common::can::ControlCommandOutputMode::CAN_ONLY, 0U);
  ASSERT_MSG(fd_encoded.ok, "FD sample encodes");
  ASSERT_MSG(
    !common::can::platform::can_send_fd(fd_encoded.frame),
    "classic SocketCAN refuses a CAN-FD send");

  setenv("SAFETY_ISLAND_CAN_FORMAT", "invalid", 1);
  ASSERT_MSG(!output.init(), "unknown CAN format fails init");

  setenv("SAFETY_ISLAND_CAN_FORMAT", "fd", 1);
  ASSERT_MSG(output.init(), "SocketCAN CAN-FD TX initializes on vcan0");
  ASSERT_MSG(
    output.send(make_sample_control_msg(), common::can::ControlCommandOutputMode::CAN_ONLY),
    "encoded command is sent as CAN-FD");

  common::logger::log_info("vcan sender passed");
  return 0;
}
