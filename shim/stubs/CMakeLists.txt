# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Stub headers for upstream-Autoware-isms that are pruned from the
# safety-island controller path (tf2, diagnostic_updater, visualization_msgs,
# autoware_utils). The stubs preserve the symbol surface that vendored
# upstream sources reference at compile time; their runtime behaviour is
# either trivial math (tf2) or a no-op (everything else).

add_library(autoware_stubs INTERFACE)
target_include_directories(autoware_stubs INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
