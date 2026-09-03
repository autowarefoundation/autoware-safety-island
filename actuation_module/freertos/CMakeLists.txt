# Copyright (c) 2025, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# FreeRTOS POSIX runtime build entry point.
#
# Preferred path from the repository root:
#   ./build.sh --platform freertos-posix -d build/freertos-posix
#
# Prerequisites for manual CMake use: build CycloneDDS host tools and target
# library first. build.sh performs these phases automatically.
#
# Phase 1 -- CycloneDDS host tools (provides idlc):
#   cmake cyclonedds -B build/cyclonedds_host \
#     -DBUILD_IDLC=ON -DBUILD_SHARED_LIBS=ON \
#     -DCMAKE_INSTALL_PREFIX=$PWD/build/cyclonedds_host/out \
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/cyclonedds_host --target install -j$(nproc)
#
# Phase 2 -- CycloneDDS target library. ddsperf must be disabled, otherwise its
# CMake configuration invokes idlc at configure time and fails in a clean
# environment where idlc is not on PATH:
#   cmake cyclonedds -B build/cyclonedds_target \
#     -DBUILD_SHARED_LIBS=OFF -DENABLE_SECURITY=OFF \
#     -DENABLE_SSL=OFF -DENABLE_SHM=OFF -DENABLE_IPV6=OFF -DBUILD_IDLC=OFF \
#     -DBUILD_DDSPERF=OFF \
#     -DCMAKE_INSTALL_PREFIX=$PWD/build/cyclonedds_target_out -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/cyclonedds_target --target install -j$(nproc)
#
# Phase 3 -- the runtime. autoware_msgs needs the Phase 1 idlc on PATH:
#   export PATH=$PWD/build/cyclonedds_host/out/bin:$PATH
#   export LD_LIBRARY_PATH=$PWD/build/cyclonedds_host/out/lib:${LD_LIBRARY_PATH:-}
#   cmake actuation_module/freertos -B build/freertos-posix \
#     -DCDDS_HOST_PREFIX=$PWD/build/cyclonedds_host/out \
#     -DCDDS_TARGET_PREFIX=$PWD/build/cyclonedds_target_out
#   cmake --build build/freertos-posix -j$(nproc)

