// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef AUTOWARE_UTILS__ROS__LOGGER_LEVEL_CONFIGURE_HPP_
#define AUTOWARE_UTILS__ROS__LOGGER_LEVEL_CONFIGURE_HPP_

namespace autoware_utils
{
class LoggerLevelConfigure
{
public:
  template<typename NodeT> explicit LoggerLevelConfigure(NodeT *) {}
};
}  // namespace autoware_utils
#endif  // AUTOWARE_UTILS__ROS__LOGGER_LEVEL_CONFIGURE_HPP_
