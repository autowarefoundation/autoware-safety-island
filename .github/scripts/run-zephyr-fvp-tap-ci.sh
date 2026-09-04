#!/usr/bin/env bash
# Privileged Zephyr FVP TAP UDP tunnel. Requires CAP_NET_ADMIN and /dev/net/tun.

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
BUILD_ROOT="${ROOT_DIR}/build/zephyr-fvp-tap-can"
FVP_BIN_NAME="FVP_BaseR_AEMv8R"
FVP_URL="https://developer.arm.com/-/cdn-downloads/permalink/FVPs-Architecture/FM-11.31/FVP_Base_AEMv8R_11.31_28_Linux_x86.tar.gz"
FVP_SHA256="627500afdb115701b412b85520e5c0e370b7f7e3f425f7ae4b1e8b14cbd4441a"
FVP_INSTALL_DIR="${ROOT_DIR}/build/zephyr-fvp/tools/fvp"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

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
    exit 1
  fi
  echo "${FVP_BIN_NAME} not found; installing FVP from public ARM CDN..."
  mkdir -p "${FVP_INSTALL_DIR}"
  wget -q --show-progress --progress=bar:force:noscroll \
    "${FVP_URL}" -O "${ROOT_DIR}/build/fvp-tap.tar.gz"
  printf '%s  %s\n' "${FVP_SHA256}" "${ROOT_DIR}/build/fvp-tap.tar.gz" | sha256sum -c -
  tar -xzf "${ROOT_DIR}/build/fvp-tap.tar.gz" -C "${FVP_INSTALL_DIR}" --strip-components=1
  rm "${ROOT_DIR}/build/fvp-tap.tar.gz"
  export ARMFVP_BIN_PATH="${FVP_INSTALL_DIR}/bin"
}

mkdir -p "${ROOT_DIR}/build"
ensure_fvp_available

echo "Zephyr FVP TAP UDP tunnel build"
"${ROOT_DIR}/build.sh" --platform zephyr-fvp --network tap --can-output-test \
  --control-output CAN_ONLY -d "${BUILD_ROOT}"

set +e
"${ROOT_DIR}/actuation_module/test/run-fvp-tap-tunnel.sh" "${BUILD_ROOT}"
tap_rc=$?
set -e
if [ "${tap_rc}" = "77" ]; then
  echo "FVP TAP tunnel skipped (no CAP_NET_ADMIN, tun, or vcan)"
  exit 0
fi
if [ "${tap_rc}" != "0" ]; then
  echo "FVP TAP tunnel failed: ${tap_rc}" >&2
  exit "${tap_rc}"
fi
echo "FVP TAP tunnel OK"
