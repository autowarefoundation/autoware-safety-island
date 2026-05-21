// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef TF2__UTILS_H_
#define TF2__UTILS_H_
#include <cmath>
#include "tf2/LinearMath/Quaternion.h"

namespace tf2
{
inline double getYaw(const Quaternion & q)
{
  return std::atan2(
    2.0 * (q.w() * q.z() + q.x() * q.y()),
    1.0 - 2.0 * (q.y() * q.y() + q.z() * q.z()));
}
// Overload for geometry_msgs::msg::Quaternion (duck-typed: x/y/z/w fields).
template<typename Q>
inline double getYaw(const Q & q)
{
  return std::atan2(
    2.0 * (q.w * q.z + q.x * q.y),
    1.0 - 2.0 * (q.y * q.y + q.z * q.z));
}
}  // namespace tf2
#endif  // TF2__UTILS_H_
