#! /usr/bin/env bash

# Copyright (c) 2025, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Build script for supported Autoware Safety Island runtime targets.
#
# This script builds Zephyr and FreeRTOS runtime targets.
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
CYCLONEDDS_HOST_PREFIX=${CYCLONEDDS_HOST_PREFIX:-"${CYCLONEDDS_HOST_BUILD_DIR}/out"}
CYCLONEDDS_TARGET_BUILD_DIR=${CYCLONEDDS_TARGET_BUILD_DIR:-"${ROOT_DIR}/build/cyclonedds_target"}
CYCLONEDDS_TARGET_PREFIX=${CYCLONEDDS_TARGET_PREFIX:-"${ROOT_DIR}/build/cyclonedds_target_out"}

# Build options
BUILD_TEST_FLAG=0
BUILD_DIR="build/actuation_module"
BUILD_DIR_SET=0
BUILD_PLATFORM="zephyr-fvp"
BUILD_PLATFORM_SET=0
NETWORK_PROFILE="default"
PARAM_PROFILE="after"
DDS_NETWORK_INTERFACE=""
CONTROL_CMD_OUTPUT_MODE=""
# Repeatable pass-through for ad hoc CMake cache entries (e.g. instrumented
# CI builds), forwarded verbatim as -D<value> to the freertos-x5h cmake
# configure call in build_freertos_x5h(). Empty by default and only ever
# appended to by --cmake-define, so leaving it unused (as every platform
# other than freertos-x5h does today) is behaviourally identical to it not
# existing.
EXTRA_CMAKE_ARGS=()
RUNTIME_TARGET_LIST=("zephyr-fvp" "zephyr-s32z" "freertos-posix" "freertos-s32z2" "freertos-x5h")
ZEPHYR_TARGET_LIST=("fvp_baser_aemv8r_smp" "s32z270dc2_rtu0_r52@D")
ZEPHYR_TARGET=${ZEPHYR_TARGET_LIST[0]} # Default target is fvp_baser_aemv8r_smp
ZEPHYR_TARGET_SET=0

