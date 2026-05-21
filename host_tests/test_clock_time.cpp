// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_all.hpp>
#include <rclcpp/rclcpp.hpp>

TEST_CASE("Duration ctor + arithmetic", "[time]") {
  rclcpp::Duration d = rclcpp::Duration::from_seconds(2.5);
  REQUIRE(d.seconds() == Catch::Approx(2.5));
  REQUIRE(d.nanoseconds() == int64_t{2'500'000'000});
}

TEST_CASE("Time arithmetic", "[time]") {
  rclcpp::Time t0(1'000'000'000);  // 1 second since epoch (nanos)
  rclcpp::Time t1 = t0 + rclcpp::Duration::from_seconds(0.5);
  REQUIRE(t1.nanoseconds() == int64_t{1'500'000'000});
  rclcpp::Duration diff = t1 - t0;
  REQUIRE(diff.seconds() == Catch::Approx(0.5));
}

TEST_CASE("Clock::now monotonic", "[clock]") {
  rclcpp::Clock c;
  rclcpp::Time a = c.now();
  rclcpp::Time b = c.now();
  REQUIRE(b.nanoseconds() >= a.nanoseconds());
}
