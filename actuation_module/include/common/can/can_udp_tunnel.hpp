#ifndef COMMON__CAN__CAN_UDP_TUNNEL_HPP_
#define COMMON__CAN__CAN_UDP_TUNNEL_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "common/can/can_frame.hpp"
#include "common/can/control_command_encoder.hpp"

namespace common::can
{

constexpr std::size_t kCanUdpTunnelDatagramSize = 48U;
constexpr uint8_t kCanUdpTunnelMagic0 = 0x43U;
constexpr uint8_t kCanUdpTunnelMagic1 = 0x54U;
constexpr uint8_t kCanUdpTunnelVersion = 1U;
constexpr uint8_t kCanUdpTunnelFlagClassicBatch = 0x01U;

struct PackedCanUdpTunnel
{
  bool ok{false};
  const char * error{"not packed"};
  std::array<uint8_t, kCanUdpTunnelDatagramSize> bytes{};
};

struct UnpackedCanUdpTunnel
{
  bool ok{false};
  const char * error{"not unpacked"};
  uint16_t sequence{0U};
  std::array<CanFrame, kControlCommandCanFrameCount> frames{};
};

namespace detail
{

inline void write_u16_le_bytes(uint8_t * dest, const uint16_t value)
{
  dest[0] = static_cast<uint8_t>(value & 0xFFU);
  dest[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline void write_u32_le_bytes(uint8_t * dest, const uint32_t value)
{
  dest[0] = static_cast<uint8_t>(value & 0xFFU);
  dest[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  dest[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  dest[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

inline uint16_t read_u16_le_bytes(const uint8_t * src)
{
  return static_cast<uint16_t>(
    static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8));
}

inline uint32_t read_u32_le_bytes(const uint8_t * src)
{
  return
    static_cast<uint32_t>(src[0]) |
    (static_cast<uint32_t>(src[1]) << 8) |
    (static_cast<uint32_t>(src[2]) << 16) |
    (static_cast<uint32_t>(src[3]) << 24);
}

inline uint16_t status_sequence(const CanFrame & frame)
{
  return static_cast<uint16_t>(
    static_cast<uint16_t>(frame.data[2]) | (static_cast<uint16_t>(frame.data[3]) << 8));
}

inline void write_wire_frame(uint8_t * dest, const CanFrame & frame)
{
  write_u32_le_bytes(dest, frame.id);
  dest[4] = frame.dlc;
  std::memcpy(dest + 5, frame.data.data(), kCanMaxDataLength);
}

inline CanFrame read_wire_frame(const uint8_t * src)
{
  CanFrame frame{};
  frame.id = read_u32_le_bytes(src);
  frame.dlc = src[4];
  frame.extended = false;
  std::memcpy(frame.data.data(), src + 5, kCanMaxDataLength);
  return frame;
}

}  // namespace detail

inline PackedCanUdpTunnel pack_can_udp_tunnel(
  const CanFrame * frames, const std::size_t count, const uint16_t sequence)
{
  PackedCanUdpTunnel packed{};
  if (frames == nullptr || count != kControlCommandCanFrameCount) {
    packed.error = "batch must contain three frames";
    return packed;
  }
  if (frames[0].id != kLateralCommandCanId || frames[0].dlc != 8U || frames[0].extended) {
    packed.error = "frame 0 must be classic 0x100 DLC 8";
    return packed;
  }
  if (frames[1].id != kLongitudinalCommandCanId || frames[1].dlc != 8U || frames[1].extended) {
    packed.error = "frame 1 must be classic 0x101 DLC 8";
    return packed;
  }
  if (frames[2].id != kCommandStatusCanId || frames[2].dlc != 8U || frames[2].extended) {
    packed.error = "frame 2 must be classic 0x102 DLC 8";
    return packed;
  }
  if (detail::status_sequence(frames[2]) != sequence) {
    packed.error = "batch sequence must match 0x102";
    return packed;
  }

  packed.bytes[0] = kCanUdpTunnelMagic0;
  packed.bytes[1] = kCanUdpTunnelMagic1;
  packed.bytes[2] = kCanUdpTunnelVersion;
  packed.bytes[3] = kCanUdpTunnelFlagClassicBatch;
  detail::write_u16_le_bytes(packed.bytes.data() + 4, sequence);
  packed.bytes[6] = 0U;
  packed.bytes[7] = 0U;
  detail::write_wire_frame(packed.bytes.data() + 8, frames[0]);
  detail::write_wire_frame(packed.bytes.data() + 21, frames[1]);
  detail::write_wire_frame(packed.bytes.data() + 34, frames[2]);
  packed.bytes[47] = 0U;
  packed.ok = true;
  packed.error = "";
  return packed;
}

inline UnpackedCanUdpTunnel unpack_can_udp_tunnel(const uint8_t * data, const std::size_t size)
{
  UnpackedCanUdpTunnel unpacked{};
  if (data == nullptr || size != kCanUdpTunnelDatagramSize) {
    unpacked.error = "datagram length must be 48";
    return unpacked;
  }
  if (data[0] != kCanUdpTunnelMagic0 || data[1] != kCanUdpTunnelMagic1) {
    unpacked.error = "invalid magic";
    return unpacked;
  }
  if (data[2] != kCanUdpTunnelVersion) {
    unpacked.error = "unsupported version";
    return unpacked;
  }
  if (data[3] != kCanUdpTunnelFlagClassicBatch) {
    unpacked.error = "unsupported flags";
    return unpacked;
  }
  if (data[6] != 0U || data[7] != 0U || data[47] != 0U) {
    unpacked.error = "reserved bytes must be zero";
    return unpacked;
  }

  unpacked.sequence = detail::read_u16_le_bytes(data + 4);
  unpacked.frames[0] = detail::read_wire_frame(data + 8);
  unpacked.frames[1] = detail::read_wire_frame(data + 21);
  unpacked.frames[2] = detail::read_wire_frame(data + 34);

  if (unpacked.frames[0].id != kLateralCommandCanId || unpacked.frames[0].dlc != 8U) {
    unpacked.error = "frame 0 must be classic 0x100 DLC 8";
    return unpacked;
  }
  if (unpacked.frames[1].id != kLongitudinalCommandCanId || unpacked.frames[1].dlc != 8U) {
    unpacked.error = "frame 1 must be classic 0x101 DLC 8";
    return unpacked;
  }
  if (unpacked.frames[2].id != kCommandStatusCanId || unpacked.frames[2].dlc != 8U) {
    unpacked.error = "frame 2 must be classic 0x102 DLC 8";
    return unpacked;
  }
  if (detail::status_sequence(unpacked.frames[2]) != unpacked.sequence) {
    unpacked.error = "batch sequence must match 0x102";
    return unpacked;
  }

  unpacked.ok = true;
  unpacked.error = "";
  return unpacked;
}

}  // namespace common::can

#endif  // COMMON__CAN__CAN_UDP_TUNNEL_HPP_
