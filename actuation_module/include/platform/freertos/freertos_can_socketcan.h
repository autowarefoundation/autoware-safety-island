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

#include "common/can/can_fd_frame.hpp"
#include "common/can/can_frame.hpp"
#include "common/logger/logger.hpp"

namespace common::can::platform
{

inline int & can_socket()
{
  static int socket_fd = -1;
  return socket_fd;
}

inline bool & can_fd_enabled()
{
  static bool enabled = false;
  return enabled;
}

inline bool can_fd_active()
{
  return can_fd_enabled();
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
  can_fd_enabled() = false;

  const char * iface = std::getenv("SAFETY_ISLAND_CAN_IFACE");
  if (iface == nullptr || iface[0] == '\0') {
    common::logger::log_error("SAFETY_ISLAND_CAN_IFACE is not set");
    return false;
  }

  const char * format = std::getenv("SAFETY_ISLAND_CAN_FORMAT");
  bool fd_enabled = false;
  if (format != nullptr && format[0] != '\0' && std::strcmp(format, "classic") != 0) {
    if (std::strcmp(format, "fd") != 0) {
      common::logger::log_error("SAFETY_ISLAND_CAN_FORMAT must be \"classic\" or \"fd\"");
      return false;
    }
    fd_enabled = true;
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

  if (fd_enabled) {
    struct ifreq mtu_request{};
    std::strncpy(mtu_request.ifr_name, iface, IFNAMSIZ - 1);
    mtu_request.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(socket_fd, SIOCGIFMTU, &mtu_request) < 0) {
      common::logger::log_error("SIOCGIFMTU failed for %s: %s", iface, std::strerror(errno));
      close(socket_fd);
      return false;
    }
    if (mtu_request.ifr_mtu < static_cast<int>(CANFD_MTU)) {
      common::logger::log_error(
        "%s does not support CAN-FD (mtu=%d); enable CAN-FD on the interface",
        iface,
        mtu_request.ifr_mtu);
      close(socket_fd);
      return false;
    }

    const int enable_fd = 1;
    if (setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd, sizeof(enable_fd)) < 0) {
      common::logger::log_error("CAN_RAW_FD_FRAMES failed for %s: %s", iface, std::strerror(errno));
      close(socket_fd);
      return false;
    }
  }

  const int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    common::logger::log_error("CAN O_NONBLOCK failed: %s", std::strerror(errno));
    close(socket_fd);
    return false;
  }

  can_socket() = socket_fd;
  can_fd_enabled() = fd_enabled;
  common::logger::log_info(
    "SocketCAN initialized on %s (format=%s)", iface, fd_enabled ? "fd" : "classic");
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

inline bool can_send_fd(const CanFdFrame & frame)
{
  const int socket_fd = can_socket();
  if (socket_fd < 0) {
    common::logger::log_error("SocketCAN is not initialized");
    return false;
  }
  if (!can_fd_enabled()) {
    common::logger::log_error("CAN-FD send requires SAFETY_ISLAND_CAN_FORMAT=fd");
    return false;
  }
  if (frame.length > kCanFdMaxDataLength || frame.id > 0x7FFU) {
    common::logger::log_error(
      "SocketCAN rejected CAN-FD frame id=0x%03x length=%u",
      static_cast<unsigned int>(frame.id),
      static_cast<unsigned int>(frame.length));
    return false;
  }

  struct canfd_frame wire{};
  wire.can_id = frame.id;
  wire.len = frame.length;
  wire.flags = frame.brs ? CANFD_BRS : 0U;
  std::memcpy(wire.data, frame.data.data(), frame.length);

  const ssize_t written = write(socket_fd, &wire, sizeof(wire));
  if (written != static_cast<ssize_t>(sizeof(wire))) {
    common::logger::log_error(
      "SocketCAN CAN-FD write failed for id=0x%03x: %s",
      static_cast<unsigned int>(frame.id),
      std::strerror(errno));
    return false;
  }
  return true;
}

}  // namespace common::can::platform

#endif  // PLATFORM__FREERTOS__CAN_SOCKETCAN_H_
