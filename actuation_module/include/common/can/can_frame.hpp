#ifndef COMMON__CAN__CAN_FRAME_HPP_
#define COMMON__CAN__CAN_FRAME_HPP_

#include <array>
#include <cstddef>
#include <cstdint>

namespace common::can
{

constexpr std::size_t kCanMaxDataLength = 8U;

struct CanFrame
{
  uint32_t id{0U};
  uint8_t dlc{0U};
  bool extended{false};
  std::array<uint8_t, kCanMaxDataLength> data{};
};

}  // namespace common::can

#endif  // COMMON__CAN__CAN_FRAME_HPP_
