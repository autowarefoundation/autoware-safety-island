"""48-byte classic CAN UDP tunnel datagram (magic CT)."""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = b"CT"
VERSION = 1
FLAG_CLASSIC_BATCH = 0x01
DATAGRAM_SIZE = 48
LATERAL_ID = 0x100
LONGITUDINAL_ID = 0x101
STATUS_ID = 0x102

_HEADER = struct.Struct("<2sBBHH")
_FRAME = struct.Struct("<IB8s")
_DATAGRAM = struct.Struct("<2sBBHH" + "IB8s" * 3 + "B")


@dataclass
class TunnelFrame:
    can_id: int
    dlc: int
    data: bytes


@dataclass
class TunnelBatch:
    sequence: int
    frames: tuple[TunnelFrame, TunnelFrame, TunnelFrame]


def _status_sequence(data: bytes) -> int:
    return struct.unpack_from("<H", data, 2)[0]


def pack(batch: TunnelBatch) -> bytes:
    frames = batch.frames
    if len(frames) != 3:
        raise ValueError("batch must contain three frames")
    expected = (LATERAL_ID, LONGITUDINAL_ID, STATUS_ID)
    for frame, can_id in zip(frames, expected, strict=True):
        if frame.can_id != can_id or frame.dlc != 8 or len(frame.data) != 8:
            raise ValueError(f"frame must be classic 0x{can_id:03x} DLC 8")
    if _status_sequence(frames[2].data) != batch.sequence:
        raise ValueError("batch sequence must match 0x102")
    packed_frames = b"".join(_FRAME.pack(frame.can_id, frame.dlc, frame.data) for frame in frames)
    return _HEADER.pack(MAGIC, VERSION, FLAG_CLASSIC_BATCH, batch.sequence, 0) + packed_frames + b"\x00"


def unpack(datagram: bytes) -> TunnelBatch:
    if len(datagram) != DATAGRAM_SIZE:
        raise ValueError("datagram length must be 48")
    magic, version, flags, sequence, reserved, *rest = _DATAGRAM.unpack(datagram)
    if magic != MAGIC:
        raise ValueError("invalid magic")
    if version != VERSION:
        raise ValueError("unsupported version")
    if flags != FLAG_CLASSIC_BATCH:
        raise ValueError("unsupported flags")
    if reserved != 0 or rest[-1] != 0:
        raise ValueError("reserved bytes must be zero")
    frames = []
    raw = rest[:-1]
    for index in range(3):
        can_id, dlc, data = raw[index * 3 : index * 3 + 3]
        frames.append(TunnelFrame(can_id=can_id, dlc=dlc, data=data))
    expected = (LATERAL_ID, LONGITUDINAL_ID, STATUS_ID)
    for frame, can_id in zip(frames, expected, strict=True):
        if frame.can_id != can_id or frame.dlc != 8:
            raise ValueError(f"frame must be classic 0x{can_id:03x} DLC 8")
    if _status_sequence(frames[2].data) != sequence:
        raise ValueError("batch sequence must match 0x102")
    return TunnelBatch(sequence=sequence, frames=tuple(frames))
