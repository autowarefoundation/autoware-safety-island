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
find_package(Threads REQUIRED)

# ---- Platform definition ----
# dds_pub.cpp / dds_sub.cpp pull in the common/ and platform/ headers, which
# require a platform selection and the CONFIG_* macros. The edge ECU peer runs
# on the host (Linux), so it builds the FreeRTOS POSIX backend just like the
# simulator app_pub / app_sub targets.
add_compile_definitions(PLATFORM_FREERTOS=1)

# ---- Configurable values (mirror actuation_module/freertos/CMakeLists.txt) ----
set(CONFIG_LOG_LEVEL 1 CACHE STRING "Log level (0=off, 1=info, 2=debug)")
set(CONFIG_LOG_THROTTLE_RATE 3 CACHE STRING "Log throttle rate in seconds")
set(CONFIG_DDS_DOMAIN_ID 2 CACHE STRING "DDS domain ID")
set(CONFIG_DDS_NETWORK_INTERFACE "" CACHE STRING "DDS network interface (name or host IP; empty=auto)")
set(CONFIG_DDS_LOG_LEVEL 0 CACHE STRING "DDS log level (0=off, 1=warn, 2=debug)")
set(CONFIG_THREAD_STACK_SIZE 262144 CACHE STRING "Thread stack size in bytes")
set(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX 0 CACHE STRING "DHCP delay (0 for host)")
set(CONFIG_ENABLE_SNTP OFF CACHE BOOL "Enable SNTP time sync")

# The peer only exchanges DDS; the CAN/output mode macros are unused here but
# the shared config template still expands them, so give them DDS-only values.
set(CONFIG_CONTROL_CMD_OUTPUT_DDS_ONLY 1)
set(CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY 0)
set(CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN 0)
set(CONFIG_CONTROL_CMD_CAN_OUTPUT 0)
set(CONFIG_CONTROL_CMD_DDS_OUTPUT 1)

# Generate the platform config header (same template the simulator uses).
configure_file(
    ${ACTUATION_DIR}/include/platform/freertos/freertos_config_generated.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/freertos_config_generated.h
    @ONLY
)

add_subdirectory(${ACTUATION_DIR}/src/autoware/autoware_msgs
                 ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

foreach(peer edge_ecu_pub edge_ecu_sub)
    if(peer STREQUAL edge_ecu_pub)
        set(_src ${ACTUATION_DIR}/test/dds_pub.cpp)
    else()
        set(_src ${ACTUATION_DIR}/test/dds_sub.cpp)
    endif()
    add_executable(${peer} ${_src})
    target_link_libraries(${peer} PRIVATE
        CycloneDDS::ddsc autoware_msgs Threads::Threads)
    target_include_directories(${peer} PRIVATE
        ${ACTUATION_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)
endforeach()
