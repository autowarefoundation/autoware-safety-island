#!/usr/bin/env python3
"""Golden-vector checks for decoder.py against the C++ encoder contract."""

from __future__ import annotations

import struct
import sys

from decoder import ControlCommandDecoder, DecoderEvent


def pack_cycle(sequence: int) -> list[tuple[int, bytes]]:
    lateral = struct.pack("<ii", 125000, -500000)
    longitudinal = struct.pack("<ii", 12250, -1500)
    status = bytes([1, 0x0B]) + struct.pack("<H", sequence) + struct.pack("<I", 12345)
    return [(0x100, lateral), (0x101, longitudinal), (0x102, status)]


def feed(decoder: ControlCommandDecoder, can_id: int, data: bytes, now: float, dlc: int = 8):
    return decoder.feed(can_id, data, dlc, False, now)


def main() -> int:
    frames = pack_cycle(7)
    decoder = ControlCommandDecoder()
    assert feed(decoder, *frames[0], 0.0) == DecoderEvent.STORED
    assert feed(decoder, *frames[1], 0.0) == DecoderEvent.STORED
    assert feed(decoder, *frames[2], 0.0) == DecoderEvent.ACCEPTED
    assert abs(decoder.command.steering_tire_angle - 0.125) < 1e-9
    assert abs(decoder.command.steering_tire_rotation_rate + 0.5) < 1e-9
    assert abs(decoder.command.velocity - 12.25) < 1e-9
    assert abs(decoder.command.acceleration + 1.5) < 1e-9
    assert decoder.command.sequence == 7

    assert feed(decoder, 0x200, bytes(8), 0.1) == DecoderEvent.IGNORED

    gap = pack_cycle(9)
    assert feed(decoder, *gap[0], 0.2) == DecoderEvent.STORED
    assert feed(decoder, *gap[1], 0.2) == DecoderEvent.STORED
    assert feed(decoder, *gap[2], 0.2) == DecoderEvent.REJECTED

    nxt = pack_cycle(8)
    assert feed(decoder, *nxt[0], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *nxt[1], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *nxt[2], 0.3) == DecoderEvent.ACCEPTED
    assert decoder.poll_watchdog(0.81, 0.5) == DecoderEvent.SAFE_STOP
    assert decoder.in_safe_stop

    reorder = ControlCommandDecoder()
    cyc = pack_cycle(0)
    assert feed(reorder, *cyc[1], 0.0) == DecoderEvent.STORED
    assert feed(reorder, *cyc[0], 0.0) == DecoderEvent.STORED
    assert feed(reorder, *cyc[2], 0.0) == DecoderEvent.ACCEPTED

    missing = ControlCommandDecoder()
    assert feed(missing, *cyc[1], 0.0) == DecoderEvent.STORED
    assert feed(missing, *cyc[2], 0.0) == DecoderEvent.REJECTED
    assert feed(missing, *cyc[0], 0.0, dlc=7) == DecoderEvent.IGNORED
    print("decoder golden vectors passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
