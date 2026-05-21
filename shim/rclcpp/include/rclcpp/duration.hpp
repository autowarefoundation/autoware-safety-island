// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__DURATION_HPP_
#define RCLCPP__DURATION_HPP_
#include <cstdint>

namespace rclcpp
{
class Duration
{
public:
  Duration() = default;
  explicit Duration(int64_t ns) : ns_(ns) {}
  static Duration from_seconds(double s) {
    return Duration(static_cast<int64_t>(s * 1e9));
  }
  double seconds() const { return static_cast<double>(ns_) / 1e9; }
  int64_t nanoseconds() const { return ns_; }
  Duration operator+(const Duration & o) const { return Duration(ns_ + o.ns_); }
  Duration operator-(const Duration & o) const { return Duration(ns_ - o.ns_); }
  bool operator<(const Duration & o) const { return ns_ < o.ns_; }
  bool operator>(const Duration & o) const { return ns_ > o.ns_; }
  bool operator==(const Duration & o) const { return ns_ == o.ns_; }
private:
  int64_t ns_{0};
};
}  // namespace rclcpp
#endif  // RCLCPP__DURATION_HPP_
