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

add_subdirectory(${ACTUATION_DIR}/src/autoware/autoware_msgs
                 ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

add_executable(edge_ecu_pub ${ACTUATION_DIR}/test/dds_pub.cpp)
target_link_libraries(edge_ecu_pub PRIVATE CycloneDDS::ddsc autoware_msgs)
target_include_directories(edge_ecu_pub PRIVATE
    ${ACTUATION_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

add_executable(edge_ecu_sub ${ACTUATION_DIR}/test/dds_sub.cpp)
target_link_libraries(edge_ecu_sub PRIVATE CycloneDDS::ddsc autoware_msgs)
target_include_directories(edge_ecu_sub PRIVATE
    ${ACTUATION_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)
