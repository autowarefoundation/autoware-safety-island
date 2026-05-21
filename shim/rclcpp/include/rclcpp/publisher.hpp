// SPDX-License-Identifier: Apache-2.0
#ifndef RCLCPP__PUBLISHER_HPP_
#define RCLCPP__PUBLISHER_HPP_
#include <memory>
#include <string>
#include "rclcpp/qos.hpp"

namespace rclcpp
{
template<typename MsgT>
class Publisher
{
public:
  using SharedPtr = std::shared_ptr<Publisher<MsgT>>;
  Publisher(const std::string & topic, const QoS & qos)
  : topic_(topic), qos_(qos) {}
  // Stage 2-4 will replace this with rcl_publish.
  void publish(const MsgT &) { /* no-op at compile stage */ }
  const std::string & topic_name() const { return topic_; }
private:
  std::string topic_;
  QoS qos_;
};
}  // namespace rclcpp
#endif  // RCLCPP__PUBLISHER_HPP_
