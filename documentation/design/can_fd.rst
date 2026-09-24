..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

######
CAN-FD
######

CAN-FD is an opt-in transport on the ``freertos-posix`` SocketCAN path. It does
not replace the classic demo contract in :doc:`can_output`: the three 8-byte
frames (``0x100`` / ``0x101`` / ``0x102``) stay the default, and Zephyr, the
FVP UDP tunnel, and S32Z hardware remain classic only.

*************
Enabling it
*************

- Safety Island: build ``freertos-posix`` as usual, then set
  ``SAFETY_ISLAND_CAN_FORMAT=fd`` next to ``SAFETY_ISLAND_CAN_IFACE``. The
  default is ``classic``.
- Host bridge: ``python3 demo/can_carla_bridge/bridge.py --can-format fd``.
  The default is ``classic``.
- ``vcan`` carries CAN-FD frames; current kernels give ``vcan0`` the FD MTU of
  72 bytes. The ``vcan`` roundtrip script sets the FD MTU while the link is
  down before running.

Fail closed: an unknown ``SAFETY_ISLAND_CAN_FORMAT`` value, an interface whose
MTU is below 72 (CAN-FD disabled), or a platform without FD support all make
``can_init()`` fail instead of silently sending classic frames.

*************
Frame format
*************

FD mode replaces the classic three-frame batch with one 24-byte frame:

.. list-table::
   :widths: 18 30 52
   :header-rows: 1

   * - CAN ID
     - Payload
     - Scaling
   * - ``0x103``
     - lateral (8 bytes), longitudinal (8 bytes), status (8 bytes)
     - the three classic payloads in order; see :doc:`can_output`

``length`` is the payload byte count (24). The wire DLC is the CAN-FD encoding
of that length: ``12`` for 24 bytes. FD DLC values 9–15 cover 12/16/20/24/32/
48/64 bytes and are not byte counts. SocketCAN maps ``length`` to the wire DLC;
BRS stays off.

The status slice keeps the classic sequence and timestamp, so the host decoder
applies the same commit, replay, and watchdog rules to both formats. A bridge
instance decodes one format at a time (``--can-format``), ignores frames in
the other format, and ignores extended-ID frames in both.

*************
Non-goals
*************

- No OEM FD DBC.
- No FD on Zephyr, the FVP UDP tunnel, or S32Z hardware.
- No BRS / second bitrate selection yet.
- No change to the classic ``0x100`` / ``0x101`` / ``0x102`` contract.

*************
Validation
*************

- ``actuation_module/test/can_output_test.cpp`` checks the FD encoder frame id,
  byte length 24, BRS off, and every payload offset against the classic
  scaling.
- ``actuation_module/test/can_vcan_sender.cpp`` sends one classic batch and one
  FD frame on ``vcan0``; ``demo/can_carla_bridge/test_vcan_roundtrip.py``
  decodes both and checks the sequence and watchdog rules. The script then
  lowers the vcan MTU to 16 and checks CAN-FD init fails closed.
- ``demo/can_carla_bridge/test_decoder.py`` and ``test_bridge.py`` cover the FD
  golden vectors, extended-ID, replay and length rejection, and format
  exclusivity.
- The ``FreeRTOS POSIX vcan`` CI job runs both formats. No CARLA.

vcan validates framing and the software path only. Bitrate, bus load, bus-off,
and real transceivers need hardware.