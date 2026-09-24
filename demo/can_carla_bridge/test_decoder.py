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


def pack_fd_cycle(sequence: int) -> bytes:
    return b"".join(data for _, data in pack_cycle(sequence))


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

    jumped = pack_cycle(9)
    assert feed(decoder, *jumped[0], 0.2) == DecoderEvent.STORED
    assert feed(decoder, *jumped[1], 0.2) == DecoderEvent.STORED
    assert feed(decoder, *jumped[2], 0.2) == DecoderEvent.ACCEPTED
    assert decoder.command.sequence == 9

    stale = pack_cycle(8)
    assert feed(decoder, *stale[0], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *stale[1], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *stale[2], 0.3) == DecoderEvent.REJECTED

    replay = pack_cycle(9)
    assert feed(decoder, *replay[0], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *replay[1], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *replay[2], 0.3) == DecoderEvent.REJECTED

    nxt = pack_cycle(10)
    assert feed(decoder, *nxt[0], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *nxt[1], 0.3) == DecoderEvent.STORED
    assert feed(decoder, *nxt[2], 0.3) == DecoderEvent.ACCEPTED
    assert decoder.poll_watchdog(0.81, 0.5) == DecoderEvent.SAFE_STOP
    assert decoder.in_safe_stop
    for frame, event in zip(
        pack_cycle(40), (DecoderEvent.STORED, DecoderEvent.STORED, DecoderEvent.ACCEPTED)
    ):
        assert feed(decoder, *frame, 1.0) == event
    assert not decoder.in_safe_stop

    reorder = ControlCommandDecoder()
    cyc = pack_cycle(0)
    assert feed(reorder, *cyc[1], 0.0) == DecoderEvent.STORED
    assert feed(reorder, *cyc[0], 0.0) == DecoderEvent.STORED
    assert feed(reorder, *cyc[2], 0.0) == DecoderEvent.ACCEPTED

    missing = ControlCommandDecoder()
    assert feed(missing, *cyc[1], 0.0) == DecoderEvent.STORED
    assert feed(missing, *cyc[2], 0.0) == DecoderEvent.REJECTED
    assert feed(missing, *cyc[0], 0.0, dlc=7) == DecoderEvent.IGNORED

    duplicate = ControlCommandDecoder()
    assert feed(duplicate, *cyc[0], 0.0) == DecoderEvent.STORED
    changed_lateral = bytes([cyc[0][1][0] + 1]) + cyc[0][1][1:]
    assert feed(duplicate, 0x100, changed_lateral, 0.0) == DecoderEvent.STORED
    assert feed(duplicate, *cyc[1], 0.0) == DecoderEvent.STORED
    assert feed(duplicate, *cyc[2], 0.0) == DecoderEvent.ACCEPTED
    assert abs(duplicate.command.steering_tire_angle - 0.125001) < 1e-9

    bad_dlc = ControlCommandDecoder()
    assert feed(bad_dlc, *cyc[0], 0.0, dlc=7) == DecoderEvent.IGNORED
    assert feed(bad_dlc, *cyc[1], 0.0) == DecoderEvent.STORED
    assert feed(bad_dlc, *cyc[2], 0.0) == DecoderEvent.REJECTED

    extended = ControlCommandDecoder()
    assert extended.feed(*cyc[0], 8, True, 0.0) == DecoderEvent.IGNORED
    assert feed(extended, *cyc[1], 0.0) == DecoderEvent.STORED
    assert feed(extended, *cyc[2], 0.0) == DecoderEvent.REJECTED

    wrap = ControlCommandDecoder()
    last = pack_cycle(65535)
    assert feed(wrap, *last[0], 0.0) == DecoderEvent.STORED
    assert feed(wrap, *last[1], 0.0) == DecoderEvent.STORED
    assert feed(wrap, *last[2], 0.0) == DecoderEvent.ACCEPTED
    zero = pack_cycle(0)
    assert feed(wrap, *zero[0], 0.1) == DecoderEvent.STORED
    assert feed(wrap, *zero[1], 0.1) == DecoderEvent.STORED
    assert feed(wrap, *zero[2], 0.1) == DecoderEvent.ACCEPTED
    wrap_replay = pack_cycle(65535)
    assert feed(wrap, *wrap_replay[0], 0.2) == DecoderEvent.STORED
    assert feed(wrap, *wrap_replay[1], 0.2) == DecoderEvent.STORED
    assert feed(wrap, *wrap_replay[2], 0.2) == DecoderEvent.REJECTED

    fd = ControlCommandDecoder()
    fd_payload = pack_fd_cycle(7)
    assert fd.feed_fd(0x103, fd_payload, 24, 0.0) == DecoderEvent.ACCEPTED
    assert abs(fd.command.steering_tire_angle - 0.125) < 1e-9
    assert abs(fd.command.velocity - 12.25) < 1e-9
    assert abs(fd.command.acceleration + 1.5) < 1e-9
    assert fd.command.sequence == 7

    assert fd.feed_fd(0x103, fd_payload, 24, 0.1) == DecoderEvent.REJECTED
    assert fd.feed_fd(0x103, pack_fd_cycle(8), 23, 0.1) == DecoderEvent.IGNORED
    assert fd.feed_fd(0x102, pack_fd_cycle(8), 24, 0.1) == DecoderEvent.IGNORED
    assert fd.feed_fd(0x103, pack_fd_cycle(8)[:16], 24, 0.1) == DecoderEvent.IGNORED
    assert fd.feed_fd(0x103, pack_fd_cycle(8), 24, 0.1) == DecoderEvent.ACCEPTED
    assert fd.command.sequence == 8

    print("decoder golden vectors passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
