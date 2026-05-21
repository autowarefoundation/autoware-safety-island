# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Compatibility shim for building selected autoware.universe packages
# (which use ament_cmake) under the actuation_module Zephyr build (which
# uses bare CMake). Each function below is the minimum needed so that
# package CMakeLists.txt files complete configure without modification.

# Marker so packages can detect they are being built in compat mode.
set(AUTOWARE_PACKAGE_COMPAT TRUE)

# --- ament_cmake stubs --------------------------------------------------

# ament_package() — emits package registration in upstream Autoware.
# Here we just record the package name for diagnostics.
function(ament_package)
  if(NOT DEFINED PROJECT_NAME)
    message(WARNING "ament_package() called before project()")
  endif()
  message(STATUS "AutowarePackageCompat: ament_package() for ${PROJECT_NAME}")
endfunction()

# ament_target_dependencies(<target> [PUBLIC|PRIVATE|INTERFACE] dep1 dep2 ...)
# Maps to plain target_link_libraries() and target_include_directories()
# using the variables that find_package() sets (or that our compat
# find_package() shim sets).
function(ament_target_dependencies target)
  set(visibility "PUBLIC")
  set(deps "")
  foreach(arg IN LISTS ARGN)
    if(arg STREQUAL "PUBLIC" OR arg STREQUAL "PRIVATE" OR arg STREQUAL "INTERFACE")
      set(visibility "${arg}")
    else()
      list(APPEND deps "${arg}")
    endif()
  endforeach()
  foreach(dep IN LISTS deps)
    if(TARGET ${dep})
      target_link_libraries(${target} ${visibility} ${dep})
    elseif(DEFINED ${dep}_LIBRARIES OR DEFINED ${dep}_INCLUDE_DIRS)
      target_include_directories(${target} ${visibility} ${${dep}_INCLUDE_DIRS})
      target_link_libraries(${target} ${visibility} ${${dep}_LIBRARIES})
    else()
      message(STATUS "AutowarePackageCompat: skipping unknown dep '${dep}'")
    endif()
  endforeach()
endfunction()

# ament_export_* — no-ops; we do not produce ament install artifacts.
function(ament_export_targets)
endfunction()
function(ament_export_dependencies)
endfunction()
function(ament_export_include_directories)
endfunction()
function(ament_export_libraries)
endfunction()
function(ament_export_definitions)
endfunction()

# ament_lint_auto_find_test_dependencies — no-ops (we do not run ament tests).
function(ament_lint_auto_find_test_dependencies)
endfunction()

# Install rules from upstream: redirect to ${CMAKE_BINARY_DIR}/install for inspection only.
# Upstream uses install(DIRECTORY include/ DESTINATION include) etc.; the
# default CMake install() works for our needs.
# This file is meant to be include()d at the *top* of autoware.universe-built
# packages before they call find_package(ament_cmake REQUIRED). The
# AddAutowarePackage.cmake helper does that include() before add_subdirectory().

# --- find_package() interception ---------------------------------------
#
# Upstream packages call find_package(<X> REQUIRED) for ament_cmake,
# rclcpp, rosidl_default_runtime, etc. We can't easily intercept the
# find_package() built-in, so we instead provide Config files on the
# CMAKE_MODULE_PATH for each.
#
# These are added under cmake/compat/ — one <Name>Config.cmake per
# faked package — and the parent CMakeLists.txt is responsible for
# prepending cmake/compat/ to CMAKE_PREFIX_PATH before doing
# add_subdirectory() on an upstream package.

set(AUTOWARE_PACKAGE_COMPAT_DIR "${CMAKE_CURRENT_LIST_DIR}/compat" CACHE PATH "")

# Convenience helper. Call from the top-level CMakeLists.txt once.
function(autoware_package_compat_setup)
  list(PREPEND CMAKE_PREFIX_PATH "${AUTOWARE_PACKAGE_COMPAT_DIR}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
endfunction()
