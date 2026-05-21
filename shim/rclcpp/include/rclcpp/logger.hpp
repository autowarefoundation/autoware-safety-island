// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__LOGGER_HPP_
#define RCLCPP__LOGGER_HPP_
#include <string>
#include <utility>

namespace rclcpp
{
class Logger
{
public:
  explicit Logger(std::string name) : name_(std::move(name)) {}
  const std::string & get_name() const { return name_; }
private:
  std::string name_;
};

inline Logger get_logger(const std::string & name) { return Logger(name); }
}  // namespace rclcpp
#endif  // RCLCPP__LOGGER_HPP_
