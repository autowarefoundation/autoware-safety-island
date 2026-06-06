#! /usr/bin/env bash

# Copyright (c) 2025, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Build script for supported Autoware Safety Island runtimes.
#
# This script builds Zephyr targets and the FreeRTOS POSIX simulator.
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
CYCLONEDDS_HOST_BUILD_DIR=${CYCLONEDDS_HOST_BUILD_DIR:-"${ROOT_DIR}/build/cyclonedds_host"}

# Build options
BUILD_TEST_FLAG=0
BUILD_DIR="build/actuation_module"
BUILD_DIR_SET=0
BUILD_PLATFORM="zephyr-fvp"
BUILD_PLATFORM_SET=0
NETWORK_PROFILE="default"
DDS_NETWORK_INTERFACE=""
CONTROL_CMD_OUTPUT_MODE=""
ZEPHYR_TARGET_LIST=("fvp_baser_aemv8r_smp" "s32z270dc2_rtu0_r52@D")
ZEPHYR_TARGET=${ZEPHYR_TARGET_LIST[0]} # Default target is fvp_baser_aemv8r_smp
ZEPHYR_TARGET_SET=0

function usage() {
  echo -e "${GREEN}Usage: $0 [OPTIONS]${NC}"
  echo -e "------------------------------------------------"
  echo -e "${GREEN}    --platform         ${NC}Build platform: zephyr-fvp, zephyr-s32z, freertos-posix."
  echo -e "${GREEN}                         default: zephyr-fvp.${NC}"
  echo -e "${GREEN}    --network          ${NC}Network profile: default, tap. tap is valid for zephyr-fvp."
  echo -e "${GREEN}    --dds-interface    ${NC}DDS interface for freertos-posix, for example wlp2s0."
  echo -e "${GREEN}    --control-output   ${NC}FreeRTOS control output: DDS_ONLY, CAN_ONLY, DDS_AND_CAN."
  echo -e "${GREEN}    -t                 ${NC}Zephyr target board: ${ZEPHYR_TARGET_LIST[*]}"
  echo -e "${GREEN}                         default: ${ZEPHYR_TARGET_LIST[0]}.${NC}"
  echo -e "${GREEN}    -d                 ${NC}Build directory. Default: ${BUILD_DIR}."
  echo -e "${GREEN}    -c                 ${NC}Clean all builds and exit."
  echo -e "${GREEN}    -h                 ${NC}Display the usage and exit."
  echo ""
  echo -e "${GREEN}    Optional arguments to build test programs:${NC}"
  echo -e "${GREEN}    --unit-test        ${NC}Build Zephyr unit test program."
  echo -e "${GREEN}    --dds-publisher    ${NC}Build Zephyr DDS publisher."
  echo -e "${GREEN}    --dds-subscriber   ${NC}Build Zephyr DDS subscriber."
  echo -e "${GREEN}    --can-output-test  ${NC}Build Zephyr CAN output test program."
  echo -e "${GREEN}    --dds-loopback-test${NC}Build Zephyr DDS loopback test program."
  echo ""
  echo -e "${GREEN}    Examples:${NC}"
  echo -e "    $0 --platform zephyr-fvp --network tap -d build/zephyr-fvp-tap"
  echo -e "    $0 --platform freertos-posix -d build/freertos-posix --dds-interface wlp2s0 --control-output DDS_ONLY"
}

function require_arg() {
  local option="$1"
  local value="${2:-}"
  if [ -z "${value}" ]; then
    echo -e "${RED}${option} requires an argument${NC}" 1>&2
    exit 1
  fi
}

function validate_zephyr_target() {
  local target="$1"
  for t in "${ZEPHYR_TARGET_LIST[@]}"; do
    if [ "${t}" = "${target}" ]; then
      return 0
    fi
  done

  echo -e "${RED}Invalid Zephyr target: ${target}${NC}\n" 1>&2
  echo -e "${YELLOW}Valid targets: ${ZEPHYR_TARGET_LIST[*]}${NC}" 1>&2
  exit 1
}

function parse_args() {
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --help|-h)
        usage
        exit 0
        ;;
      --platform)
        require_arg "$1" "${2:-}"
        BUILD_PLATFORM="$2"
        BUILD_PLATFORM_SET=1
        shift 2
        ;;
      --network)
        require_arg "$1" "${2:-}"
        NETWORK_PROFILE="$2"
        shift 2
        ;;
      --dds-interface)
        require_arg "$1" "${2:-}"
        DDS_NETWORK_INTERFACE="$2"
        shift 2
        ;;
      --control-output)
        require_arg "$1" "${2:-}"
        CONTROL_CMD_OUTPUT_MODE="$2"
        shift 2
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
      -t)
        require_arg "$1" "${2:-}"
        validate_zephyr_target "$2"
        ZEPHYR_TARGET="$2"
        ZEPHYR_TARGET_SET=1
        shift 2
        ;;
      -d)
        require_arg "$1" "${2:-}"
        BUILD_DIR="$2"
        BUILD_DIR_SET=1
        shift 2
        ;;
      -c)
        clean
        exit 0
        ;;
      *)
        echo -e "${RED}Invalid option: $1${NC}\n" 1>&2
        usage
        exit 1
        ;;
    esac
  done
}

