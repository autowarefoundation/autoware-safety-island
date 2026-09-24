#!/usr/bin/env python3
"""Privilege-free pack/unpack checks for the 48-byte CAN UDP tunnel."""

from __future__ import annotations

import struct
import sys

from datagram import FLAG_CLASSIC_BATCH, MAGIC, TunnelBatch, TunnelFrame, pack, unpack


def sample(sequence: int = 42) -> TunnelBatch:
    lateral = struct.pack("<ii", 125000, -500000)
    longitudinal = struct.pack("<ii", 12250, -1500)
    status = bytes([1, 0x0B]) + struct.pack("<H", sequence) + struct.pack("<I", 12345)
    return TunnelBatch(
        sequence=sequence,
        frames=(
            TunnelFrame(0x100, 8, lateral),
            TunnelFrame(0x101, 8, longitudinal),
            TunnelFrame(0x102, 8, status),
        ),
    )


# Fixed cross-language wire vector. Keep in sync with kGoldenDatagram in
# actuation_module/test/can_output_test.cpp.
GOLDEN = bytes([
    0x43, 0x54, 0x01, 0x01, 0x2A, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x08, 0x48, 0xE8, 0x01,
    0x00, 0xE0, 0x5E, 0xF8, 0xFF, 0x01, 0x01, 0x00,
    0x00, 0x08, 0xDA, 0x2F, 0x00, 0x00, 0x24, 0xFA,
    0xFF, 0xFF, 0x02, 0x01, 0x00, 0x00, 0x08, 0x01,
    0x0B, 0x2A, 0x00, 0x39, 0x30, 0x00, 0x00, 0x00,
])


def main() -> int:
    batch = sample()
    datagram = pack(batch)
    assert len(datagram) == 48
    assert datagram[0:2] == MAGIC
    assert datagram[0] == 0x43 and datagram[1] == 0x54
    assert datagram[2] == 1
    assert datagram[3] == FLAG_CLASSIC_BATCH
    assert datagram == GOLDEN
    restored = unpack(GOLDEN)
    assert restored.sequence == 42
    assert restored.frames[0].can_id == 0x100
    assert restored.frames[1].can_id == 0x101
    assert restored.frames[2].can_id == 0x102
    assert restored.frames[0].data == batch.frames[0].data

    try:
        unpack(datagram[:-1])
        raise AssertionError("short datagram")
    except ValueError:
        pass

    swapped = b"TC" + datagram[2:]
    try:
        unpack(swapped)
        raise AssertionError("TC magic")
    except ValueError:
        pass

    bad_version = datagram[:2] + bytes([2]) + datagram[3:]
    try:
        unpack(bad_version)
        raise AssertionError("version")
    except ValueError:
        pass

    mismatched = TunnelBatch(
        sequence=42,
        frames=(
            batch.frames[0],
            batch.frames[1],
            TunnelFrame(
                0x102,
                8,
                bytes([1, 0x0B]) + struct.pack("<H", 41) + struct.pack("<I", 12345),
            ),
        ),
    )
    try:
        pack(mismatched)
        raise AssertionError("sequence mismatch")
    except ValueError:
        pass

    print("CAN UDP datagram tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
