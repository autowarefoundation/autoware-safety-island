// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__NODE_HPP_
#define RCLCPP__NODE_HPP_
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "rclcpp/clock.hpp"
#include "rclcpp/logger.hpp"
#include "rclcpp/node_options.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/time.hpp"

namespace rclcpp
{
using ParameterValue = std::variant<
  bool, int64_t, double, std::string,
  std::vector<bool>, std::vector<int64_t>, std::vector<double>, std::vector<std::string>>;

class Node
{
public:
  explicit Node(const std::string & name);
  Node(const std::string & name, const NodeOptions & options);
  virtual ~Node();

  const std::string & get_name() const { return name_; }
  Logger get_logger() const { return Logger(name_); }
  Clock::SharedPtr get_clock() const { return clock_; }
  Time now() const { return clock_->now(); }

  template<typename T>
  T declare_parameter(const std::string & name);
  template<typename T>
  T declare_parameter(const std::string & name, const T & default_value);

  template<typename T>
  T get_parameter(const std::string & name) const;

private:
  std::string name_;
  Clock::SharedPtr clock_;
  std::unordered_map<std::string, ParameterValue> params_;
};

template<typename T>
T Node::declare_parameter(const std::string & name, const T & default_value)
{
  auto [it, inserted] = params_.emplace(name, ParameterValue{default_value});
  if (!inserted) {
    return std::get<T>(it->second);
  }
  return default_value;
}

template<typename T>
T Node::declare_parameter(const std::string & name)
{
  return declare_parameter<T>(name, T{});
}

template<typename T>
T Node::get_parameter(const std::string & name) const
{
  auto it = params_.find(name);
  if (it == params_.end()) {
    return T{};
  }
  return std::get<T>(it->second);
}
}  // namespace rclcpp
#endif  // RCLCPP__NODE_HPP_
