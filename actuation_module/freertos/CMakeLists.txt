# Copyright (c) 2025, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# FreeRTOS POSIX simulator build entry point.
#
# Prerequisites: build CycloneDDS host tools and target library first:
#   mkdir -p build-freertos/cyclonedds_host && cd build-freertos/cyclonedds_host
#   cmake ../../cyclonedds -DBUILD_IDLC=ON -DBUILD_SHARED_LIBS=ON \
#     -DCMAKE_INSTALL_PREFIX=$(pwd)/../cdds_host_out -DCMAKE_BUILD_TYPE=Release
#   make -j$(nproc) install
#   cd .. && mkdir cyclonedds_target && cd cyclonedds_target
#   cmake ../../cyclonedds -DBUILD_SHARED_LIBS=OFF -DENABLE_SECURITY=OFF \
#     -DENABLE_SSL=OFF -DENABLE_SHM=OFF -DENABLE_IPV6=OFF -DBUILD_IDLC=OFF \
#     -DCMAKE_INSTALL_PREFIX=$(pwd)/../cdds_target_out -DCMAKE_BUILD_TYPE=Debug
#   make -j$(nproc) install
#
# Then build the simulator:
#   cd build-freertos && mkdir app && cd app
#   cmake ../../actuation_module/freertos
#   make -j$(nproc)

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
set(CDDS_HOST_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/../cdds_host_out CACHE PATH "CycloneDDS host tools install prefix")
set(CDDS_TARGET_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/../cdds_target_out CACHE PATH "CycloneDDS target library install prefix")

# ---- Platform definition ----
set(PLATFORM_FREERTOS ON)
add_compile_definitions(PLATFORM_FREERTOS=1)

# ---- Configurable values (same defaults as Zephyr Kconfig) ----
set(CONFIG_LOG_LEVEL 1 CACHE STRING "Log level (0=off, 1=info, 2=debug)")
set(CONFIG_LOG_THROTTLE_RATE 3 CACHE STRING "Log throttle rate in seconds")
set(CONFIG_DDS_DOMAIN_ID 2 CACHE STRING "DDS domain ID")
set(CONFIG_DDS_NETWORK_INTERFACE "" CACHE STRING "DDS network interface name (empty=auto)")
set(CONFIG_DDS_LOG_LEVEL 0 CACHE STRING "DDS log level (0=off, 1=warn, 2=debug)")
set(CONFIG_THREAD_STACK_SIZE 262144 CACHE STRING "Thread stack size in bytes")
set(CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX 0 CACHE STRING "DHCP delay (0 for simulator)")
set(CONFIG_ENABLE_SNTP OFF CACHE BOOL "Enable SNTP time sync")

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
find_package(Eigen3 REQUIRED)

# ---- Autoware Messages ----
add_subdirectory(${ACTUATION_DIR}/src/autoware/autoware_msgs ${CMAKE_CURRENT_BINARY_DIR}/autoware_msgs)

# ---- Autoware Components (shared core) ----
include(${ACTUATION_DIR}/cmake/actuation_core.cmake)

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
