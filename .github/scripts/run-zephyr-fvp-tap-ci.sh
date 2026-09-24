#!/usr/bin/env bash
# Privileged Zephyr FVP TAP UDP tunnel. Requires CAP_NET_ADMIN and /dev/net/tun.
#
# The workflow runs this in the devcontainer with host networking so the vcan0
# interface prepared on the runner is visible and FVP can attach to host tap0.
# The Zephyr workspace cache is restored by the workflow; initialize it here
# when that cache is cold.

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
BUILD_ROOT="${ROOT_DIR}/build/zephyr-fvp-tap-can"
TAP="${FVP_TAP_INTERFACE:-tap0}"
IFACE="${SAFETY_ISLAND_CAN_IFACE:-vcan0}"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

# Local runs may skip when the privileged setup is unavailable; on CI the
# workflow provides tun and vcan, so the same condition fails the job.
skip_or_fail()
{
  if [ -n "${GITHUB_ACTIONS:-}" ]; then
    echo "$1 (required on CI)" >&2
    exit 1
  fi
  echo "SKIP: $1"
  exit 0
}

# Probe the tunnel prerequisites before the FVP download and Zephyr build so
# an unusable environment fails in seconds instead of after several minutes.
ensure_tunnel_devices()
{
  [ -e /dev/net/tun ] || skip_or_fail "/dev/net/tun is missing"
  if ! ip link show "${TAP}" >/dev/null 2>&1; then
    ip tuntap add dev "${TAP}" mode tap 2>/dev/null ||
      skip_or_fail "cannot create ${TAP} (need CAP_NET_ADMIN and tun)"
  fi
  if ! ip link show "${IFACE}" >/dev/null 2>&1; then
    ip link add "${IFACE}" type vcan 2>/dev/null ||
      skip_or_fail "cannot create ${IFACE} (need CAP_NET_ADMIN and the vcan module)"
  fi
}

ensure_zephyr_workspace()
{
  pip3 install -q -r "${ROOT_DIR}/zephyr/scripts/requirements-base.txt"
  pip3 install -q -r "${ROOT_DIR}/zephyr/scripts/requirements-build-test.txt"
  (
    cd "${ROOT_DIR}"
    if [ ! -d .west ]; then
      west init -l actuation_module
      west update
    fi
    west zephyr-export
  )
}

mkdir -p "${ROOT_DIR}/build"
ensure_tunnel_devices
ensure_zephyr_workspace
ensure_fvp_available

echo "Zephyr FVP TAP UDP tunnel build"
"${ROOT_DIR}/build.sh" --platform zephyr-fvp --network tap --can-output-test \
  --control-output CAN_ONLY -d "${BUILD_ROOT}"

set +e
"${ROOT_DIR}/actuation_module/test/run-fvp-tap-tunnel.sh" "${BUILD_ROOT}"
tap_rc=$?
set -e
if [ "${tap_rc}" = "77" ]; then
  skip_or_fail "FVP TAP tunnel setup failed (no CAP_NET_ADMIN, tun, or vcan)"
fi
if [ "${tap_rc}" != "0" ]; then
  echo "FVP TAP tunnel failed: ${tap_rc}" >&2
  exit "${tap_rc}"
fi
echo "FVP TAP tunnel OK"
