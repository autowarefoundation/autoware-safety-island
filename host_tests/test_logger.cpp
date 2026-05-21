// SPDX-License-Identifier: Apache-2.0
#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include <rclcpp/rclcpp.hpp>

#include <sstream>

TEST_CASE("Logger has a name", "[logger]") {
  auto logger = rclcpp::get_logger("controller");
  REQUIRE(logger.get_name() == std::string("controller"));
}

TEST_CASE("RCLCPP_INFO compiles and runs", "[logger]") {
  auto logger = rclcpp::get_logger("ctl");
  RCLCPP_INFO(logger, "hello %s", "world");
  RCLCPP_WARN(logger, "n=%d", 42);
  RCLCPP_ERROR(logger, "static");
  SUCCEED();
}

TEST_CASE("Node ctor and accessors", "[node]") {
  rclcpp::Node n("ctrl");
  REQUIRE(n.get_name() == std::string("ctrl"));
  REQUIRE(n.get_logger().get_name() == std::string("ctrl"));
  REQUIRE(n.get_clock() != nullptr);
  auto p = n.declare_parameter<double>("ctrl_period", 0.05);
  REQUIRE(p == Catch::Approx(0.05));
}
