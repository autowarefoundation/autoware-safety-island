// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__SUBSCRIPTION_HPP_
#define RCLCPP__SUBSCRIPTION_HPP_
#include <functional>
#include <memory>
#include <string>
#include "rclcpp/qos.hpp"

namespace rclcpp
{
template<typename MsgT>
class Subscription
{
public:
  using SharedPtr = std::shared_ptr<Subscription<MsgT>>;
  using Callback = std::function<void(const MsgT &)>;
  using ConstSharedPtr = std::shared_ptr<const MsgT>;
  Subscription(const std::string & topic, const QoS & qos, Callback cb)
  : topic_(topic), qos_(qos), cb_(std::move(cb)) {}
private:
  std::string topic_;
  QoS qos_;
  Callback cb_;
};
}  // namespace rclcpp
#endif  // RCLCPP__SUBSCRIPTION_HPP_
