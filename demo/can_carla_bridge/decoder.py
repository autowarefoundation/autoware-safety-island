"""Assemble classic 0x100/0x101/0x102 or one 24-byte CAN-FD 0x103, CRC-checked."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional


LATERAL_ID = 0x100
LONGITUDINAL_ID = 0x101
STATUS_ID = 0x102
FD_COMMAND_ID = 0x103
FD_PAYLOAD_LENGTH = 24

# Status frame (0x102) layout. Keep in sync with
# actuation_module/include/common/can/control_command_encoder.hpp.
#   byte 0    output mode (bits 0-1), supervisor decision (bits 4-5)
#   byte 1    flags
#   bytes 2-3 sequence, little-endian
#   bytes 4-5 producer timestamp in ms modulo 2^16, little-endian
#   byte 6    fault id (0 = no fault latched)
#   byte 7    CRC-8 SAE J1850 over the 0x100 payload, the 0x101 payload and
#             bytes 0-6 of this frame
STATUS_MODE_MASK = 0x03
STATUS_DECISION_SHIFT = 4
STATUS_DECISION_MASK = 0x03
STATUS_FLAG_STEERING_RATE_DEFINED = 0x01
STATUS_FLAG_ACCELERATION_DEFINED = 0x02
STATUS_FLAG_JERK_DEFINED = 0x04
STATUS_FLAG_COMMAND_VALID = 0x08
STATUS_FLAG_SUPERVISED = 0x10

DECISION_NORMAL = 0
DECISION_SI_STOP = 1
DECISION_HOLD = 2
DECISION_NAMES = {DECISION_NORMAL: "NORMAL", DECISION_SI_STOP: "SI_STOP", DECISION_HOLD: "HOLD"}


class DecoderEvent(Enum):
    IGNORED = auto()
    STORED = auto()
    ACCEPTED = auto()
    REJECTED = auto()
    SAFE_STOP = auto()


@dataclass
class DecodedControlCommand:
    steering_tire_angle: float = 0.0
    steering_tire_rotation_rate: float = 0.0
    steering_rate_defined: bool = False
    velocity: float = 0.0
    acceleration: float = 0.0
    acceleration_defined: bool = False
    jerk_defined: bool = False
    sequence: int = 0
    output_mode: int = 0
    decision: int = DECISION_NORMAL
    fault_id: int = 0


def _i32_le(data: bytes, offset: int) -> int:
    raw = int.from_bytes(data[offset : offset + 4], "little", signed=False)
    if raw >= 2**31:
        raw -= 2**32
    return raw


def _u16_le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=False)


def crc8_j1850(data: bytes) -> int:
    """CRC-8 SAE J1850: polynomial 0x1D, init 0xFF, final XOR 0xFF."""
    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc ^ 0xFF


def pack_status(
    lateral: bytes,
    longitudinal: bytes,
    sequence: int,
    *,
    mode: int = 1,
    flags: int = 0x1B,
    timestamp_ms: int = 12345,
    decision: int = DECISION_NORMAL,
    fault_id: int = 0,
) -> bytes:
    """Build the 0x102 payload for a command, CRC included (tests and tools)."""
    head = bytes(
        [
            (mode & STATUS_MODE_MASK) | ((decision & STATUS_DECISION_MASK) << STATUS_DECISION_SHIFT),
            flags,
        ]
    )
    body = (
        head
        + (sequence & 0xFFFF).to_bytes(2, "little")
        + (timestamp_ms & 0xFFFF).to_bytes(2, "little")
        + bytes([fault_id & 0xFF])
    )
    return body + bytes([crc8_j1850(lateral[:8] + longitudinal[:8] + body)])


def _seq_delta(sequence: int, expected: int) -> int:
    return ((sequence - expected + 32768) & 0xFFFF) - 32768


class ControlCommandDecoder:
    def __init__(self) -> None:
        self._has_lateral = False
        self._has_longitudinal = False
        self._lateral: Optional[bytes] = None
        self._longitudinal: Optional[bytes] = None
        self._command = DecodedControlCommand()
        self._has_command = False
        self._in_safe_stop = False
        self._has_expected = False
        self._expected = 0
        self._has_anchor = False
        self._anchor = 0.0

    def feed(
        self, can_id: int, data: bytes, dlc: int, extended: bool, now: float
    ) -> DecoderEvent:
        self._note_time(now)
        if extended or dlc != 8 or len(data) < 8:
            return DecoderEvent.IGNORED
        payload = bytes(data[:8])
        if can_id == LATERAL_ID:
            self._lateral = payload
            self._has_lateral = True
            return DecoderEvent.STORED
        if can_id == LONGITUDINAL_ID:
            self._longitudinal = payload
            self._has_longitudinal = True
            return DecoderEvent.STORED
        if can_id != STATUS_ID:
            return DecoderEvent.IGNORED
        if not self._has_lateral or not self._has_longitudinal:
            self._clear_pending()
            return DecoderEvent.REJECTED
        assert self._lateral is not None
        assert self._longitudinal is not None
        # Integrity first: nothing else in the frame is trusted until the CRC
        # over all three payloads matches. A status frame without the
        # supervised flag is the earlier layout (no CRC, no decision).
        decision = (payload[0] >> STATUS_DECISION_SHIFT) & STATUS_DECISION_MASK
        if (
            not payload[1] & STATUS_FLAG_SUPERVISED
            or crc8_j1850(self._lateral + self._longitudinal + payload[:7]) != payload[7]
            or decision not in DECISION_NAMES
        ):
            self._clear_pending()
            return DecoderEvent.REJECTED
        sequence = _u16_le(payload, 2)
        if self._has_expected and _seq_delta(sequence, self._expected) < 0:
            self._clear_pending()
            return DecoderEvent.REJECTED
        self._command = DecodedControlCommand(
            steering_tire_angle=_i32_le(self._lateral, 0) / 1_000_000.0,
            steering_tire_rotation_rate=_i32_le(self._lateral, 4) / 1_000_000.0,
            velocity=_i32_le(self._longitudinal, 0) / 1_000.0,
            acceleration=_i32_le(self._longitudinal, 4) / 1_000.0,
            output_mode=payload[0] & STATUS_MODE_MASK,
            steering_rate_defined=bool(payload[1] & STATUS_FLAG_STEERING_RATE_DEFINED),
            acceleration_defined=bool(payload[1] & STATUS_FLAG_ACCELERATION_DEFINED),
            jerk_defined=bool(payload[1] & STATUS_FLAG_JERK_DEFINED),
            sequence=sequence,
            decision=decision,
            fault_id=payload[6],
        )
        self._has_command = True
        self._in_safe_stop = False
        self._has_expected = True
        self._expected = (sequence + 1) & 0xFFFF
        self._anchor = now
        self._has_anchor = True
        self._clear_pending()
        return DecoderEvent.ACCEPTED

    def feed_fd(
        self, can_id: int, data: bytes, length: int, extended: bool, now: float
    ) -> DecoderEvent:
        """Decode one CAN-FD command frame (0x103, 24 payload bytes).

        The payload carries the classic lateral, longitudinal, and status
        slices in order, so commit, replay, and watchdog rules are shared.
        Extended-ID frames are ignored like classic ones.
        """
        self._note_time(now)
        if (
            extended
            or can_id != FD_COMMAND_ID
            or length != FD_PAYLOAD_LENGTH
            or len(data) < FD_PAYLOAD_LENGTH
        ):
            return DecoderEvent.IGNORED
        payload = bytes(data[:FD_PAYLOAD_LENGTH])
        self.feed(LATERAL_ID, payload[0:8], 8, False, now)
        self.feed(LONGITUDINAL_ID, payload[8:16], 8, False, now)
        return self.feed(STATUS_ID, payload[16:24], 8, False, now)

    def poll_watchdog(self, now: float, timeout_sec: float) -> DecoderEvent:
        self._note_time(now)
        if self._in_safe_stop:
            return DecoderEvent.SAFE_STOP
        if not self._has_anchor:
            return DecoderEvent.IGNORED
        if (now - self._anchor) <= timeout_sec:
            return DecoderEvent.IGNORED
        self._in_safe_stop = True
        self._has_expected = False
        self._clear_pending()
        return DecoderEvent.SAFE_STOP

    @property
    def command(self) -> DecodedControlCommand:
        return self._command

    @property
    def has_command(self) -> bool:
        return self._has_command

    @property
    def in_safe_stop(self) -> bool:
        return self._in_safe_stop

    def _note_time(self, now: float) -> None:
        if not self._has_anchor:
            self._anchor = now
            self._has_anchor = True

    def _clear_pending(self) -> None:
        self._has_lateral = False
        self._has_longitudinal = False
        self._lateral = None
        self._longitudinal = None