cmake_minimum_required(VERSION 3.22.1)
project(actuation_module_freertos LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Root of actuation_module source tree
set(ACTUATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Root of the repository
set(REPO_ROOT ${ACTUATION_DIR}/..)
# Pre-built CycloneDDS locations
set(CDDS_HOST_PREFIX ${REPO_ROOT}/build/cyclonedds_host/out CACHE PATH "CycloneDDS host tools install prefix")
set(CDDS_TARGET_PREFIX ${REPO_ROOT}/build/cyclonedds_target_out CACHE PATH "CycloneDDS target library install prefix")

# ---- Platform definition ----
set(PLATFORM_FREERTOS ON)
add_compile_definitions(PLATFORM_FREERTOS=1 PLATFORM_FREERTOS_POSIX=1)

# ---- Configurable values ----
# These defaults mirror Zephyr Kconfig unless noted otherwise.
set(CONFIG_LOG_LEVEL 1 CACHE STRING "Log level (0=off, 1=info, 2=debug)")
set(CONFIG_LOG_THROTTLE_RATE 3 CACHE STRING "Log throttle rate in seconds")
set(CONFIG_DDS_DOMAIN_ID 2 CACHE STRING "DDS domain ID")
# The POSIX simulator runs on a Linux host. Default to loopback so CI and
# local smoke runs work without extra configuration; override with
# -DCONFIG_DDS_NETWORK_INTERFACE=<iface> when running against an external peer.
set(CONFIG_DDS_NETWORK_INTERFACE "lo" CACHE STRING "DDS network interface name")
set(CONFIG_DDS_LOG_LEVEL 0 CACHE STRING "DDS log level (0=off, 1=warn, 2=debug)")
set(CONFIG_THREAD_STACK_SIZE 262144 CACHE STRING "Thread stack size in bytes")
set(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX 0 CACHE STRING "DHCP delay (0 for simulator)")
set(CONFIG_ENABLE_SNTP OFF CACHE BOOL "Enable SNTP time sync")

# Runtime default is DDS_ONLY. SocketCAN is opt-in via --control-output and
# SAFETY_ISLAND_CAN_IFACE. The in-memory mock is compiled only for --can-output-test.
set(CONFIG_CONTROL_CMD_OUTPUT_MODE "DDS_ONLY" CACHE STRING "Control command output mode: DDS_ONLY, CAN_ONLY, DDS_AND_CAN")
set_property(CACHE CONFIG_CONTROL_CMD_OUTPUT_MODE PROPERTY STRINGS "DDS_ONLY" "CAN_ONLY" "DDS_AND_CAN")

set(CONFIG_CONTROL_CMD_OUTPUT_DDS_ONLY 0)
set(CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY 0)
set(CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN 0)
set(CONFIG_CONTROL_CMD_CAN_OUTPUT 0)
set(CONFIG_CONTROL_CMD_DDS_OUTPUT 0)

if(CONFIG_CONTROL_CMD_OUTPUT_MODE STREQUAL "DDS_ONLY")
    set(CONFIG_CONTROL_CMD_OUTPUT_DDS_ONLY 1)
    set(CONFIG_CONTROL_CMD_DDS_OUTPUT 1)
elseif(CONFIG_CONTROL_CMD_OUTPUT_MODE STREQUAL "CAN_ONLY")
    set(CONFIG_CONTROL_CMD_OUTPUT_CAN_ONLY 1)
    set(CONFIG_CONTROL_CMD_CAN_OUTPUT 1)
elseif(CONFIG_CONTROL_CMD_OUTPUT_MODE STREQUAL "DDS_AND_CAN")
    set(CONFIG_CONTROL_CMD_OUTPUT_DDS_AND_CAN 1)
    set(CONFIG_CONTROL_CMD_CAN_OUTPUT 1)
    set(CONFIG_CONTROL_CMD_DDS_OUTPUT 1)
else()
    message(FATAL_ERROR "Unsupported CONFIG_CONTROL_CMD_OUTPUT_MODE=${CONFIG_CONTROL_CMD_OUTPUT_MODE}. Use DDS_ONLY, CAN_ONLY, or DDS_AND_CAN.")
endif()

# Generate platform config header
configure_file(
    ${ACTUATION_DIR}/include/platform/freertos/freertos_config_generated.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/freertos_config_generated.h
    @ONLY
)

# ---- FreeRTOS Kernel ----
set(FREERTOS_KERNEL_DIR ${REPO_ROOT}/freertos-kernel)

if(NOT EXISTS ${FREERTOS_KERNEL_DIR}/include/FreeRTOS.h)
    message(FATAL_ERROR
        "FreeRTOS-Kernel not found at ${FREERTOS_KERNEL_DIR}. "
        "Run: git submodule add https://github.com/FreeRTOS/FreeRTOS-Kernel.git freertos-kernel "
        "&& cd freertos-kernel && git checkout V11.1.0")
endif()

add_library(freertos_kernel STATIC
    ${FREERTOS_KERNEL_DIR}/croutine.c
    ${FREERTOS_KERNEL_DIR}/event_groups.c
    ${FREERTOS_KERNEL_DIR}/list.c
    ${FREERTOS_KERNEL_DIR}/queue.c
    ${FREERTOS_KERNEL_DIR}/stream_buffer.c
    ${FREERTOS_KERNEL_DIR}/tasks.c
    ${FREERTOS_KERNEL_DIR}/timers.c
    ${FREERTOS_KERNEL_DIR}/portable/ThirdParty/GCC/Posix/port.c
    ${FREERTOS_KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    ${FREERTOS_KERNEL_DIR}/portable/MemMang/heap_3.c
)
target_include_directories(freertos_kernel PUBLIC
    ${FREERTOS_KERNEL_DIR}/include
    ${FREERTOS_KERNEL_DIR}/portable/ThirdParty/GCC/Posix
    ${ACTUATION_DIR}/include/platform/freertos  # FreeRTOSConfig.h
)
target_compile_options(freertos_kernel PRIVATE -Wno-cast-qual)
target_link_libraries(freertos_kernel PUBLIC pthread)

# ---- CycloneDDS (pre-built) ----
# Verify pre-built CycloneDDS exists
if(NOT EXISTS ${CDDS_HOST_PREFIX}/bin/idlc)
    message(FATAL_ERROR "CycloneDDS host tools not found. Build them first. See comments at top of this file.")
endif()
if(NOT EXISTS ${CDDS_TARGET_PREFIX}/lib/libddsc.a)
    message(FATAL_ERROR "CycloneDDS target library not found. Build it first. See comments at top of this file.")
endif()

# Import pre-built CycloneDDS target library
add_library(cdds STATIC IMPORTED GLOBAL)
set_target_properties(cdds PROPERTIES IMPORTED_LOCATION ${CDDS_TARGET_PREFIX}/lib/libddsc.a)

set(CYCLONEDDS_INCLUDE_DIR ${CDDS_TARGET_PREFIX}/include)

# Make idlc available for autoware_msgs
set(ENV{PATH} "${CDDS_HOST_PREFIX}/bin:$ENV{PATH}")
set(ENV{LD_LIBRARY_PATH} "${CDDS_HOST_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")
set(CycloneDDS_DIR ${CDDS_HOST_PREFIX}/lib/cmake/CycloneDDS)

# ---- Eigen3 ----
if(NOT BUILD_TEST STREQUAL "4")
    find_package(Eigen3 REQUIRED)
else()
    set(EIGEN3_INCLUDE_DIR "")
endif()

# ---- Autoware Messages ----
add_subdirectory(${ACTUATION_DIR}/src/autoware/autoware_msgs ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

# ---- Autoware Components (shared core) ----
if(NOT BUILD_TEST STREQUAL "4")
    include(${ACTUATION_DIR}/cmake/actuation_core.cmake)
else()
    set(APP_SOURCES "")
    set(APP_INCLUDE_DIRS "")
endif()

# ---- Rename main() in actuation src to actuation_main() ----
# The FreeRTOS wrapper provides main() and calls actuation_main() as a task.
add_compile_definitions(main=actuation_main)

# ---- Build target selection ----
if(BUILD_TEST STREQUAL "1")
    set(ACTUATION_MAIN_SRC ${ACTUATION_DIR}/test/unit_test.cpp)
elseif(BUILD_TEST STREQUAL "2")
    set(ACTUATION_MAIN_SRC ${ACTUATION_DIR}/test/dds_pub.cpp)
elseif(BUILD_TEST STREQUAL "3")
    set(ACTUATION_MAIN_SRC ${ACTUATION_DIR}/test/dds_sub.cpp)
elseif(BUILD_TEST STREQUAL "4")
    set(ACTUATION_MAIN_SRC ${ACTUATION_DIR}/test/can_output_test.cpp)
else()
    set(ACTUATION_MAIN_SRC ${ACTUATION_DIR}/src/main.cpp)
endif()

# ---- Executable ----
add_executable(actuation_freertos
    ${CMAKE_CURRENT_SOURCE_DIR}/freertos_main.cpp
    ${ACTUATION_MAIN_SRC}
    ${APP_SOURCES}
)

# Undo the main rename for the FreeRTOS wrapper itself
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/freertos_main.cpp PROPERTIES
    COMPILE_DEFINITIONS "main=main"
)

target_include_directories(actuation_freertos PRIVATE
    ${ACTUATION_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CYCLONEDDS_INCLUDE_DIR}
    ${EIGEN3_INCLUDE_DIR}
    ${APP_INCLUDE_DIRS}
)

target_link_libraries(actuation_freertos PRIVATE
    freertos_kernel
    cdds
    autoware_msgs
    pthread
)

target_compile_options(actuation_freertos PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -fexceptions -frtti
)

if(BUILD_TEST STREQUAL "4")
    target_compile_definitions(actuation_freertos PRIVATE PLATFORM_FREERTOS_CAN_MOCK=1)
endif()

add_executable(can_vcan_roundtrip ${ACTUATION_DIR}/test/can_vcan_roundtrip.cpp)
set_source_files_properties(${ACTUATION_DIR}/test/can_vcan_roundtrip.cpp PROPERTIES
    COMPILE_DEFINITIONS "main=main"
)
target_include_directories(can_vcan_roundtrip PRIVATE
    ${ACTUATION_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CYCLONEDDS_INCLUDE_DIR}
)
target_link_libraries(can_vcan_roundtrip PRIVATE autoware_msgs pthread)
target_compile_options(can_vcan_roundtrip PRIVATE -Wall -Wextra -Wno-unused-parameter -fexceptions)

# ---- Host-compiled rpmsg_netif_core unit test (Task 5) ----
# rpmsg_netif_core.{c,h} is deliberately OS-free, lwIP-free C so its framing
# policy (bounds checks, stat counters, delegation to the tx/rx_deliver
# function pointers) can be exercised directly on the build host -- it needs
# neither CycloneDDS nor the FreeRTOS kernel that actuation_freertos links.
# Built alongside unit_test.cpp under the same BUILD_TEST=1 selector so
# `build.sh --platform freertos-posix --unit-test` (the command CI already
# runs) also builds and runs this test, without a second developer-facing
# flag to remember.
if(BUILD_TEST STREQUAL "1")
    set(RPMSG_NETIF_CORE_TEST_MAIN ${ACTUATION_DIR}/test/test_rpmsg_netif_core.c)

    add_executable(rpmsg_netif_core_test
        ${ACTUATION_DIR}/freertos_x5h/rpmsg_netif_core.c
        ${RPMSG_NETIF_CORE_TEST_MAIN}
    )

    # This directory redefines main -> actuation_main (above) so the
    # C++ test programs' main() can run as a FreeRTOS task inside
    # actuation_freertos. This test is a separate, plain-C host executable
    # with its own real main(), so undo that redefinition here -- the same
    # way freertos_main.cpp undoes it for the FreeRTOS wrapper's own main().
    set_source_files_properties(${RPMSG_NETIF_CORE_TEST_MAIN} PROPERTIES
        COMPILE_DEFINITIONS "main=main"
    )

    target_include_directories(rpmsg_netif_core_test PRIVATE
        ${ACTUATION_DIR}/freertos_x5h
    )

    target_compile_options(rpmsg_netif_core_test PRIVATE -Wall -Wextra)

    # Run as part of the build itself (not a separately-remembered ctest
    # invocation) so a regression fails the same `cmake --build` that
    # `build.sh --unit-test` performs.
    add_custom_command(TARGET rpmsg_netif_core_test POST_BUILD
        COMMAND rpmsg_netif_core_test
        COMMENT "Running rpmsg_netif_core host unit test"
        VERBATIM
    )
endif()