function normalize_platform() {
  if [ "${BUILD_PLATFORM_SET}" = "0" ] && [ "${ZEPHYR_TARGET_SET}" = "1" ]; then
    if [ "${ZEPHYR_TARGET}" = "fvp_baser_aemv8r_smp" ]; then
      BUILD_PLATFORM="zephyr-fvp"
    elif [ "${ZEPHYR_TARGET}" = "s32z270dc2_rtu0_r52@D" ]; then
      BUILD_PLATFORM="zephyr-s32z"
    fi
  fi

  case "${BUILD_PLATFORM}" in
    zephyr-fvp)
      if [ "${ZEPHYR_TARGET_SET}" = "1" ] && [ "${ZEPHYR_TARGET}" != "fvp_baser_aemv8r_smp" ]; then
        echo -e "${RED}--platform zephyr-fvp conflicts with -t ${ZEPHYR_TARGET}${NC}" 1>&2
        exit 1
      fi
      ZEPHYR_TARGET="fvp_baser_aemv8r_smp"
      ;;
    zephyr-s32z)
      if [ "${ZEPHYR_TARGET_SET}" = "1" ] && [ "${ZEPHYR_TARGET}" != "s32z270dc2_rtu0_r52@D" ]; then
        echo -e "${RED}--platform zephyr-s32z conflicts with -t ${ZEPHYR_TARGET}${NC}" 1>&2
        exit 1
      fi
      ZEPHYR_TARGET="s32z270dc2_rtu0_r52@D"
      ;;
    freertos-posix)
      if [ "${ZEPHYR_TARGET_SET}" = "1" ]; then
        echo -e "${RED}-t is only valid for Zephyr platforms${NC}" 1>&2
        exit 1
      fi
      if [ "${BUILD_DIR_SET}" = "0" ]; then
        BUILD_DIR="build/freertos-posix"
      fi
      ;;
    *)
      echo -e "${RED}Invalid platform: ${BUILD_PLATFORM}${NC}" 1>&2
      echo -e "${YELLOW}Valid platforms: zephyr-fvp zephyr-s32z freertos-posix${NC}" 1>&2
      exit 1
      ;;
  esac

  if [ "${NETWORK_PROFILE}" != "default" ] && [ "${NETWORK_PROFILE}" != "tap" ]; then
    echo -e "${RED}Invalid network profile: ${NETWORK_PROFILE}${NC}" 1>&2
    echo -e "${YELLOW}Valid network profiles: default tap${NC}" 1>&2
    exit 1
  fi

  if [ "${NETWORK_PROFILE}" = "tap" ] && [ "${BUILD_PLATFORM}" != "zephyr-fvp" ]; then
    echo -e "${RED}--network tap is only valid for --platform zephyr-fvp${NC}" 1>&2
    exit 1
  fi

  if [ -n "${DDS_NETWORK_INTERFACE}" ] && [ "${BUILD_PLATFORM}" != "freertos-posix" ]; then
    echo -e "${RED}--dds-interface is only valid for --platform freertos-posix${NC}" 1>&2
    exit 1
  fi

  if [ -n "${CONTROL_CMD_OUTPUT_MODE}" ] && [ "${BUILD_PLATFORM}" != "freertos-posix" ]; then
    echo -e "${RED}--control-output is only valid for --platform freertos-posix${NC}" 1>&2
    exit 1
  fi

  if [ -n "${CONTROL_CMD_OUTPUT_MODE}" ]; then
    case "${CONTROL_CMD_OUTPUT_MODE}" in
      DDS_ONLY|CAN_ONLY|DDS_AND_CAN) ;;
      *)
        echo -e "${RED}Invalid control output mode: ${CONTROL_CMD_OUTPUT_MODE}${NC}" 1>&2
        echo -e "${YELLOW}Valid modes: DDS_ONLY CAN_ONLY DDS_AND_CAN${NC}" 1>&2
        exit 1
        ;;
    esac
  fi
}

function clean() {
  rm -rf "${ROOT_DIR}"/build "${ROOT_DIR}"/install
}

function build_cyclonedds_host() {
  echo -e "${GREEN}Building CycloneDDS host tools...${NC}"
  mkdir -p "${CYCLONEDDS_HOST_BUILD_DIR}"
  pushd "${CYCLONEDDS_HOST_BUILD_DIR}"
  cmake -DCMAKE_INSTALL_PREFIX="$(pwd)"/out -DENABLE_SECURITY=OFF -DENABLE_SSL=OFF -DBUILD_IDLC=ON -DBUILD_SHARED_LIBS=ON -DENABLE_SHM=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DDSPERF=OFF "${ROOT_DIR}"/cyclonedds
  cmake --build . --target install -- -j"$(nproc)"
  popd
}

