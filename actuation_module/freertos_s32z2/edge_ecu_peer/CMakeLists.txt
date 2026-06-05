# Copyright (c) 2026, Arm Limited and contributors.
# SPDX-License-Identifier: Apache-2.0
#
# Host-side build of dds_pub / dds_sub from actuation_module/test/, used as the
# DDS peer (Edge ECU role) for the S32Z2 verification.

cmake_minimum_required(VERSION 3.22)
project(actuation_edge_ecu_peer LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ACTUATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(REPO_ROOT     ${ACTUATION_DIR}/..)

# Reuse the host CycloneDDS already built for the POSIX simulator.
set(CDDS_HOST_PREFIX ${REPO_ROOT}/build-freertos/cdds_host_out)
if(NOT EXISTS ${CDDS_HOST_PREFIX}/bin/idlc)
    message(FATAL_ERROR
        "Host CycloneDDS not found at ${CDDS_HOST_PREFIX}. Run the POSIX "
        "simulator phase 1 build first.")
endif()
set(ENV{PATH} "${CDDS_HOST_PREFIX}/bin:$ENV{PATH}")
set(CycloneDDS_DIR ${CDDS_HOST_PREFIX}/lib/cmake/CycloneDDS)
find_package(CycloneDDS REQUIRED)

# dds_pub.cpp / dds_sub.cpp include common/node/node.hpp, which resolves the
# platform layer through platform/platform_config.h (and platform_clock.h /
# platform_threading.h). Those headers require PLATFORM_FREERTOS to be defined
# and the generated freertos_config_generated.h to be on the include path.
# Mirror the POSIX FreeRTOS simulator build (actuation_module/freertos/
# CMakeLists.txt) so this peer compiles self-contained instead of depending on a
# sibling build tree having produced the header.
add_compile_definitions(PLATFORM_FREERTOS=1)

set(CONFIG_LOG_LEVEL 1)
set(CONFIG_LOG_THROTTLE_RATE 3)
set(CONFIG_DDS_DOMAIN_ID 2)
set(CONFIG_DDS_NETWORK_INTERFACE "")  # empty = auto-select (overridable at runtime)
set(CONFIG_DDS_LOG_LEVEL 0)
set(CONFIG_THREAD_STACK_SIZE 262144)
set(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX 0)
set(CONFIG_ENABLE_SNTP OFF)
# The control-command output mode is irrelevant to a pure DDS peer, but the
# generated header declares these macros unconditionally, so give them values.
set(CONFIG_CONTROL_CMD_OUTPUT_DDS_ONLY 0)
set(CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY 0)
set(CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN 1)
set(CONFIG_CONTROL_CMD_CAN_OUTPUT 1)
set(CONFIG_CONTROL_CMD_DDS_OUTPUT 1)

set(GENERATED_CONFIG_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(
    ${ACTUATION_DIR}/include/platform/freertos/freertos_config_generated.h.in
    ${GENERATED_CONFIG_DIR}/freertos_config_generated.h)

add_subdirectory(${ACTUATION_DIR}/src/autoware/autoware_msgs
                 ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

add_executable(edge_ecu_pub ${ACTUATION_DIR}/test/dds_pub.cpp)
target_link_libraries(edge_ecu_pub PRIVATE CycloneDDS::ddsc autoware_msgs)
target_include_directories(edge_ecu_pub PRIVATE
    ${ACTUATION_DIR}/include
    ${GENERATED_CONFIG_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

add_executable(edge_ecu_sub ${ACTUATION_DIR}/test/dds_sub.cpp)
target_link_libraries(edge_ecu_sub PRIVATE CycloneDDS::ddsc autoware_msgs)
target_include_directories(edge_ecu_sub PRIVATE
    ${ACTUATION_DIR}/include
    ${GENERATED_CONFIG_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)
