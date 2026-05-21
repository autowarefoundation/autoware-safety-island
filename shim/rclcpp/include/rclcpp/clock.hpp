// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__CLOCK_HPP_
#define RCLCPP__CLOCK_HPP_
#include <memory>
#include "rclcpp/time.hpp"

namespace rclcpp
{
class Clock
{
public:
  using SharedPtr = std::shared_ptr<Clock>;
  Clock() = default;
  Time now();
};
}  // namespace rclcpp
#endif  // RCLCPP__CLOCK_HPP_