function usage() {
  echo -e "${GREEN}Usage: $0 [OPTIONS]${NC}"
  echo -e "------------------------------------------------"
  echo -e "${GREEN}    --platform         ${NC}Runtime target: ${RUNTIME_TARGET_LIST[*]}."
  echo -e "${GREEN}                         default: zephyr-fvp.${NC}"
  echo -e "${GREEN}    --network          ${NC}Network profile: default, tap. tap is valid for zephyr-fvp."
  echo -e "${GREEN}    --dds-interface    ${NC}DDS interface/IP selector for FreeRTOS targets."
  echo -e "${GREEN}    --control-output   ${NC}FreeRTOS control output: DDS_ONLY, CAN_ONLY, DDS_AND_CAN."
  echo -e "${GREEN}    --cmake-define     ${NC}Extra CMake cache entry KEY=VALUE, forwarded as -DKEY=VALUE."
  echo -e "${GREEN}                         Repeatable. freertos-x5h only."
  echo -e "${GREEN}    --param-profile    ${NC}Actuation parameter profile: after (default), before."
  echo -e "${GREEN}                         freertos-x5h only (MRM before/after demo).${NC}"
  echo -e "${GREEN}    -t                 ${NC}Zephyr target board: ${ZEPHYR_TARGET_LIST[*]}"
  echo -e "${GREEN}                         default: ${ZEPHYR_TARGET_LIST[0]}.${NC}"
  echo -e "${GREEN}    -d                 ${NC}Build directory. Default: ${BUILD_DIR}."
  echo -e "${GREEN}    -c                 ${NC}Clean all builds and exit."
  echo -e "${GREEN}    -h                 ${NC}Display the usage and exit."
  echo ""
  echo -e "${GREEN}    Optional arguments to build test programs:${NC}"
  echo -e "${GREEN}    --unit-test        ${NC}Build unit test program."
  echo -e "${GREEN}    --dds-publisher    ${NC}Build DDS publisher test program."
  echo -e "${GREEN}    --dds-subscriber   ${NC}Build DDS subscriber test program."
  echo -e "${GREEN}    --can-output-test  ${NC}Build CAN output test program."
  echo -e "${GREEN}    --dds-loopback-test${NC}Build Zephyr DDS loopback test program."
  echo ""
  echo -e "${GREEN}    Runtime target matrix:${NC}"
  echo -e "    zephyr-fvp       Zephyr on Arm FVP for local validation / AVH."
  echo -e "    zephyr-s32z      Zephyr on S32Z hardware."
  echo -e "    freertos-posix   FreeRTOS POSIX runtime for local validation."
  echo -e "    freertos-s32z2   FreeRTOS on S32Z2 hardware."
  echo -e "    freertos-x5h     FreeRTOS on R-Car X5H hardware (full actuation module + CycloneDDS + lwIP-over-RPMsg)."
  echo ""
  echo -e "${GREEN}    Examples:${NC}"
  echo -e "    $0 --platform zephyr-fvp --network tap -d build/zephyr-fvp-tap"
  echo -e "    $0 --platform freertos-posix -d build/freertos-posix --dds-interface wlp2s0 --control-output DDS_ONLY"
  echo -e "    $0 --platform freertos-s32z2 -d build/freertos-s32z2 --dds-interface 192.168.0.105"
  echo -e "    $0 --platform freertos-x5h -d build/freertos-x5h"
  echo -e "    $0 --platform freertos-x5h -d build/freertos-x5h-diag --cmake-define X5H_DIAG_TASK_TABLE=ON --cmake-define CONFIG_DDS_LOG_LEVEL=3"
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
      --cmake-define)
        require_arg "$1" "${2:-}"
        EXTRA_CMAKE_ARGS+=("-D$2")
        shift 2
        ;;
      --param-profile)
        require_arg "$1" "${2:-}"
        case "$2" in
          before|after)
            PARAM_PROFILE="$2"
            ;;
          *)
            echo -e "${RED}--param-profile must be 'before' or 'after', got '$2'${NC}" 1>&2
            exit 1
            ;;
        esac
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
      if [ "${BUILD_DIR_SET}" = "0" ]; then
        BUILD_DIR="build/zephyr-fvp"
      fi
      ;;
    zephyr-s32z)
      if [ "${ZEPHYR_TARGET_SET}" = "1" ] && [ "${ZEPHYR_TARGET}" != "s32z270dc2_rtu0_r52@D" ]; then
        echo -e "${RED}--platform zephyr-s32z conflicts with -t ${ZEPHYR_TARGET}${NC}" 1>&2
        exit 1
      fi
      ZEPHYR_TARGET="s32z270dc2_rtu0_r52@D"
      if [ "${BUILD_DIR_SET}" = "0" ]; then
        BUILD_DIR="build/zephyr-s32z"
      fi
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
    freertos-s32z2)
      if [ "${ZEPHYR_TARGET_SET}" = "1" ]; then
        echo -e "${RED}-t is only valid for Zephyr platforms${NC}" 1>&2
        exit 1
      fi
      if [ "${BUILD_DIR_SET}" = "0" ]; then
        BUILD_DIR="build/freertos-s32z2"
      fi
      ;;
    freertos-x5h)
      if [ "${ZEPHYR_TARGET_SET}" = "1" ]; then
        echo -e "${RED}-t is only valid for Zephyr platforms${NC}" 1>&2
        exit 1
      fi
      if [ "${BUILD_DIR_SET}" = "0" ]; then
        BUILD_DIR="build/freertos-x5h"
      fi
      ;;
    *)
      echo -e "${RED}Invalid platform: ${BUILD_PLATFORM}${NC}" 1>&2
      echo -e "${YELLOW}Valid platforms: ${RUNTIME_TARGET_LIST[*]}${NC}" 1>&2
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

  if [ -n "${DDS_NETWORK_INTERFACE}" ] && [ "${BUILD_PLATFORM}" != "freertos-posix" ] && [ "${BUILD_PLATFORM}" != "freertos-s32z2" ] && [ "${BUILD_PLATFORM}" != "freertos-x5h" ]; then
    echo -e "${RED}--dds-interface is only valid for --platform freertos-posix, freertos-s32z2, or freertos-x5h${NC}" 1>&2
    exit 1
  fi

  if [ -n "${CONTROL_CMD_OUTPUT_MODE}" ] && [ "${BUILD_PLATFORM}" != "freertos-posix" ] && [ "${BUILD_PLATFORM}" != "freertos-s32z2" ] && [ "${BUILD_PLATFORM}" != "freertos-x5h" ]; then
    echo -e "${RED}--control-output is only valid for --platform freertos-posix, freertos-s32z2, or freertos-x5h${NC}" 1>&2
    exit 1
  fi

  if [ "${#EXTRA_CMAKE_ARGS[@]}" -gt 0 ] && [ "${BUILD_PLATFORM}" != "freertos-x5h" ]; then
    echo -e "${RED}--cmake-define is only valid for --platform freertos-x5h${NC}" 1>&2
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

  case "${BUILD_PLATFORM}" in
    freertos-s32z2|freertos-x5h)
      if [ "${BUILD_TEST_FLAG}" != "0" ]; then
        echo -e "${RED}Test build options are not supported for --platform ${BUILD_PLATFORM}${NC}" 1>&2
        exit 1
      fi
      ;;
  esac
}

