# Copyright (c) 2022-2023, Arm Limited.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.12)

project(autoware_msgs)

# idlc is a host tool, and doesn't care about the architecture of the target platform.
# Prevent cmake from checking the architecture by temporarily unsetting CMAKE_SIZEOF_VOID_P.
set(TMP_CMAKE_SIZEOF_VOID_P ${CMAKE_SIZEOF_VOID_P})
unset(CMAKE_SIZEOF_VOID_P)
find_package(CycloneDDS REQUIRED COMPONENTS ddsc idlc)
set(CMAKE_SIZEOF_VOID_P ${TMP_CMAKE_SIZEOF_VOID_P})

file(GLOB IDL_FILES msg/*.idl)
list(TRANSFORM IDL_FILES REPLACE "\\.idl" ".c" OUTPUT_VARIABLE SOURCES)
list(TRANSFORM SOURCES REPLACE ".*/" "${CMAKE_CURRENT_BINARY_DIR}/")
list(TRANSFORM SOURCES REPLACE "\\.c$" ".h" OUTPUT_VARIABLE HEADERS)

execute_process(
  COMMAND idlc -v
  OUTPUT_VARIABLE IDLC_VERSION_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE IDLC_VERSION_RESULT
)

if(NOT IDLC_VERSION_RESULT EQUAL 0)
  message(WARNING "Failed to get idlc version. Exit code: ${IDLC_VERSION_RESULT}")
else()
  message(STATUS "IDLC Version: ${IDLC_VERSION_OUTPUT}")
endif()

# Generate source files and headers for the DDS messages.
# Edit out potential absolute path to the source .idl from the resulting .c and .h files.
add_custom_command(
  OUTPUT ${SOURCES} ${HEADERS}
  COMMAND /bin/sh -c "find ${CMAKE_CURRENT_SOURCE_DIR}/msg/ -maxdepth 1 -name '*.idl' -exec idlc {} \\; && sed -i -- 's|/\\(.\\+\\)/\\(.*\\.idl\\)|\\2|' *.h *.c"
  DEPENDS ${IDL_FILES}
  VERBATIM
)

# --- ORIGINAL ---
# add_library(${PROJECT_NAME}
#   ${SOURCES}
#   ${HEADERS}
# )
# set_target_properties(${PROJECT_NAME} PROPERTIES
#   LINKER_LANGUAGE CXX
# )

# --- NEW VERSION (Zephyr-friendly ABI build) ---
zephyr_library_named(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES} ${HEADERS})

get_target_property(CycloneDDS_INCLUDE_DIR CycloneDDS::ddsc INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(${PROJECT_NAME} SYSTEM BEFORE PUBLIC
  ${CycloneDDS_INCLUDE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# --- ORIGINAL INSTALL (commented, because Zephyr libs are not getting) ---
# install(TARGETS ${PROJECT_NAME} DESTINATION lib)
# install(
#   FILES ${HEADERS}
#   DESTINATION include/autoware/autoware_msgs
# )
# install(
#   DIRECTORY include/autoware/
#   DESTINATION include/autoware
# )
