// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef VISUALIZATION_MSGS__MSG__MARKER_HPP_
#define VISUALIZATION_MSGS__MSG__MARKER_HPP_
#include <cstdint>
#include <string>

namespace visualization_msgs::msg
{
struct Marker
{
  // Minimal stub. Upstream code reads/writes these fields; we provide
  // them as plain data members. No runtime semantics.
  int32_t type{0};
  int32_t action{0};
  std::string ns;
  int32_t id{0};
  // Additional fields can be added lazily as compile errors surface.
};
}  // namespace visualization_msgs::msg
#endif  // VISUALIZATION_MSGS__MSG__MARKER_HPP_
