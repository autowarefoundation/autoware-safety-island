#!/usr/bin/env bash
# Privileged FVP TAP UDP → gateway → vcan0. Exit 77 if tap/vcan cannot be created.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build/zephyr-fvp-tap-can}"
TAP="${FVP_TAP_INTERFACE:-tap0}"
IFACE="${SAFETY_ISLAND_CAN_IFACE:-vcan0}"
TIMEOUT_SECONDS="${FVP_TAP_TIMEOUT_SECONDS:-120}"

if [ ! -e /dev/net/tun ]; then
  echo "SKIP: /dev/net/tun is missing" >&2
  exit 77
fi

if [ ! -f "${BUILD_DIR}/zephyr/zephyr.elf" ]; then
  echo "Missing ELF: ${BUILD_DIR}/zephyr/zephyr.elf" >&2
  echo "Build first: ./build.sh --platform zephyr-fvp --network tap --can-output-test --control-output CAN_ONLY -d ${BUILD_DIR}" >&2
  exit 1
fi

if ! ip tuntap add dev "${TAP}" mode tap 2>/dev/null; then
  if ! ip link show "${TAP}" >/dev/null 2>&1; then
    echo "SKIP: cannot create ${TAP} (need CAP_NET_ADMIN and tun)" >&2
    exit 77
  fi
fi
ip addr replace 192.168.10.1/24 dev "${TAP}"
ip link set dev "${TAP}" up

if ! ip link show "${IFACE}" >/dev/null 2>&1; then
  if ! ip link add "${IFACE}" type vcan 2>/dev/null; then
    echo "SKIP: cannot create ${IFACE}" >&2
    exit 77
  fi
fi
ip link set up "${IFACE}"

accepted_file="$(mktemp)"
gateway_log="$(mktemp)"
fvp_log="$(mktemp)"
python3 "${ROOT_DIR}/demo/can_tunnel_bridge/gateway.py" \
  --bind 192.168.10.1 --port 5555 --interface "${IFACE}" \
  >"${gateway_log}" 2>&1 &
gateway_pid=$!

python3 - "${ROOT_DIR}" "${IFACE}" "${accepted_file}" <<'PY' &
import os
import socket
import struct
import sys
import time

root, iface, out = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.join(root, "demo/can_carla_bridge"))
from decoder import ControlCommandDecoder, DecoderEvent

can_sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
can_sock.bind((iface,))
can_sock.settimeout(1.0)
fmt = struct.Struct("=IB3x8s")
decoder = ControlCommandDecoder()
accepted = 0
deadline = time.monotonic() + 90.0
while time.monotonic() < deadline:
    try:
        raw = can_sock.recv(16)
    except socket.timeout:
        continue
    can_id, dlc, data = fmt.unpack(raw)
    event = decoder.feed(can_id, data[:dlc], dlc, False, time.monotonic())
    if event == DecoderEvent.ACCEPTED:
        accepted += 1
        if accepted >= 1:
            break
with open(out, "w", encoding="utf-8") as handle:
    handle.write(str(accepted))
PY
collector_pid=$!

cleanup() {
  kill "${gateway_pid}" "${collector_pid}" 2>/dev/null || true
  wait "${gateway_pid}" 2>/dev/null || true
  wait "${collector_pid}" 2>/dev/null || true
  rm -f "${accepted_file}" "${gateway_log}" "${fvp_log}"
}
trap cleanup EXIT

set +e
timeout --kill-after=5s "${TIMEOUT_SECONDS}s" west build -d "${BUILD_DIR}" --target run >"${fvp_log}" 2>&1
fvp_rc=$?
set -e
wait "${collector_pid}" 2>/dev/null || true

if ! grep -q "CAN output tests passed" "${fvp_log}"; then
  echo "FVP UART did not report CAN output tests passed" >&2
  cat "${fvp_log}" >&2
  cat "${gateway_log}" >&2
  exit 1
fi

accepted="$(cat "${accepted_file}" 2>/dev/null || echo 0)"
if [ "${accepted}" -lt 1 ]; then
  echo "gateway/vcan decoder received no accepted command" >&2
  cat "${gateway_log}" >&2
  cat "${fvp_log}" >&2
  exit 1
fi

echo "FVP TAP UDP tunnel roundtrip OK (${accepted} command(s))"
# timeout 124 is expected if FVP does not exit after the test.
if [ "${fvp_rc}" != "0" ] && [ "${fvp_rc}" != "124" ] && [ "${fvp_rc}" != "137" ]; then
  echo "FVP exited ${fvp_rc}" >&2
  exit "${fvp_rc}"
fi
