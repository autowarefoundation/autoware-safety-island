# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Minimal rclcpp facade. Interface-only at this stage; .cpp sources are
# added incrementally as the facade grows (Tasks 3.3+).

add_library(rclcpp_facade INTERFACE)
target_include_directories(rclcpp_facade INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# Alias so upstream find_package(rclcpp) consumers can link rclcpp::rclcpp.
add_library(rclcpp::rclcpp ALIAS rclcpp_facade)
