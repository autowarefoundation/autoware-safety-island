// SPDX-License-Identifier: Apache-2.0
#include "rclcpp/node.hpp"

namespace rclcpp
{
Node::Node(const std::string & name)
: name_(name), clock_(std::make_shared<Clock>()) {}

Node::Node(const std::string & name, const NodeOptions &)
: Node(name) {}

Node::~Node() = default;
}  // namespace rclcpp