function clean() {
  rm -rf "${ROOT_DIR}"/build "${ROOT_DIR}"/install
}

function build_cyclonedds_host() {
  if [ -x "${CYCLONEDDS_HOST_PREFIX}/bin/idlc" ]; then
    echo -e "${GREEN}CycloneDDS host tools already built at ${CYCLONEDDS_HOST_PREFIX}${NC}"
    return
  fi

  echo -e "${GREEN}Building CycloneDDS host tools...${NC}"
  cmake cyclonedds -B "${CYCLONEDDS_HOST_BUILD_DIR}" \
    -DBUILD_IDLC=ON -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX="${CYCLONEDDS_HOST_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_SECURITY=OFF -DENABLE_SSL=OFF -DENABLE_SHM=OFF \
    -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_DDSPERF=OFF
  cmake --build "${CYCLONEDDS_HOST_BUILD_DIR}" --target install -j"$(nproc)"
}

function build_cyclonedds_target_posix() {
  if [ -f "${CYCLONEDDS_TARGET_PREFIX}/lib/libddsc.a" ]; then
    echo -e "${GREEN}CycloneDDS POSIX target library already built at ${CYCLONEDDS_TARGET_PREFIX}${NC}"
    return
  fi

  echo -e "${GREEN}Building CycloneDDS POSIX target library...${NC}"
  cmake cyclonedds -B "${CYCLONEDDS_TARGET_BUILD_DIR}" \
    -DBUILD_SHARED_LIBS=OFF -DENABLE_SECURITY=OFF \
    -DENABLE_SSL=OFF -DENABLE_SHM=OFF -DENABLE_IPV6=OFF \
    -DBUILD_IDLC=OFF -DBUILD_DDSPERF=OFF \
    -DCMAKE_INSTALL_PREFIX="${CYCLONEDDS_TARGET_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Debug
  cmake --build "${CYCLONEDDS_TARGET_BUILD_DIR}" --target install -j"$(nproc)"
}

