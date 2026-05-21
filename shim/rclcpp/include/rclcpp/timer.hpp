// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__TIMER_HPP_
#define RCLCPP__TIMER_HPP_
#include <chrono>
#include <functional>
#include <memory>
#include "rclcpp/duration.hpp"

namespace rclcpp
{
class TimerBase
{
public:
  using SharedPtr = std::shared_ptr<TimerBase>;
  TimerBase(std::chrono::nanoseconds period, std::function<void()> cb)
  : period_(period), cb_(std::move(cb)) {}
  virtual ~TimerBase() = default;
protected:
  std::chrono::nanoseconds period_;
  std::function<void()> cb_;
};
}  // namespace rclcpp
#endif  // RCLCPP__TIMER_HPP_
