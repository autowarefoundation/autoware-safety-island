// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_
#define DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_
#include <memory>
#include <string>
#include <rclcpp/rclcpp.hpp>

namespace diagnostic_updater
{
class DiagnosticStatusWrapper
{
public:
  void add(const std::string &, const std::string &) {}
  void summary(int, const std::string &) {}
};
class Updater
{
public:
  template<typename NodeT>
  explicit Updater(NodeT *) {}
  void setHardwareID(const std::string &) {}
  template<typename T, typename U>
  void add(const std::string &, T *, U) {}
  void force_update() {}
};
}  // namespace diagnostic_updater
#endif  // DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_
