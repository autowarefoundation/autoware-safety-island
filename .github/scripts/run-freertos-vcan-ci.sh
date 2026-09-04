#!/usr/bin/env bash
# Privileged SocketCAN vcan roundtrip. Requires CAP_NET_ADMIN.

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
BUILD_ROOT="${ROOT_DIR}/build/freertos-posix-can"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

cd "${ROOT_DIR}"

echo "FreeRTOS POSIX SocketCAN vcan roundtrip"
"${ROOT_DIR}/build.sh" --platform freertos-posix -d "${BUILD_ROOT}" \
  --can-output-test --control-output DDS_AND_CAN

set +e
"${ROOT_DIR}/actuation_module/test/run-vcan-roundtrip.sh" \
  "${BUILD_ROOT}/can_vcan_roundtrip"
vcan_rc=$?
set -e
if [ "${vcan_rc}" = "77" ]; then
  echo "vcan roundtrip skipped (no CAP_NET_ADMIN or vcan module)"
  exit 0
fi
if [ "${vcan_rc}" != "0" ]; then
  echo "vcan roundtrip failed: ${vcan_rc}" >&2
  exit "${vcan_rc}"
fi
echo "vcan roundtrip OK"

echo "UDP tunnel gateway → vcan roundtrip"
set +e
"${ROOT_DIR}/actuation_module/test/run-udp-tunnel-roundtrip.sh"
udp_rc=$?
set -e
if [ "${udp_rc}" = "77" ]; then
  echo "UDP tunnel vcan roundtrip skipped (no CAP_NET_ADMIN or vcan module)"
  exit 0
fi
if [ "${udp_rc}" != "0" ]; then
  echo "UDP tunnel vcan roundtrip failed: ${udp_rc}" >&2
  exit "${udp_rc}"
fi
echo "UDP tunnel vcan roundtrip OK"
