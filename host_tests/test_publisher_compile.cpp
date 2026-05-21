// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_all.hpp>
#include <rclcpp/rclcpp.hpp>

struct FakeMsg { int x; };

TEST_CASE("Publisher / Subscription SharedPtr types resolve", "[pubsub]") {
  rclcpp::Node n("c");
  rclcpp::Publisher<FakeMsg>::SharedPtr pub =
    n.create_publisher<FakeMsg>("topic", rclcpp::QoS(1));
  REQUIRE(pub != nullptr);
  rclcpp::Subscription<FakeMsg>::SharedPtr sub =
    n.create_subscription<FakeMsg>("topic", rclcpp::QoS(1),
      [](const FakeMsg &) {});
  REQUIRE(sub != nullptr);
  auto t = n.create_wall_timer(std::chrono::milliseconds(100), []() {});
  REQUIRE(t != nullptr);
}
