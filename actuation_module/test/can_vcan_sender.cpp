#include <cassert>
#include <cstdlib>

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

int main()
{
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

  common::logger::log_info("vcan sender passed");
  return 0;
}
