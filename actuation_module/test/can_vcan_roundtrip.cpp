#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "autoware/autoware_msgs/messages.hpp"
#include "common/can/control_command_can_output.hpp"
#include "common/can/control_command_decoder.hpp"
#include "common/can/control_command_encoder.hpp"
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

static int open_rx_socket(const char * iface)
{
  const int socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (socket_fd < 0) {
    return -1;
  }

  struct ifreq request{};
  std::strncpy(request.ifr_name, iface, IFNAMSIZ - 1);
  request.ifr_name[IFNAMSIZ - 1] = '\0';
  if (ioctl(socket_fd, SIOCGIFINDEX, &request) < 0) {
    close(socket_fd);
    return -1;
  }

  struct sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = request.ifr_ifindex;
  if (bind(socket_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    close(socket_fd);
    return -1;
  }

  struct timeval timeout{};
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    close(socket_fd);
    return -1;
  }
  return socket_fd;
}

static common::can::CanFrame read_frame(const int socket_fd)
{
  struct can_frame wire{};
  const ssize_t n = recv(socket_fd, &wire, sizeof(wire), 0);
  ASSERT_MSG(n == static_cast<ssize_t>(sizeof(wire)), "SocketCAN receive timed out");
  common::can::CanFrame frame{};
  frame.id = wire.can_id & CAN_SFF_MASK;
  frame.dlc = wire.can_dlc;
  frame.extended = (wire.can_id & CAN_EFF_FLAG) != 0U;
  std::memcpy(frame.data.data(), wire.data, frame.dlc);
  return frame;
}

int main()
{
  common::logger::log_info("=== Starting SocketCAN vcan roundtrip ===");

  unsetenv("SAFETY_ISLAND_CAN_IFACE");
  ASSERT_MSG(!common::can::platform::can_init(), "missing iface fails init");

  setenv("SAFETY_ISLAND_CAN_IFACE", "does_not_exist", 1);
  ASSERT_MSG(!common::can::platform::can_init(), "missing device fails init");

  const char * iface = "vcan0";
  setenv("SAFETY_ISLAND_CAN_IFACE", iface, 1);

  const int rx = open_rx_socket(iface);
  ASSERT_MSG(rx >= 0, "RX socket opens on vcan0");

  common::can::ControlCommandCanOutput output;
  ASSERT_MSG(output.init(), "SocketCAN TX initializes on vcan0");
  ASSERT_MSG(
    output.send(make_sample_control_msg(), common::can::ControlCommandOutputMode::CAN_ONLY),
    "encoded command is sent");

  common::can::ControlCommandDecoder decoder;
  common::can::DecoderEvent event = common::can::DecoderEvent::Ignored;
  for (int i = 0; i < 3; ++i) {
    event = decoder.feed(read_frame(rx), 0.0);
  }
  close(rx);

  ASSERT_MSG(event == common::can::DecoderEvent::Accepted, "decoder accepted the vcan cycle");
  ASSERT_MSG(decoder.command().sequence == 0U, "decoded sequence");
  ASSERT_MSG(std::fabs(decoder.command().steering_tire_angle - 0.125F) < 1e-6F, "decoded steer");
  ASSERT_MSG(std::fabs(decoder.command().velocity - 12.25F) < 1e-4F, "decoded velocity");
  ASSERT_MSG(std::fabs(decoder.command().acceleration + 1.5F) < 1e-4F, "decoded acceleration");
  ASSERT_MSG(decoder.poll_watchdog(0.51, 0.5) == common::can::DecoderEvent::SafeStop, "watchdog after producer stops");
  ASSERT_MSG(decoder.in_safe_stop(), "decoder holds safe stop");

  common::logger::log_info("vcan roundtrip tests passed");
  return 0;
}
