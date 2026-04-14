# Copyright (c) 2025, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Shared source and include path definitions for the actuation module.
# Included by both Zephyr and FreeRTOS CMake builds.
#
# The autoware component .cmake files use paths relative to actuation_module/.
# ACTUATION_MODULE_DIR must be set before including this file.

set(ACTUATION_MODULE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

# Save and override CMAKE_CURRENT_SOURCE_DIR so that component .cmake files
# resolve their relative paths correctly (they assume CWD = actuation_module/).
set(_SAVED_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_CURRENT_SOURCE_DIR ${ACTUATION_MODULE_DIR})

set(AUTOWARE_COMPONENTS
    autoware_universe_utils
    autoware_interpolation
    autoware_motion_utils
    autoware_vehicle_info_utils
    autoware_trajectory_follower_base
    autoware_mpc_lateral_controller
    autoware_pid_longitudinal_controller
    autoware_trajectory_follower_node
)

set(APP_SOURCES "")
set(APP_INCLUDE_DIRS "")

foreach(component IN LISTS AUTOWARE_COMPONENTS)
    include(${ACTUATION_MODULE_DIR}/src/autoware/${component}/${component}.cmake)
endforeach()

# Prefix all source paths with ACTUATION_MODULE_DIR since they're relative
list(TRANSFORM APP_SOURCES PREPEND "${ACTUATION_MODULE_DIR}/")
list(TRANSFORM APP_INCLUDE_DIRS PREPEND "${ACTUATION_MODULE_DIR}/")

# Restore
set(CMAKE_CURRENT_SOURCE_DIR ${_SAVED_CURRENT_SOURCE_DIR})
