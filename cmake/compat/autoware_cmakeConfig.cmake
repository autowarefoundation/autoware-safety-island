# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Stub for autoware_cmake. The real package provides autoware_package() and
# ament_auto_add_library() macros on top of ament_cmake. We provide minimal
# no-op / forwarding versions here.

set(autoware_cmake_FOUND TRUE)

# autoware_package() — calls ament_package() under the hood.
macro(autoware_package)
  ament_package()
endmacro()

# ament_auto_add_library(<target> [SHARED|STATIC] <sources...>)
# Collects all sources and calls plain add_library(). Like the real macro,
# also adds include/ from the package source tree as a public include dir.
function(ament_auto_add_library target)
  set(options SHARED STATIC MODULE)
  cmake_parse_arguments(AAAL "${options}" "" "" ${ARGN})
  if(AAAL_SHARED)
    set(_lib_type SHARED)
  elseif(AAAL_STATIC)
    set(_lib_type STATIC)
  elseif(AAAL_MODULE)
    set(_lib_type MODULE)
  else()
    set(_lib_type SHARED)
  endif()
  add_library(${target} ${_lib_type} ${AAAL_UNPARSED_ARGUMENTS})
  # Replicate ament_auto behaviour: expose include/ directory publicly.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_include_directories(${target} PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
  endif()
endfunction()

# ament_auto_add_executable(<target> <sources...>)
function(ament_auto_add_executable target)
  add_executable(${target} ${ARGN})
endfunction()

# ament_auto_package() — no-op; calls ament_package() equivalent.
macro(ament_auto_package)
  ament_package()
endmacro()
