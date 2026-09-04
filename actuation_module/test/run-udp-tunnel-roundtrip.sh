#!/usr/bin/env bash
# Host UDP gateway → vcan0 roundtrip. Exit 77 if vcan cannot be created.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIND="${UDP_TUNNEL_BIND:-127.0.0.1}"
PORT="${UDP_TUNNEL_PORT:-5555}"
IFACE="${SAFETY_ISLAND_CAN_IFACE:-vcan0}"

setup_vcan() {
  if ip link show "${IFACE}" >/dev/null 2>&1; then
    ip link set up "${IFACE}" >/dev/null 2>&1 || true
    return 0
  fi
  if ip link add "${IFACE}" type vcan 2>/dev/null && ip link set up "${IFACE}" 2>/dev/null; then
    return 0
  fi
  echo "SKIP: cannot create ${IFACE} (need CAP_NET_ADMIN and the vcan module)" >&2
  exit 77
}

setup_vcan

gateway_log="$(mktemp)"
python3 "${ROOT_DIR}/demo/can_tunnel_bridge/gateway.py" \
  --bind "${BIND}" --port "${PORT}" --interface "${IFACE}" \
  >"${gateway_log}" 2>&1 &
gateway_pid=$!
cleanup() {
  kill "${gateway_pid}" 2>/dev/null || true
  wait "${gateway_pid}" 2>/dev/null || true
  rm -f "${gateway_log}"
}
trap cleanup EXIT
sleep 0.2

python3 - "${ROOT_DIR}" "${BIND}" "${PORT}" "${IFACE}" <<'PY'
import os
import socket
import struct
import sys
import time

root, bind, port, iface = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
sys.path.insert(0, os.path.join(root, "demo/can_tunnel_bridge"))
sys.path.insert(0, os.path.join(root, "demo/can_carla_bridge"))
from datagram import TunnelBatch, TunnelFrame, pack
from decoder import ControlCommandDecoder, DecoderEvent

sequence = 7
lateral = struct.pack("<ii", 125000, -500000)
longitudinal = struct.pack("<ii", 12250, -1500)
status = bytes([1, 0x0B]) + struct.pack("<H", sequence) + struct.pack("<I", 12345)
datagram = pack(
    TunnelBatch(
        sequence=sequence,
        frames=(
            TunnelFrame(0x100, 8, lateral),
            TunnelFrame(0x101, 8, longitudinal),
            TunnelFrame(0x102, 8, status),
        ),
    )
)

udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp.sendto(datagram, (bind, port))
udp.sendto(b"short", (bind, port))

can_sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
can_sock.bind((iface,))
can_sock.settimeout(1.0)
fmt = struct.Struct("=IB3x8s")
decoder = ControlCommandDecoder()
accepted = False
deadline = time.monotonic() + 2.0
while time.monotonic() < deadline:
    try:
        raw = can_sock.recv(16)
    except socket.timeout:
        continue
    can_id, dlc, data = fmt.unpack(raw)
    event = decoder.feed(can_id, data[:dlc], dlc, False, time.monotonic())
    if event == DecoderEvent.ACCEPTED:
        accepted = True
        break
if not accepted:
    raise SystemExit("decoder did not accept a tunneled command")
if abs(decoder.command.steering_tire_angle - 0.125) > 1e-9:
    raise SystemExit("decoded steering mismatch")
print("UDP tunnel vcan roundtrip OK")
PY
