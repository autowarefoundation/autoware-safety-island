# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Standalone micro-ROS build wiring.
#
# Layers (bottom-up, each depends on the layer below):
#   1. micro_cdr            (eProsima/micro-CDR)             — wire-format CDR
#   2. microxrcedds_client  (eProsima/Micro-XRCE-DDS-Client) — XRCE-DDS client
#   3. rcutils              (ros2/rcutils)                   — base C utils
#   4. rmw                  (ros2/rmw)                       — RMW abstract API
#   5. rmw_microxrcedds_c   (micro-ROS/rmw_microxrcedds)     — RMW on XRCE
#   6. rcl                  (ros2/rcl)                       — Client API
#   7. rclc                 (ros2/rclc)                      — Executor helpers
#
# Current scope (this PR / Stage 2-4): vendoring + bottom layer only.
#
# Layer 1 (`microcdr` static lib) builds cleanly via add_subdirectory.
# Layer 2 (`microxrcedds_client`) ships its sources via a superbuild
# (ExternalProject_Add of microcdr followed by a recursive cmake
# invocation), which doesn't compose with add_subdirectory because the
# inner project does `find_package(microcdr ${VERSION} EXACT REQUIRED)`
# expecting an install tree. Wiring it up requires either:
#   (a) producing an install tree of micro_cdr first and pointing
#       CMAKE_PREFIX_PATH at it before configuring the client, or
#   (b) writing a thin Config-file shim under cmake/compat/ that exposes
#       a `microcdr-VERSION.cmake` consumable by the client's
#       find_package(... EXACT REQUIRED) check.
# Layers 3-7 (rcutils/rmw/rmw_microxrcedds_c/rcl/rclc) further need
# generated *-extras.cmake and config headers that ament normally
# produces; standalone scaffolding for those is iterative work
# consciously deferred to the next stack PR.

set(MR_ROOT "${CMAKE_SOURCE_DIR}/../external/micro_ros")

# ── Layer 1: micro-CDR ────────────────────────────────────────────────────
# Disable superbuild so the `microcdr` target is created directly in this
# build tree instead of being delegated to an ExternalProject.
set(UCDR_SUPERBUILD OFF CACHE BOOL "" FORCE)
set(UCDR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UCDR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${MR_ROOT}/micro-CDR micro_cdr EXCLUDE_FROM_ALL)

# Marker for downstream code: layer 1 is available, higher layers TBD.
set(MICROROS_LAYER_MICROCDR_AVAILABLE TRUE)
