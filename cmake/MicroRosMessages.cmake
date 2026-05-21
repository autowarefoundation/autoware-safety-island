# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# generate_autoware_message_package(<pkg_name>
#   SOURCE_DIR <abs_dir>     # directory containing msg/*.msg files
#   DEPENDS <pkg1> <pkg2> ...  # other generated message packages this depends on
# )
#
# Drives rosidl_generator_c and rosidl_generator_cpp on the .msg files
# in <SOURCE_DIR>/msg/. Output goes to
#   ${CMAKE_BINARY_DIR}/gen/<pkg>/include/<pkg>/msg/*.h         (C)
#   ${CMAKE_BINARY_DIR}/gen/<pkg>/include/<pkg>/msg/*.hpp       (C++)
# and creates an INTERFACE library named <pkg> that propagates the include dir.

set(ROSIDL_DIR "${CMAKE_SOURCE_DIR}/external/rosidl"
    CACHE PATH "Path to ros2/rosidl checkout")
set(ROSIDL_VENV "${CMAKE_SOURCE_DIR}/.venv-rosidl"
    CACHE PATH "Host Python venv with rosidl tooling")

function(generate_autoware_message_package pkg)
  cmake_parse_arguments(ARG "" "SOURCE_DIR" "DEPENDS" ${ARGN})
  if(NOT ARG_SOURCE_DIR)
    message(FATAL_ERROR "generate_autoware_message_package(${pkg}): SOURCE_DIR required")
  endif()

  file(GLOB MSG_FILES "${ARG_SOURCE_DIR}/msg/*.msg")
  if(NOT MSG_FILES)
    message(FATAL_ERROR "generate_autoware_message_package(${pkg}): no .msg files in ${ARG_SOURCE_DIR}/msg/")
  endif()

  set(OUT_DIR "${CMAKE_BINARY_DIR}/gen/${pkg}/include/${pkg}/msg")
  file(MAKE_DIRECTORY "${OUT_DIR}")

  set(GENERATED_HEADERS "")
  foreach(msg IN LISTS MSG_FILES)
    get_filename_component(base "${msg}" NAME_WE)
    string(TOLOWER "${base}" base_lower)
    list(APPEND GENERATED_HEADERS
      "${OUT_DIR}/${base_lower}.h"
      "${OUT_DIR}/${base_lower}.hpp"
    )
  endforeach()

  # The standalone driver script lives at scripts/run_rosidl_generator.py
  # and accepts: --pkg <name> --in-dir <dir> --out-dir <dir> --lang c|cpp|both.
  add_custom_command(
    OUTPUT ${GENERATED_HEADERS}
    COMMAND "${ROSIDL_VENV}/bin/python"
            "${CMAKE_SOURCE_DIR}/scripts/run_rosidl_generator.py"
            --pkg "${pkg}"
            --in-dir "${ARG_SOURCE_DIR}"
            --out-dir "${CMAKE_BINARY_DIR}/gen/${pkg}"
            --rosidl-dir "${ROSIDL_DIR}"
            --lang both
    DEPENDS ${MSG_FILES}
    COMMENT "rosidl: generating ${pkg} (C and C++)"
    VERBATIM
  )

  add_custom_target(${pkg}_gen DEPENDS ${GENERATED_HEADERS})

  add_library(${pkg} INTERFACE)
  add_dependencies(${pkg} ${pkg}_gen)
  target_include_directories(${pkg} INTERFACE
    "${CMAKE_BINARY_DIR}/gen/${pkg}/include"
  )

  foreach(dep IN LISTS ARG_DEPENDS)
    target_link_libraries(${pkg} INTERFACE ${dep})
  endforeach()
endfunction()
