// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__TIME_HPP_
#define RCLCPP__TIME_HPP_
#include <cstdint>
#include "rclcpp/duration.hpp"

namespace rclcpp
{
class Time
{
public:
  Time() = default;
  explicit Time(int64_t ns) : ns_(ns) {}
  Time(int32_t sec, uint32_t nsec) : ns_(int64_t{sec} * 1'000'000'000 + nsec) {}
  int64_t nanoseconds() const { return ns_; }
  double seconds() const { return static_cast<double>(ns_) / 1e9; }
  Time operator+(const Duration & d) const { return Time(ns_ + d.nanoseconds()); }
  Duration operator-(const Time & o) const { return Duration(ns_ - o.ns_); }
  bool operator<(const Time & o) const { return ns_ < o.ns_; }
  bool operator>(const Time & o) const { return ns_ > o.ns_; }
private:
  int64_t ns_{0};
};
}  // namespace rclcpp
#endif  // RCLCPP__TIME_HPP_
