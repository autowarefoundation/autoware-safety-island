// Copyright (c) 2026, Arm Limited.
// SPDX-License-Identifier: Apache-2.0
#ifndef AUTOWARE_UTILS__ROS__PUBLISHED_TIME_PUBLISHER_HPP_
#define AUTOWARE_UTILS__ROS__PUBLISHED_TIME_PUBLISHER_HPP_

namespace autoware_utils
{
class PublishedTimePublisher
{
public:
  template<typename NodeT> explicit PublishedTimePublisher(NodeT *) {}
  template<typename PubT, typename MsgT>
  void publish_if_subscribed(PubT *, const MsgT &) {}
};
}  // namespace autoware_utils
#endif  // AUTOWARE_UTILS__ROS__PUBLISHED_TIME_PUBLISHER_HPP_
