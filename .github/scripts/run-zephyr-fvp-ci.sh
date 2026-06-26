#!/usr/bin/env bash
# Zephyr FVP runtime CI phases.

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
BUILD_ROOT="${ROOT_DIR}/build/zephyr-fvp"
LOG_DIR="${BUILD_ROOT}/logs"
FVP_BIN_NAME="FVP_BaseR_AEMv8R"
FVP_URL="https://developer.arm.com/-/cdn-downloads/permalink/FVPs-Architecture/FM-11.31/FVP_Base_AEMv8R_11.31_28_Linux_x86.tar.gz"
FVP_SHA256="627500afdb115701b412b85520e5c0e370b7f7e3f425f7ae4b1e8b14cbd4441a"
FVP_INSTALL_DIR="${BUILD_ROOT}/tools/fvp"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

mkdir -p "${LOG_DIR}"

ensure_fvp_available()
{
  local fvp_bin

  fvp_bin="$(command -v "${FVP_BIN_NAME}" || true)"
  if [ -n "${fvp_bin}" ]; then
    ARMFVP_BIN_PATH="$(dirname "${fvp_bin}")"
    export ARMFVP_BIN_PATH
    return
  fi

  if [ "$(uname -m)" != "x86_64" ]; then
    echo "${FVP_BIN_NAME} is available from Arm as a Linux x86 host binary only." >&2
    echo "Run Zephyr FVP validation on an amd64/x86_64 runner or devcontainer image." >&2
    exit 1
  fi

  echo "${FVP_BIN_NAME} not found; installing FVP from public ARM CDN..."
  mkdir -p "${FVP_INSTALL_DIR}"
  wget -q --show-progress --progress=bar:force:noscroll \
    "${FVP_URL}" -O "${BUILD_ROOT}/fvp.tar.gz"
  printf '%s  %s\n' "${FVP_SHA256}" "${BUILD_ROOT}/fvp.tar.gz" | sha256sum -c -
  tar -xzf "${BUILD_ROOT}/fvp.tar.gz" -C "${FVP_INSTALL_DIR}" --strip-components=1
  rm "${BUILD_ROOT}/fvp.tar.gz"

  if [ ! -x "${FVP_INSTALL_DIR}/bin/${FVP_BIN_NAME}" ]; then
    echo "Missing FVP binary after install: ${FVP_INSTALL_DIR}/bin/${FVP_BIN_NAME}" >&2
    exit 1
  fi

  export ARMFVP_BIN_PATH="${FVP_INSTALL_DIR}/bin"
}

ensure_fvp_available

# FVP run timeout
FVP_TIMEOUT_SECONDS="${FVP_TIMEOUT_SECONDS:-90}"

build_variant()
{
  local name="$1"
  shift

  "${ROOT_DIR}/build.sh" --platform zephyr-fvp -d "${BUILD_ROOT}/${name}" "$@"
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

echo "Phase 5 - Zephyr FVP TAP network build smoke"
"${ROOT_DIR}/build.sh" --platform zephyr-fvp --network tap -d "${ROOT_DIR}/build/zephyr-fvp-tap"
test -f "${ROOT_DIR}/build/zephyr-fvp-tap/zephyr/zephyr.elf"

echo "Zephyr FVP runtime validation OK"
