#ifndef COMMON__CAN__CAN_FD_FRAME_HPP_
#define COMMON__CAN__CAN_FD_FRAME_HPP_

#include <array>
#include <cstddef>
#include <cstdint>

namespace common::can
{

constexpr std::size_t kCanFdMaxDataLength = 64U;

// CAN-FD frame: opt-in alternative to the classic three-frame batch.
// `length` is the payload byte count. SocketCAN maps it to the wire DLC;
// FD DLC 9..15 encode 12/16/20/24/32/48/64 bytes and are not byte counts.
struct CanFdFrame
{
  uint32_t id{0U};
  uint8_t length{0U};
  bool brs{false};
  std::array<uint8_t, kCanFdMaxDataLength> data{};
};

}  // namespace common::can

#endif  // COMMON__CAN__CAN_FD_FRAME_HPP_