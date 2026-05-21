set(rclcpp_FOUND TRUE)
# Real consumers will use target_link_libraries(... rclcpp::rclcpp).
# The alias is declared by shim/rclcpp/CMakeLists.txt, which must have
# been add_subdirectory()-ed before find_package(rclcpp) runs.
if(NOT TARGET rclcpp::rclcpp)
  message(FATAL_ERROR "rclcpp facade not added; add_subdirectory(shim/rclcpp) is required first")
endif()
set(rclcpp_INCLUDE_DIRS "")
set(rclcpp_LIBRARIES rclcpp::rclcpp)
