#ifndef PLATFORM__ZEPHYR__CAN_UDP_TUNNEL_H_
#define PLATFORM__ZEPHYR__CAN_UDP_TUNNEL_H_

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/can/can_frame.hpp"
#include "common/can/can_udp_tunnel.hpp"
#include "common/logger/logger.hpp"

namespace common::can::platform
{

inline int & can_udp_socket()
{
  static int socket_fd = -1;
  return socket_fd;
}

inline sockaddr_in & can_udp_peer()
{
  static sockaddr_in peer{};
  return peer;
}

inline void can_udp_close()
{
  int & socket_fd = can_udp_socket();
  if (socket_fd >= 0) {
    close(socket_fd);
    socket_fd = -1;
  }
}

inline bool can_init()
{
  can_udp_close();

  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(static_cast<uint16_t>(CONFIG_CONTROL_CMD_CAN_TUNNEL_PORT));
  if (inet_pton(AF_INET, CONFIG_CONTROL_CMD_CAN_TUNNEL_PEER, &peer.sin_addr) != 1) {
    common::logger::log_error(
      "CAN UDP tunnel peer is invalid: %s", CONFIG_CONTROL_CMD_CAN_TUNNEL_PEER);
    return false;
  }

  const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_fd < 0) {
    common::logger::log_error("CAN UDP socket failed: %s", std::strerror(errno));
    return false;
  }

  const int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    common::logger::log_error("CAN UDP O_NONBLOCK failed: %s", std::strerror(errno));
    close(socket_fd);
    return false;
  }

  can_udp_socket() = socket_fd;
  can_udp_peer() = peer;
  common::logger::log_info(
    "CAN UDP tunnel initialized to %s:%d",
    CONFIG_CONTROL_CMD_CAN_TUNNEL_PEER,
    CONFIG_CONTROL_CMD_CAN_TUNNEL_PORT);
  return true;
}

inline bool can_send(const CanFrame &)
{
  common::logger::log_error("CAN UDP tunnel requires can_send_batch()");
  return false;
}

inline bool can_send_batch(const CanFrame * frames, const std::size_t count)
{
  const int socket_fd = can_udp_socket();
  if (socket_fd < 0) {
    common::logger::log_error("CAN UDP tunnel is not initialized");
    return false;
  }

  uint16_t sequence = 0U;
  if (frames != nullptr && count == kControlCommandCanFrameCount) {
    sequence = static_cast<uint16_t>(
      static_cast<uint16_t>(frames[2].data[2]) |
      (static_cast<uint16_t>(frames[2].data[3]) << 8));
  }

  const auto packed = pack_can_udp_tunnel(frames, count, sequence);
  if (!packed.ok) {
    common::logger::log_error("CAN UDP pack failed: %s", packed.error);
    return false;
  }

  const auto & peer = can_udp_peer();
  const ssize_t sent = sendto(
    socket_fd,
    packed.bytes.data(),
    packed.bytes.size(),
    0,
    reinterpret_cast<const sockaddr *>(&peer),
    sizeof(peer));
  if (sent != static_cast<ssize_t>(packed.bytes.size())) {
    common::logger::log_error("CAN UDP send failed: %s", std::strerror(errno));
    return false;
  }
  return true;
}

}  // namespace common::can::platform

#endif  // PLATFORM__ZEPHYR__CAN_UDP_TUNNEL_H_
