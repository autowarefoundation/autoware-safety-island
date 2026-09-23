#!/usr/bin/env python3
"""Decode a real SocketCAN command sent by the C++ Safety Island encoder."""

import socket
import struct
import subprocess
import sys
import time

from decoder import ControlCommandDecoder, DecoderEvent


CAN_FRAME = struct.Struct("=IB3x8s")
CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
CAN_SFF_MASK = 0x7FF


def main(sender: str) -> None:
    decoder = ControlCommandDecoder()
    with socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW) as receiver:
        receiver.bind(("vcan0",))
        receiver.settimeout(1.0)
        subprocess.run([sender], check=True, timeout=10)

        for expected_id in (0x100, 0x101, 0x102):
            wire = receiver.recv(CAN_FRAME.size)
            assert len(wire) == CAN_FRAME.size, "expected a classic CAN frame"
            can_id, dlc, data = CAN_FRAME.unpack(wire)
            assert not can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG)
            assert (can_id & CAN_SFF_MASK) == expected_id
            event = decoder.feed(
                can_id & CAN_SFF_MASK, data[:dlc], dlc,
                bool(can_id & CAN_EFF_FLAG), time.monotonic(),
            )

    assert event == DecoderEvent.ACCEPTED
    assert decoder.command.sequence == 0
    assert abs(decoder.command.steering_tire_angle - 0.125) < 1e-6
    assert abs(decoder.command.velocity - 12.25) < 1e-4
    assert abs(decoder.command.acceleration + 1.5) < 1e-4
    assert decoder.poll_watchdog(time.monotonic() + 0.51, 0.5) == DecoderEvent.SAFE_STOP
    assert decoder.in_safe_stop
    print("vcan roundtrip tests passed")


if __name__ == "__main__":
    main(sys.argv[1])
