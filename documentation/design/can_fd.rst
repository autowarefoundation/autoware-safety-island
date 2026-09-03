..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

######
CAN-FD
######

Sketch only. Do not implement a runtime CAN-FD backend until the classic
SocketCAN loop in :doc:`can_carla_integration` works.

Classic CAN remains the demo contract (three 8-byte frames, IDs ``0x100`` /
``0x101`` / ``0x102``). CAN-FD is an optional packing and API extension on the
same ``freertos-posix`` SocketCAN path.

*************
API sketch
*************

Extend ``common::can::CanFrame`` without breaking classic senders:

- ``data`` capacity grows from 8 to 64 bytes; classic frames still set
  ``dlc`` to 8 and use the first eight bytes.
- ``bool fd{false}`` — FDF.
- ``bool brs{false}`` — bit-rate switch.
- Classic ``can_send`` ignores FD flags. An FD-aware backend reads them.

Optional encoder mode (build-time or runtime flag, default off): pack the
current three classic payloads into one FD frame (ID TBD, payload 24 bytes
plus the status word). Decoder on the host bridge must accept either layout.

``freertos-posix`` SocketCAN already used for classic TX can enable
``CAN_RAW_FD_FRAMES`` when the iface is FD-capable (``vcan`` is). Zephyr and
S32Z FD hardware are out of this sketch.

*************
Non-goals
*************

- No OEM FD DBC.
- No CARLA-side FD requirement (the bridge still decodes to
  ``VehicleControl``).
- No change to DDS inputs.
- No FVP CAN-FD transport.

*************
Acceptance for this PR
*************

- This design note in the docs toctree.
- ``CanFrame`` field sketch applied or clearly deferred with the struct
  comment pointing here.
- No requirement to ship an FD encoder, FD CI job, or CARLA demo change.
