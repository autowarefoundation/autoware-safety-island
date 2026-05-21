// SPDX-License-Identifier: Apache-2.0
#include "rclcpp/clock.hpp"
#include <chrono>

namespace rclcpp
{
Time Clock::now()
{
  const auto t = std::chrono::steady_clock::now().time_since_epoch();
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t).count();
  return Time(static_cast<int64_t>(ns));
}
}  // namespace rclcpp
