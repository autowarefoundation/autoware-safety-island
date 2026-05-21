// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__NODE_OPTIONS_HPP_
#define RCLCPP__NODE_OPTIONS_HPP_
namespace rclcpp
{
class NodeOptions
{
public:
  NodeOptions() = default;
  // Upstream chains setters; we accept and ignore them.
  template<typename T> NodeOptions & arguments(const T &) { return *this; }
  NodeOptions & use_intra_process_comms(bool) { return *this; }
  NodeOptions & automatically_declare_parameters_from_overrides(bool) { return *this; }
};
}  // namespace rclcpp
#endif  // RCLCPP__NODE_OPTIONS_HPP_
