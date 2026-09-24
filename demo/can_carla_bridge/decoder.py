"""Assemble placeholder classic 0x100/0x101/0x102 or one 24-byte CAN-FD 0x103."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional


LATERAL_ID = 0x100
LONGITUDINAL_ID = 0x101
STATUS_ID = 0x102
FD_COMMAND_ID = 0x103
FD_PAYLOAD_LENGTH = 24


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


def _i32_le(data: bytes, offset: int) -> int:
    raw = int.from_bytes(data[offset : offset + 4], "little", signed=False)
    if raw >= 2**31:
        raw -= 2**32
    return raw


def _u16_le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=False)


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
        sequence = _u16_le(payload, 2)
        if self._has_expected and _seq_delta(sequence, self._expected) < 0:
            self._clear_pending()
            return DecoderEvent.REJECTED
        assert self._lateral is not None
        assert self._longitudinal is not None
        self._command = DecodedControlCommand(
            steering_tire_angle=_i32_le(self._lateral, 0) / 1_000_000.0,
            steering_tire_rotation_rate=_i32_le(self._lateral, 4) / 1_000_000.0,
            velocity=_i32_le(self._longitudinal, 0) / 1_000.0,
            acceleration=_i32_le(self._longitudinal, 4) / 1_000.0,
            output_mode=payload[0],
            steering_rate_defined=bool(payload[1] & 0x01),
            acceleration_defined=bool(payload[1] & 0x02),
            jerk_defined=bool(payload[1] & 0x04),
            sequence=sequence,
        )
        self._has_command = True
        self._in_safe_stop = False
        self._has_expected = True
        self._expected = (sequence + 1) & 0xFFFF
        self._anchor = now
        self._has_anchor = True
        self._clear_pending()
        return DecoderEvent.ACCEPTED

    def feed_fd(self, can_id: int, data: bytes, length: int, now: float) -> DecoderEvent:
        """Decode one CAN-FD command frame (0x103, 24 payload bytes).

        The payload carries the classic lateral, longitudinal, and status
        slices in order, so commit, replay, and watchdog rules are shared.
        """
        self._note_time(now)
        if (
            can_id != FD_COMMAND_ID
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
