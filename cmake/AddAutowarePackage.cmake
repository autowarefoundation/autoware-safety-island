# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# add_autoware_package(<name> SUBDIR <relative-to-external/autoware_universe>)
#
# Wraps add_subdirectory() with AutowarePackageCompat preloaded into the
# package's scope.

include("${CMAKE_CURRENT_LIST_DIR}/AutowarePackageCompat.cmake")
autoware_package_compat_setup()

function(add_autoware_package name)
  cmake_parse_arguments(ARG "" "SUBDIR" "" ${ARGN})
  # Locate the repo root relative to this file (cmake/ → parent).
  get_filename_component(_aaup_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  set(src_dir "${_aaup_repo_root}/external/autoware_universe/${ARG_SUBDIR}")
  set(bin_dir "${CMAKE_BINARY_DIR}/autoware_universe/${name}")
  if(NOT EXISTS "${src_dir}/CMakeLists.txt")
    message(FATAL_ERROR "add_autoware_package(${name}): ${src_dir}/CMakeLists.txt not found")
  endif()
  add_subdirectory("${src_dir}" "${bin_dir}" EXCLUDE_FROM_ALL)
endfunction()
