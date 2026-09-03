#!/usr/bin/env bash
# SocketCAN vcan0 roundtrip. Exit 77 if vcan cannot be created.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${1:-}"

if [ -z "${BIN}" ]; then
  for candidate in \
    "${ROOT_DIR}/build/freertos-posix-can/can_vcan_roundtrip" \
    "${ROOT_DIR}/build/freertos-posix/can_vcan_roundtrip"
  do
    if [ -x "${candidate}" ]; then
      BIN="${candidate}"
      break
    fi
  done
fi

if [ -z "${BIN}" ] || [ ! -x "${BIN}" ]; then
  echo "can_vcan_roundtrip not found. Build first:" >&2
  echo "  ./build.sh --platform freertos-posix --can-output-test --control-output DDS_AND_CAN" >&2
  exit 1
fi

setup_vcan() {
  if ip link show vcan0 >/dev/null 2>&1; then
    ip link set up vcan0 >/dev/null 2>&1 || true
    return 0
  fi
  if ip link add vcan0 type vcan 2>/dev/null && ip link set up vcan0 2>/dev/null; then
    return 0
  fi
  echo "SKIP: cannot create vcan0 (need CAP_NET_ADMIN and the vcan module)" >&2
  exit 77
}

setup_vcan
exec "${BIN}"
