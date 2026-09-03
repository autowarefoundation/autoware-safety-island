#ifndef PLATFORM__FREERTOS__CAN_SOCKETCAN_H_
#define PLATFORM__FREERTOS__CAN_SOCKETCAN_H_

#include <cerrno>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>

#include "common/can/can_frame.hpp"
#include "common/logger/logger.hpp"

namespace common::can::platform
{

inline int & can_socket()
{
  static int socket_fd = -1;
  return socket_fd;
}

inline void can_close_socket()
{
  int & socket_fd = can_socket();
  if (socket_fd >= 0) {
    close(socket_fd);
    socket_fd = -1;
  }
}

inline bool can_init()
{
  can_close_socket();

  const char * iface = std::getenv("SAFETY_ISLAND_CAN_IFACE");
  if (iface == nullptr || iface[0] == '\0') {
    common::logger::log_error("SAFETY_ISLAND_CAN_IFACE is not set");
    return false;
  }

  const int socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (socket_fd < 0) {
    common::logger::log_error("PF_CAN socket failed: %s", std::strerror(errno));
    return false;
  }

  struct ifreq request{};
  std::strncpy(request.ifr_name, iface, IFNAMSIZ - 1);
  request.ifr_name[IFNAMSIZ - 1] = '\0';
  if (ioctl(socket_fd, SIOCGIFINDEX, &request) < 0) {
    common::logger::log_error("SIOCGIFINDEX failed for %s: %s", iface, std::strerror(errno));
    close(socket_fd);
    return false;
  }

  struct sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = request.ifr_ifindex;
  if (bind(socket_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    common::logger::log_error("CAN bind failed for %s: %s", iface, std::strerror(errno));
    close(socket_fd);
    return false;
  }

  const int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    common::logger::log_error("CAN O_NONBLOCK failed: %s", std::strerror(errno));
    close(socket_fd);
    return false;
  }

  can_socket() = socket_fd;
  common::logger::log_info("SocketCAN initialized on %s", iface);
  return true;
}

inline bool can_send(const CanFrame & frame)
{
  const int socket_fd = can_socket();
  if (socket_fd < 0) {
    common::logger::log_error("SocketCAN is not initialized");
    return false;
  }
  if (frame.dlc > kCanMaxDataLength || frame.extended) {
    common::logger::log_error(
      "SocketCAN rejected frame id=0x%03x dlc=%u extended=%d",
      static_cast<unsigned int>(frame.id),
      static_cast<unsigned int>(frame.dlc),
      frame.extended ? 1 : 0);
    return false;
  }

  struct can_frame wire{};
  wire.can_id = frame.id;
  wire.can_dlc = frame.dlc;
  std::memcpy(wire.data, frame.data.data(), frame.dlc);

  const ssize_t written = write(socket_fd, &wire, sizeof(wire));
  if (written != static_cast<ssize_t>(sizeof(wire))) {
    common::logger::log_error(
      "SocketCAN write failed for id=0x%03x: %s",
      static_cast<unsigned int>(frame.id),
      std::strerror(errno));
    return false;
  }
  return true;
}

inline bool can_send_batch(const CanFrame * frames, const std::size_t count)
{
  for (std::size_t index = 0U; index < count; ++index) {
    if (!can_send(frames[index])) {
      return false;
    }
  }
  return true;
}

}  // namespace common::can::platform

#endif  // PLATFORM__FREERTOS__CAN_SOCKETCAN_H_
