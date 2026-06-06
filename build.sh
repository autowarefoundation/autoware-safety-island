#! /usr/bin/env bash

# Copyright (c) 2025, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Build script for the Zephyr Actuation Module
#
# This script builds the Zephyr Actuation Module for the specified target board.
#
# Usage: ./build.sh [OPTIONS]

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

# Root directory
ROOT_DIR=$(dirname "$(realpath "$0")")
set -e
set -u

# Build options
BUILD_TEST_FLAG=0
BUILD_DIR="build/actuation_module"
ZEPHYR_TARGET_LIST=("fvp_baser_aemv8r_smp" "s32z270dc2_rtu0_r52@D")
ZEPHYR_TARGET=${ZEPHYR_TARGET_LIST[0]} # Default target is fvp_baser_aemv8r_smp

function usage() {
  echo -e "${GREEN}Usage: $0 [OPTIONS]${NC}"
  echo -e "------------------------------------------------"
  echo -e "${GREEN}    -t                 ${NC}Zephyr target board: ${ZEPHYR_TARGET_LIST[*]}"
  echo -e "${GREEN}                         default: ${ZEPHYR_TARGET_LIST[0]}.${NC}"
  echo -e "${GREEN}    -d                 ${NC}Build directory. Default: ${BUILD_DIR}."
  echo -e "${GREEN}    -c                 ${NC}Clean all builds and exit."
  echo -e "${GREEN}    -h                 ${NC}Display the usage and exit."
  echo ""
  echo -e "${GREEN}    Optional arguments to build Zephyr test programs:${NC}"
  echo -e "${GREEN}    --unit-test        ${NC}Build Zephyr unit test program."
  echo -e "${GREEN}    --dds-publisher    ${NC}Build Zephyr DDS publisher."
  echo -e "${GREEN}    --dds-subscriber   ${NC}Build Zephyr DDS subscriber."
  echo -e "${GREEN}    --can-output-test  ${NC}Build Zephyr CAN output test program."
  echo -e "${GREEN}    --dds-loopback-test${NC}Build Zephyr DDS loopback test program."
}

function parse_args() {
  new_args=()
  for arg in "$@"; do
    case $arg in
      --help)
        usage
        exit 0
        ;;
      --unit-test)
        BUILD_TEST_FLAG=1
        shift
        ;;
      --dds-publisher)
        BUILD_TEST_FLAG=2
        shift
        ;;
      --dds-subscriber)
        BUILD_TEST_FLAG=3
        shift
        ;;
      --can-output-test)
        BUILD_TEST_FLAG=4
        shift
        ;;
      --dds-loopback-test)
        BUILD_TEST_FLAG=5
        shift
        ;;
      *)
        new_args+=("$arg")
        ;;
    esac
  done
  set -- "${new_args[@]}" # Reset the positional parameters to the remaining arguments

  while getopts "t:d:ch" opt; do
    case ${opt} in
      t )
        ZEPHYR_TARGET=""
        for t in "${ZEPHYR_TARGET_LIST[@]}"; do
          if [ "${t}" = "${OPTARG}" ]; then
            ZEPHYR_TARGET=${t}
            break
          fi
        done
        if [ -z "${ZEPHYR_TARGET}" ]; then
          echo -e "${RED}Invalid Zephyr target: ${OPTARG}${NC}\n" 1>&2
          echo -e "${YELLOW}Valid targets: ${ZEPHYR_TARGET_LIST[*]}${NC}" 1>&2
          exit 1
        fi
        ;;
      d )
        BUILD_DIR=${OPTARG}
        ;;
      c )
        clean
        exit 0
        ;;
      h )
        usage
        exit 0
        ;;
      \? )
        echo -e "${RED}Invalid option: ${OPTARG}${NC}\n" 1>&2
        usage
        exit 1
        ;;
    esac
  done
  shift $((OPTIND -1))
}

function clean() {
  rm -rf "${ROOT_DIR}"/build "${ROOT_DIR}"/install
}

function build_cyclonedds_host() {
  echo -e "${GREEN}Building CycloneDDS host tools...${NC}"
  mkdir -p build/cyclonedds_host
  pushd build/cyclonedds_host
  cmake -DCMAKE_INSTALL_PREFIX="$(pwd)"/out -DENABLE_SECURITY=OFF -DENABLE_SSL=OFF -DBUILD_IDLC=ON -DBUILD_SHARED_LIBS=ON -DENABLE_SHM=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DDSPERF=OFF "${ROOT_DIR}"/cyclonedds
  cmake --build . --target install -- -j"$(nproc)"
  popd
}

function build_actuation_module() {
  echo -e "${GREEN}Building Zephyr Actuation Module...${NC}"
  typeset PATH="${ROOT_DIR}"/build/cyclonedds_host/out/bin:$PATH
  typeset LD_LIBRARY_PATH="${ROOT_DIR}"/build/cyclonedds_host/out/lib
  export CMAKE_PREFIX_PATH=""
  export AMENT_PREFIX_PATH=""
  local target_base="${ZEPHYR_TARGET%%@*}"
  local extra_conf_files=()

  # Build command with common arguments
  local build_args=(
    -DZEPHYR_TARGET="${ZEPHYR_TARGET}"
    -DCYCLONEDDS_SRC="${ROOT_DIR}"/cyclonedds
    -DEXTRA_CFLAGS="-Wno-error"
    -DEXTRA_CXXFLAGS="-Wno-error"
    "-DBUILD_TEST=${BUILD_TEST_FLAG}"
  )

  local board_conf="${ROOT_DIR}/actuation_module/boards/${target_base}_actuation.conf"
  if [ -f "${board_conf}" ]; then
    extra_conf_files+=("${board_conf}")
  fi

  # Add device tree overlay only for ARM board variant
  if [ "${ZEPHYR_TARGET}" = "s32z270dc2_rtu0_r52@D" ]; then
    build_args+=(-DEXTRA_DTC_OVERLAY_FILE="${ROOT_DIR}"/actuation_module/boards/s32z270dc2_rtu0_r52@D.overlay)
  fi

  local can_loopback_conf="${ROOT_DIR}/actuation_module/boards/${target_base}_can_loopback.conf"
  local can_loopback_overlay="${ROOT_DIR}/actuation_module/boards/${target_base}_can_loopback.overlay"
  if [ "${BUILD_TEST_FLAG}" = "4" ]; then
    if [ -f "${can_loopback_conf}" ]; then
      extra_conf_files+=("${can_loopback_conf}")
    fi
    if [ -f "${can_loopback_overlay}" ]; then
      build_args+=(-DEXTRA_DTC_OVERLAY_FILE="${can_loopback_overlay}")
    fi
  fi

  if [ "${#extra_conf_files[@]}" -gt 0 ]; then
    local extra_conf_file
    extra_conf_file=$(IFS=';'; echo "${extra_conf_files[*]}")
    build_args+=(-DEXTRA_CONF_FILE="${extra_conf_file}")
  fi

  west build -p auto -d "${BUILD_DIR}" -b "${ZEPHYR_TARGET}" actuation_module/ -- "${build_args[@]}"
}

## MAIN ##
parse_args "$@"

# Create build directory
cd "${ROOT_DIR}"
mkdir -p build

# Build CycloneDDS host tools
build_cyclonedds_host

# Build Zephyr Actuation Module
build_actuation_module
