#!/usr/bin/env bash
# Shared Zephyr/FVP runtime CI phases.

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
FVP_BIN_NAME="FVP_BaseR_AEMv8R"
ZEPHYR_TARGET="fvp_baser_aemv8r_smp"
BUILD_ROOT="${ROOT_DIR}/build/zephyr-fvp"
LOG_DIR="${BUILD_ROOT}/logs"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

mkdir -p "${LOG_DIR}"

resolve_armfvp()
{
  if [ -n "${ARMFVP_BIN_PATH:-}" ] && [ -x "${ARMFVP_BIN_PATH}/${FVP_BIN_NAME}" ]; then
    return 0
  fi

  local fvp_path
  fvp_path=$(command -v "${FVP_BIN_NAME}" || true)
  if [ -n "${fvp_path}" ]; then
    ARMFVP_BIN_PATH=$(dirname "${fvp_path}")
    export ARMFVP_BIN_PATH
    return 0
  fi

  echo "Missing ${FVP_BIN_NAME}. Install Armv8-R AEM FVP and set ARMFVP_BIN_PATH, or add it to the CI image." >&2
  exit 1
}

build_variant()
{
  local name="$1"
  shift

  "${ROOT_DIR}/build.sh" -t "${ZEPHYR_TARGET}" -d "${BUILD_ROOT}/${name}" "$@"
}

run_fvp_variant()
{
  local name="$1"
  local log="$2"
  local timeout_seconds="$3"

  run_command_with_timeout "${log}" "${timeout_seconds}" \
    cmake --build "${BUILD_ROOT}/${name}" --target run_armfvp
}

resolve_armfvp

echo "Phase 1 - Zephyr full controller build + FVP runtime smoke"
build_variant full
run_fvp_variant full "${LOG_DIR}/controller.log" "${ZEPHYR_FVP_CONTROLLER_TIMEOUT_SECONDS:-60}"
require_marker "${LOG_DIR}/controller.log" "Starting Controller Node"
require_marker "${LOG_DIR}/controller.log" "Controller Node Started"
require_marker "${LOG_DIR}/controller.log" "Actuation Safety Island is Live"

echo "Phase 2 - Zephyr unit_test build + FVP run"
build_variant unit --unit-test
run_fvp_variant unit "${LOG_DIR}/unit.log" "${ZEPHYR_FVP_UNIT_TIMEOUT_SECONDS:-60}"
require_marker "${LOG_DIR}/unit.log" "=== All Tests Passed ==="

echo "Phase 3 - Zephyr DDS loopback build + FVP run"
build_variant dds-loopback --dds-loopback-test
run_fvp_variant dds-loopback "${LOG_DIR}/dds-loopback.log" "${ZEPHYR_FVP_DDS_TIMEOUT_SECONDS:-90}"
require_marker "${LOG_DIR}/dds-loopback.log" "Starting DDS loopback test"
require_marker "${LOG_DIR}/dds-loopback.log" "STEERING REPORT"
require_marker "${LOG_DIR}/dds-loopback.log" "DDS loopback test passed"

echo "Phase 4 - Zephyr CAN loopback build + FVP run"
build_variant can --can-output-test
run_fvp_variant can "${LOG_DIR}/can.log" "${ZEPHYR_FVP_CAN_TIMEOUT_SECONDS:-60}"
require_marker "${LOG_DIR}/can.log" "CAN output tests passed"

echo "Zephyr FVP runtime validation OK"
