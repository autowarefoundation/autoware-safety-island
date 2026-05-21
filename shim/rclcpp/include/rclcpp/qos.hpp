// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__QOS_HPP_
#define RCLCPP__QOS_HPP_
#include <cstddef>

namespace rclcpp
{
struct KeepLast { size_t depth; explicit KeepLast(size_t d) : depth(d) {} };

class QoS
{
public:
  explicit QoS(size_t depth) : depth_(depth) {}
  explicit QoS(const KeepLast & k) : depth_(k.depth) {}

  QoS & transient_local() { transient_local_ = true; return *this; }
  QoS & reliable()        { reliable_ = true; return *this; }
  QoS & best_effort()     { reliable_ = false; return *this; }
  QoS & durability_volatile() { transient_local_ = false; return *this; }

  size_t depth() const          { return depth_; }
  bool is_transient_local() const { return transient_local_; }
  bool is_reliable() const      { return reliable_; }

private:
  size_t depth_;
  bool transient_local_{false};
  bool reliable_{true};
};
}  // namespace rclcpp
#endif  // RCLCPP__QOS_HPP_