function build_zephyr_actuation_module() {
  echo -e "${GREEN}Building Zephyr Actuation Module...${NC}"
  export PATH="${CYCLONEDDS_HOST_PREFIX}"/bin:$PATH
  export LD_LIBRARY_PATH="${CYCLONEDDS_HOST_PREFIX}"/lib:${LD_LIBRARY_PATH:-}
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
  echo -e "${GREEN}Building FreeRTOS POSIX runtime...${NC}"
  if [ "${BUILD_TEST_FLAG}" = "5" ]; then
    echo -e "${RED}--dds-loopback-test is not supported by the FreeRTOS POSIX CMake path${NC}" 1>&2
    exit 1
  fi

  local app_build_dir
  app_build_dir=$(realpath -m "${BUILD_DIR}")

  build_cyclonedds_host
  build_cyclonedds_target_posix

  export PATH="${CYCLONEDDS_HOST_PREFIX}"/bin:$PATH
  export LD_LIBRARY_PATH="${CYCLONEDDS_HOST_PREFIX}"/lib:${LD_LIBRARY_PATH:-}
  local freertos_args=(
    actuation_module/freertos
    -B "${app_build_dir}"
    -DCDDS_HOST_PREFIX="${CYCLONEDDS_HOST_PREFIX}"
    -DCDDS_TARGET_PREFIX="${CYCLONEDDS_TARGET_PREFIX}"
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

function build_freertos_s32z2() {
  echo -e "${GREEN}Building FreeRTOS S32Z2 target...${NC}"
  echo -e "${YELLOW}This target requires NXP S32Z2 RTD, FreeRTOS, lwIP, and S32 Config Tools output.${NC}"

  local app_build_dir
  app_build_dir=$(realpath -m "${BUILD_DIR}")
  local cdds_target_build_dir="${FREERTOS_S32Z2_CDDS_TARGET_BUILD_DIR:-${app_build_dir}/cdds_target}"
  local cdds_target_prefix="${FREERTOS_S32Z2_CDDS_TARGET_PREFIX:-${app_build_dir}/cdds_target_out}"

  build_cyclonedds_host

  FREERTOS_S32Z2_BUILD_ROOT="${app_build_dir}" \
  FREERTOS_S32Z2_CDDS_HOST_PREFIX="${CYCLONEDDS_HOST_PREFIX}" \
  FREERTOS_S32Z2_CDDS_TARGET_BUILD_DIR="${cdds_target_build_dir}" \
  FREERTOS_S32Z2_CDDS_TARGET_PREFIX="${cdds_target_prefix}" \
    "${ROOT_DIR}/actuation_module/freertos_s32z2/scripts/build-cdds-target.sh"

  local freertos_args=(
    actuation_module/freertos_s32z2
    -B "${app_build_dir}"
    -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/actuation_module/freertos_s32z2/cmake/arm-cortex-r52.cmake"
    -DCDDS_HOST_PREFIX="${CYCLONEDDS_HOST_PREFIX}"
    -DCDDS_TARGET_PREFIX="${cdds_target_prefix}"
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

function build_freertos_x5h() {
  echo -e "${GREEN}Building FreeRTOS X5H target...${NC}"
  echo -e "${YELLOW}Full-linked actuation module + CycloneDDS + lwIP-over-RPMsg. Network bring-up (RPMsg netif) is live, not stubbed.${NC}"

  # Task 8's frozen wire constants (CR52 172.16.52.2, Linux 172.16.52.1, DDS
  # domain 2, multicast disabled both sides) are asserted here, before any
  # compiling starts, precisely because this checker needs no build
  # artifacts at all -- it only greps/xpaths source files
  # (CMakeLists.txt, scripts/build-edge-ecu-peer-arm64.sh,
  # edge_ecu_peer/cyclonedds-x5h.xml). Run unconditionally rather than
  # gating it on the ELF the way check-elf-contract.sh/check-image-budget.sh
  # are gated below: a wire-constant regression is worth catching before
  # spending build time, not after.
  "${ROOT_DIR}/actuation_module/freertos_x5h/scripts/check-dds-config.sh"

  local app_build_dir
  app_build_dir=$(realpath -m "${BUILD_DIR}")

  local toolchain_bin
  toolchain_bin=$("${ROOT_DIR}/actuation_module/freertos_x5h/scripts/fetch-toolchain.sh")
  export PATH="${toolchain_bin}:${PATH}"

  local x5h_dir="${ROOT_DIR}/actuation_module/freertos_x5h"
  local rcar_bsp_dir="${x5h_dir}/rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52"
  local cdds_target_build_dir="${FREERTOS_X5H_CDDS_TARGET_BUILD_DIR:-${app_build_dir}/cdds_target}"
  local cdds_target_prefix="${FREERTOS_X5H_CDDS_TARGET_PREFIX:-${app_build_dir}/cdds_target_out}"

  # autoware_msgs's IDL -> C generation (pulled in transitively via
  # actuation_x5h's CMakeLists.txt) runs the host idlc, and CycloneDDS's own
  # target build runs it too (on its own internal .idl files) -- both need
  # the host tools built first. PATH/LD_LIBRARY_PATH are exported at the
  # shell level (not just via CMake's ENV{PATH}, which only reaches the
  # configure-time process) so the generated Makefiles' add_custom_command
  # invocations of idlc -- run later by `cmake --build`, in a separate
  # process tree -- can find it too. Mirrors build_freertos_s32z2()'s
  # identical export pair.
  build_cyclonedds_host
  export PATH="${CYCLONEDDS_HOST_PREFIX}"/bin:$PATH
  # Only prepend a ":" separator when LD_LIBRARY_PATH already has a value
  # (review round 1 fix): the old unconditional
  # "${CYCLONEDDS_HOST_PREFIX}/lib:${LD_LIBRARY_PATH:-}" left a trailing
  # colon whenever LD_LIBRARY_PATH was unset (the common case), which the
  # dynamic loader treats as an empty path component meaning "the current
  # working directory" -- silently adding CWD to the loader search path.
  if [ -n "${LD_LIBRARY_PATH:-}" ]; then
    export LD_LIBRARY_PATH="${CYCLONEDDS_HOST_PREFIX}"/lib:"${LD_LIBRARY_PATH}"
  else
    export LD_LIBRARY_PATH="${CYCLONEDDS_HOST_PREFIX}"/lib
  fi

  FREERTOS_X5H_BUILD_ROOT="${app_build_dir}" \
  FREERTOS_X5H_CDDS_HOST_PREFIX="${CYCLONEDDS_HOST_PREFIX}" \
  FREERTOS_X5H_CDDS_TARGET_BUILD_DIR="${cdds_target_build_dir}" \
  FREERTOS_X5H_CDDS_TARGET_PREFIX="${cdds_target_prefix}" \
    "${x5h_dir}/scripts/build-cdds-target.sh"

  # -S points at the vendor's own BSP directory, not
  # actuation_module/freertos_x5h: the vendor's CMakeLists.txt derives its
  # BSP_DIR/FREERTOS_DIR from CMAKE_SOURCE_DIR, which is fixed to whatever
  # -S names for the whole invocation and cannot be overridden from a child
  # add_subdirectory() scope. -DCMAKE_PROJECT_INCLUDE pulls
  # actuation_x5h back in via a deferred include() -- see
  # actuation_module/freertos_x5h/cmake/inject_actuation_x5h.cmake for the
  # full rationale. BOARD/RAM_REGION/MFIS_CHAN/UART_ID/CACHE/ENABLE_OPENAMP
  # match Task 2's scripts/build-bsp-rpmsg-sample.sh (see AUDIT.md Section 6).
  local x5h_args=(
    -S "${rcar_bsp_dir}"
    -B "${app_build_dir}"
    -DCMAKE_TOOLCHAIN_FILE="${rcar_bsp_dir}/toolchain_arm_none_eabi.cmake"
    -DCMAKE_PROJECT_INCLUDE="${x5h_dir}/cmake/inject_actuation_x5h.cmake"
    -DBOARD=x5h_ironhide
    -DENABLE_OPENAMP=1
    -DRAM_REGION=2
    -DMFIS_CHAN=1
    -DUART_ID=1
    -DCACHE=1
    -DCDDS_HOST_PREFIX="${CYCLONEDDS_HOST_PREFIX}"
    -DCDDS_TARGET_PREFIX="${cdds_target_prefix}"
  )

  if [ -n "${DDS_NETWORK_INTERFACE}" ]; then
    x5h_args+=(-DCONFIG_DDS_NETWORK_INTERFACE="${DDS_NETWORK_INTERFACE}")
  fi
  if [ -n "${CONTROL_CMD_OUTPUT_MODE}" ]; then
    x5h_args+=(-DCONFIG_CONTROL_CMD_OUTPUT_MODE="${CONTROL_CMD_OUTPUT_MODE}")
  fi
  x5h_args+=(-DACTUATION_PARAM_PROFILE="${PARAM_PROFILE}")
  # e.g. --cmake-define X5H_DIAG_TASK_TABLE=ON --cmake-define
  # CONFIG_DDS_LOG_LEVEL=3 for an instrumented build into its own -d
  # directory; empty by default, so the default configuration's argv (and
  # therefore its byte-identical output) is unchanged when this is absent.
  if [ "${#EXTRA_CMAKE_ARGS[@]}" -gt 0 ]; then
    x5h_args+=("${EXTRA_CMAKE_ARGS[@]}")
  fi

  cmake "${x5h_args[@]}"
  # --target actuation_x5h, not a bare `cmake --build`: the vendor -S
  # directory also defines its own hello_world/rpmsg_sample/etc. targets,
  # and a bare build would compile all of them too.
  cmake --build "${app_build_dir}" --target actuation_x5h -j"$(nproc)"

  # Review finding (Minor #1): netif_only_x5h is EXCLUDE_FROM_ALL (see its
  # CMakeLists.txt block) precisely so a bare `cmake --build` of this same
  # configure does not compile it as a side effect of building
  # actuation_x5h -- but that also meant nothing ever built or contract/
  # budget-checked it automatically; it silently bit-rotted between manual
  # `--target netif_only_x5h` invocations. Build it explicitly here, in the
  # same configure (per the controller's ruling that this must be a second
  # target, not a second configure), so every build.sh run proves both
  # artifacts still build and pass both gates.
  cmake --build "${app_build_dir}" --target netif_only_x5h -j"$(nproc)"

  # A future rcar_bsp submodule bump is loud in most ways a layout change
  # could break this target (renamed sources fail to configure, a second
  # -T fails to link, a new vendor project() call duplicates a target) --
  # but a bump that silently shifts .text or .resource_table to a different
  # address is not loud at all unless something checks for it. Run the
  # frozen-layout contract here so that check happens on every build, not
  # only when someone remembers to run it by hand. Pass RPMSG_ETH_SERVICE's
  # literal ("rpmsg-eth", rpmsg_netif_core.h) so the contract also confirms
  # the service-name string made it into .rodata on both ELFs -- safe to
  # rely on now that check-elf-contract.sh's SVC check no longer has the
  # SIGPIPE-under-pipefail false-failure bug (see that script's own
  # top-of-file comment).
  "${x5h_dir}/scripts/check-elf-contract.sh" "${app_build_dir}/actuation_x5h.elf" rpmsg-eth "${PARAM_PROFILE}"
  "${x5h_dir}/scripts/check-elf-contract.sh" "${app_build_dir}/netif_only_x5h.elf" rpmsg-eth "${PARAM_PROFILE}"

  # Task 4's memory-risk gate: the full lwIP + CycloneDDS + actuation module
  # link must fit the frozen 10 MiB Core1 boot-slot window. Checked on both
  # ELFs for the same reason as above: netif_only_x5h must not silently grow
  # past budget just because nothing was watching it.
  "${x5h_dir}/scripts/check-image-budget.sh" "${app_build_dir}/actuation_x5h.elf"
  "${x5h_dir}/scripts/check-image-budget.sh" "${app_build_dir}/netif_only_x5h.elf"
}

## MAIN ##
parse_args "$@"
normalize_platform

# Create build directory
cd "${ROOT_DIR}"
mkdir -p build

if [ "${PARAM_PROFILE}" != "after" ] && [ "${BUILD_PLATFORM}" != "freertos-x5h" ]; then
  echo -e "${RED}--param-profile is only supported for --platform freertos-x5h${NC}" 1>&2
  exit 1
fi

case "${BUILD_PLATFORM}" in
  zephyr-fvp|zephyr-s32z)
    build_cyclonedds_host
    build_zephyr_actuation_module
    ;;
  freertos-posix)
    build_freertos_posix
    ;;
  freertos-s32z2)
    build_freertos_s32z2
    ;;
  freertos-x5h)
    build_freertos_x5h
    ;;
esac