function build_zephyr_actuation_module() {
  echo -e "${GREEN}Building Zephyr Actuation Module...${NC}"
  export PATH="${CYCLONEDDS_HOST_BUILD_DIR}"/out/bin:$PATH
  export LD_LIBRARY_PATH="${CYCLONEDDS_HOST_BUILD_DIR}"/out/lib:${LD_LIBRARY_PATH:-}
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

  if [ "${NETWORK_PROFILE}" = "tap" ]; then
    extra_conf_files+=("${ROOT_DIR}/actuation_module/boards/fvp_baser_aemv8r_smp_tap_network.conf")
    local fvp_tap_interface="${FVP_TAP_INTERFACE:-tap0}"
    export ARMFVP_EXTRA_FLAGS="${ARMFVP_EXTRA_FLAGS:-} -C bp.hostbridge.userNetworking=0 -C bp.hostbridge.interfaceName=${fvp_tap_interface}"
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

function build_freertos_posix() {
  echo -e "${GREEN}Building FreeRTOS POSIX simulator...${NC}"
  if [ "${BUILD_TEST_FLAG}" = "5" ]; then
    echo -e "${RED}--dds-loopback-test is not supported by the FreeRTOS POSIX CMake path${NC}" 1>&2
    exit 1
  fi

  local app_build_dir
  app_build_dir=$(realpath -m "${BUILD_DIR}")
  local build_root
  build_root=$(dirname "${app_build_dir}")
  local cdds_host_build_dir="${FREERTOS_CDDS_HOST_BUILD_DIR:-${build_root}/cyclonedds_host}"
  local cdds_target_build_dir="${FREERTOS_CDDS_TARGET_BUILD_DIR:-${build_root}/cyclonedds_target}"
  local cdds_host_prefix="${FREERTOS_CDDS_HOST_PREFIX:-${build_root}/cdds_host_out}"
  local cdds_target_prefix="${FREERTOS_CDDS_TARGET_PREFIX:-${build_root}/cdds_target_out}"

  cmake cyclonedds -B "${cdds_host_build_dir}" \
    -DBUILD_IDLC=ON -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX="${cdds_host_prefix}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_DDSPERF=OFF -DENABLE_SECURITY=OFF \
    -DENABLE_SSL=OFF -DENABLE_SHM=OFF \
    -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
  cmake --build "${cdds_host_build_dir}" --target install -j"$(nproc)"

  cmake cyclonedds -B "${cdds_target_build_dir}" \
    -DBUILD_SHARED_LIBS=OFF -DENABLE_SECURITY=OFF \
    -DENABLE_SSL=OFF -DENABLE_SHM=OFF -DENABLE_IPV6=OFF \
    -DBUILD_IDLC=OFF -DBUILD_DDSPERF=OFF \
    -DCMAKE_INSTALL_PREFIX="${cdds_target_prefix}" \
    -DCMAKE_BUILD_TYPE=Debug
  cmake --build "${cdds_target_build_dir}" --target install -j"$(nproc)"

  export PATH="${cdds_host_prefix}"/bin:$PATH
  export LD_LIBRARY_PATH="${cdds_host_prefix}"/lib:${LD_LIBRARY_PATH:-}
  local freertos_args=(
    actuation_module/freertos
    -B "${app_build_dir}"
    -DCDDS_HOST_PREFIX="${cdds_host_prefix}"
    -DCDDS_TARGET_PREFIX="${cdds_target_prefix}"
    "-DBUILD_TEST=${BUILD_TEST_FLAG}"
  )

  if [ -n "${DDS_NETWORK_INTERFACE}" ]; then
    freertos_args+=(-DCONFIG_DDS_NETWORK_INTERFACE="${DDS_NETWORK_INTERFACE}")
  fi
  if [ -n "${CONTROL_CMD_OUTPUT_MODE}" ]; then
    freertos_args+=(-DCONFIG_CONTROL_CMD_OUTPUT_MODE="${CONTROL_CMD_OUTPUT_MODE}")
  fi

  cmake "${freertos_args[@]}"
  cmake --build "${app_build_dir}" -j"$(nproc)"
}

## MAIN ##
parse_args "$@"
normalize_platform

# Create build directory
cd "${ROOT_DIR}"
mkdir -p build

case "${BUILD_PLATFORM}" in
  zephyr-fvp|zephyr-s32z)
    build_cyclonedds_host
    build_zephyr_actuation_module
    ;;
  freertos-posix)
    build_freertos_posix
    ;;
esac
