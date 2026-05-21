add_library(rclcpp_facade STATIC
  src/clock.cpp
)
target_include_directories(rclcpp_facade PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_library(rclcpp::rclcpp ALIAS rclcpp_facade)
