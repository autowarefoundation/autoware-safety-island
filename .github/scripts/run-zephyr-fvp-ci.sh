#!/usr/bin/env bash
# Zephyr FVP runtime CI phases (FVP binary is pre-installed in devcontainer)

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
ZEPHYR_TARGET="fvp_baser_aemv8r_smp"
BUILD_ROOT="${ROOT_DIR}/build/zephyr-fvp"
LOG_DIR="${BUILD_ROOT}/logs"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

mkdir -p "${LOG_DIR}"

# FVP binary path (installed in devcontainer)
FVP_BIN="/usr/local/bin/FVP_BaseR_AEMv8R"
export ARMFVP_BIN_PATH="/usr/local/bin"

# FVP terminal output port (default is 5000)
FVP_TERM_PORT="${FVP_TERM_PORT:-5000}"

# FVP run timeout
FVP_TIMEOUT_SECONDS="${FVP_TIMEOUT_SECONDS:-90}"

build_variant()
{
  local name="$1"
  shift

  "${ROOT_DIR}/build.sh" -t "${ZEPHYR_TARGET}" -d "${BUILD_ROOT}/${name}" "$@"
}

# Run FVP with the built ELF and capture output
run_fvp_variant()
{
  local name="$1"
  local log="$2"
  local timeout_seconds="$3"

  local build_dir="${BUILD_ROOT}/${name}"
  if [ ! -f "${build_dir}/zephyr/zephyr.elf" ]; then
    echo "Missing ELF: ${build_dir}/zephyr/zephyr.elf" >&2
    exit 1
  fi

  # Remove old log
  rm -f "${log}"

  echo "Starting FVP for ${name}..."
  
  # Use west build --target run which uses CMake's FVP configuration
  # ARMFVP_BIN_PATH must be set for CMake to find the FVP binary
  set +e
  run_command_with_timeout "${log}" "${timeout_seconds}" \
    west build -d "${build_dir}" --target run
  local rc=$?
  set -e

  if [ ! -s "${log}" ]; then
    echo "FVP produced no output for ${name}" >&2
    exit 1
  fi

  echo "FVP ${name} finished"
}

echo "Phase 1 - Zephyr FVP full controller build + runtime smoke"
build_variant full
run_fvp_variant full "${LOG_DIR}/controller.log" "${FVP_TIMEOUT_SECONDS}"
require_marker "${LOG_DIR}/controller.log" "Starting Controller Node"
require_marker "${LOG_DIR}/controller.log" "Controller Node Started"
require_marker "${LOG_DIR}/controller.log" "Actuation Safety Island is Live"

echo "Phase 2 - Zephyr FVP unit_test build + run"
build_variant unit --unit-test
run_fvp_variant unit "${LOG_DIR}/unit.log" "${FVP_TIMEOUT_SECONDS}"
require_marker "${LOG_DIR}/unit.log" "=== All Tests Passed ==="

echo "Phase 3 - Zephyr FVP DDS loopback build + run"
build_variant dds-loopback --dds-loopback-test
run_fvp_variant dds-loopback "${LOG_DIR}/dds-loopback.log" "${FVP_TIMEOUT_SECONDS}"
require_marker "${LOG_DIR}/dds-loopback.log" "Starting DDS loopback test"
require_marker "${LOG_DIR}/dds-loopback.log" "STEERING REPORT"
require_marker "${LOG_DIR}/dds-loopback.log" "DDS loopback test passed"

echo "Phase 4 - Zephyr FVP CAN loopback build + run"
build_variant can --can-output-test
run_fvp_variant can "${LOG_DIR}/can.log" "${FVP_TIMEOUT_SECONDS}"
require_marker "${LOG_DIR}/can.log" "CAN output tests passed"

echo "Zephyr FVP runtime validation OK"
