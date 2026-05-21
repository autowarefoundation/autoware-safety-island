cmake_minimum_required(VERSION 3.22)
project(actuation_host_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.3
)
FetchContent_MakeAvailable(catch2)

add_subdirectory(../shim/rclcpp shim_rclcpp)

add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE rclcpp::rclcpp Catch2::Catch2WithMain)

enable_testing()
add_test(NAME logger COMMAND test_logger)

add_executable(test_clock_time test_clock_time.cpp)
target_link_libraries(test_clock_time PRIVATE rclcpp::rclcpp Catch2::Catch2WithMain)
add_test(NAME clock_time COMMAND test_clock_time)

add_executable(test_qos test_qos.cpp)
target_link_libraries(test_qos PRIVATE rclcpp::rclcpp Catch2::Catch2WithMain)
add_test(NAME qos COMMAND test_qos)

add_executable(test_publisher_compile test_publisher_compile.cpp)
target_link_libraries(test_publisher_compile PRIVATE rclcpp::rclcpp Catch2::Catch2WithMain)
add_test(NAME publisher_compile COMMAND test_publisher_compile)

# Compile probe: tries to build autoware_universe_utils with vendored
# sources untouched. The target is EXCLUDE_FROM_ALL, so it does not
# participate in the default build or `ctest` — invoke it explicitly with
# `cmake --build build/host_tests --target autoware_universe_utils` to
# surface the next gap in the facade / stubs / message-generation.
#
# Note: the iterate-to-green for this target is consciously deferred to
# Stage 2-5 (next stack PR), where the required stub headers — tf2,
# diagnostic_updater, visualization_msgs, autoware_utils — land. The
# plan acknowledges this with "out-of-order is allowed; do Task 5.1 now
# (Stage 2-3 Task 3.9, step 4)". We keep the scopes separated for review
# clarity: this PR ships the facade alone.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake")
include(AddAutowarePackage)
add_autoware_package(autoware_universe_utils SUBDIR common/autoware_universe_utils)
