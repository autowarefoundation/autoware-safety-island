// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef AUTOWARE_UTILS__ROS__MARKER_HELPER_HPP_
#define AUTOWARE_UTILS__ROS__MARKER_HELPER_HPP_
#include "visualization_msgs/msg/marker.hpp"

namespace autoware_utils
{
// Marker helpers are not used at runtime; provide free-function shells
// matching the upstream signatures that universe_utils may reference.
inline visualization_msgs::msg::Marker create_default_marker(
  const char *, double, double, double, double, double, double, double, double)
{
  return {};
}
}  // namespace autoware_utils
#endif  // AUTOWARE_UTILS__ROS__MARKER_HELPER_HPP_
