#!/usr/bin/env python3
"""UDP CAN tunnel → SocketCAN. One datagram is one classic command batch."""

from __future__ import annotations

import argparse
import os
import socket
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from datagram import unpack

CAN_FRAME = struct.Struct("=IB3x8s")


def open_can(interface: str) -> socket.socket:
    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((interface,))
    return sock


def inject(can_sock: socket.socket, can_id: int, data: bytes) -> None:
    can_sock.send(CAN_FRAME.pack(can_id, len(data), data))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="192.168.10.1")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--interface", default="vcan0")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind((args.bind, args.port))
    can_sock = None if args.dry_run else open_can(args.interface)
    print(f"CAN UDP gateway listening on {args.bind}:{args.port}", flush=True)
    while True:
        payload, addr = udp.recvfrom(256)
        try:
            batch = unpack(payload)
        except ValueError as exc:
            print(f"reject from {addr}: {exc}", flush=True)
            continue
        try:
            if can_sock is not None:
                inject(can_sock, batch.frames[0].can_id, batch.frames[0].data)
                inject(can_sock, batch.frames[1].can_id, batch.frames[1].data)
                inject(can_sock, batch.frames[2].can_id, batch.frames[2].data)
        except OSError as exc:
            print(f"SocketCAN inject failed: {exc}", flush=True)
            continue
        print(
            f"injected seq={batch.sequence} from {addr[0]} "
            f"0x{batch.frames[0].can_id:03x}/"
            f"0x{batch.frames[1].can_id:03x}/"
            f"0x{batch.frames[2].can_id:03x}",
            flush=True,
        )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
