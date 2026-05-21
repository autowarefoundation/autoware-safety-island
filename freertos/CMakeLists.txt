# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Top-level CMake for the FreeRTOS POSIX simulator. The output is a
# single native executable that runs the actuation controller on top of
# FreeRTOS-Kernel (POSIX port).

cmake_minimum_required(VERSION 3.22)
project(actuation_freertos C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(REPO_ROOT "${CMAKE_SOURCE_DIR}/..")
set(KERNEL_DIR "${REPO_ROOT}/external/FreeRTOS-Kernel")

# --- FreeRTOS-Kernel (POSIX port) -------------------------------------
add_library(freertos_kernel STATIC
  ${KERNEL_DIR}/tasks.c
  ${KERNEL_DIR}/queue.c
  ${KERNEL_DIR}/list.c
  ${KERNEL_DIR}/timers.c
  ${KERNEL_DIR}/event_groups.c
  ${KERNEL_DIR}/stream_buffer.c
  ${KERNEL_DIR}/portable/MemMang/heap_4.c
  ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/port.c
  ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
)
target_include_directories(freertos_kernel PUBLIC
  ${KERNEL_DIR}/include
  ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix
  ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils
  ${CMAKE_CURRENT_SOURCE_DIR}    # for FreeRTOSConfig.h
)
target_link_libraries(freertos_kernel PUBLIC pthread)

# --- Hello-world gate target (Stage 0) --------------------------------
add_executable(hello_freertos
  main_freertos.c
  hello_task.c
)
target_link_libraries(hello_freertos PRIVATE freertos_kernel)
