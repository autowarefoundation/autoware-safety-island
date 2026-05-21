// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef VISUALIZATION_MSGS__MSG__MARKER_ARRAY_HPP_
#define VISUALIZATION_MSGS__MSG__MARKER_ARRAY_HPP_
#include <vector>
#include "visualization_msgs/msg/marker.hpp"

namespace visualization_msgs::msg
{
struct MarkerArray { std::vector<Marker> markers; };
}  // namespace visualization_msgs::msg
#endif  // VISUALIZATION_MSGS__MSG__MARKER_ARRAY_HPP_
