// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_all.hpp>
#include <rclcpp/rclcpp.hpp>

TEST_CASE("QoS builder API", "[qos]") {
  auto qos = rclcpp::QoS(10).transient_local().reliable();
  REQUIRE(qos.depth() == 10u);
  REQUIRE(qos.is_transient_local());
  REQUIRE(qos.is_reliable());
}

TEST_CASE("NodeOptions default-constructible", "[node_options]") {
  rclcpp::NodeOptions opts;
  (void)opts;
  SUCCEED();
}
