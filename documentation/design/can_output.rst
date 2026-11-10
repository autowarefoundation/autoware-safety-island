..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##############
CAN output
##############

The controller can send the final
``autoware_control_msgs/msg/Control`` command over DDS, CAN, or both. DDS
inputs are unchanged; only the final control-command output is gated by this
mode.

**********************
Output modes
**********************

``CONFIG_CONTROL_CMD_OUTPUT_*`` selects one mode at build time:

- ``DDS_ONLY`` — publish ``/control/trajectory_follower/control_cmd`` only.
- ``CAN_ONLY`` — send CAN frames only.
- ``DDS_AND_CAN`` — publish DDS and send CAN frames.

Current defaults:

- ``zephyr-fvp``: ``DDS_ONLY``. TAP plus the UDP tunnel selects
  ``CAN_ONLY`` or ``DDS_AND_CAN`` explicitly.
- ``zephyr-s32z``: ``DDS_AND_CAN``.
- ``freertos-posix``: ``DDS_ONLY``. SocketCAN is selected with
  ``--control-output CAN_ONLY`` or ``DDS_AND_CAN`` and
  ``SAFETY_ISLAND_CAN_IFACE``. The in-memory mock is test-only.
- ``freertos-s32z2`` / ``freertos-x5h``: ``DDS_ONLY``; CAN is not part of
  this stack on those targets.

**********************
Frame format
**********************

The CAN mapping is a placeholder contract intended for integration testing and
early bring-up. Each command produces three classic 8-byte CAN frames:

.. list-table::
   :widths: 20 35 45
   :header-rows: 1

   * - CAN ID
     - Payload
     - Scaling
   * - ``0x100``
     - steering angle, steering rate
     - signed little-endian ``int32``, value * 1,000,000
   * - ``0x101``
     - velocity, acceleration
     - signed little-endian ``int32``, value * 1,000
   * - ``0x102``
     - output mode and supervisor decision, flags, sequence, timestamp,
       fault id, CRC
     - see the status frame layout below

Non-finite command values are rejected before any CAN frame is sent.

Status frame
============

``0x102`` closes a command and tells the receiver what the supervisor decided:

.. list-table::
   :widths: 15 85
   :header-rows: 1

   * - Byte
     - Content
   * - 0
     - bits 0-1: output mode (``0`` DDS_ONLY, ``1`` CAN_ONLY, ``2`` DDS_AND_CAN);
       bits 4-5: supervisor decision (``0`` NORMAL, ``1`` SI_STOP, ``2`` HOLD);
       other bits ``0``
   * - 1
     - flags: ``0x01`` steering rate defined, ``0x02`` acceleration defined,
       ``0x04`` jerk defined, ``0x08`` command valid, ``0x10`` supervised layout
   * - 2-3
     - sequence, little-endian ``uint16``, one step per command
   * - 4-5
     - producer timestamp in ms modulo 65536, little-endian ``uint16``
   * - 6
     - fault id of the latched fault, ``0`` when none is latched (a latched
       fault always maps into 1-255)
   * - 7
     - CRC-8 SAE J1850 (polynomial ``0x1D``, initial value ``0xFF``, final XOR
       ``0xFF``) over the ``0x100`` payload, the ``0x101`` payload and bytes 0-6
       of this frame, in that order

The decision is the one on ``ApprovedRequest``: with CAN as the only output, a
receiver can tell an ``SI_STOP`` from a normal command and read which fault
caused it. The payload of an ``SI_STOP`` or ``HOLD`` command is the stop
command itself, so a receiver that only applies the payload still stops.

The CRC covers all three frames. A receiver must reject a command whose CRC
does not match, which also rejects a command assembled from frames of
different cycles, and must reject a status frame without the ``0x10`` flag:
that is the earlier layout, which carried a 32-bit timestamp in bytes 4-7 and
no decision, fault id or CRC.

``freertos-posix`` can instead send the same three payloads as one 24-byte
CAN-FD frame ``0x103`` when ``SAFETY_ISLAND_CAN_FORMAT=fd`` is set. That mode
is opt-in and documented in :doc:`can_fd`; every other runtime stays classic.

**********************
Backends
**********************

Zephyr uses the selected ``zephyr,canbus`` device. The S32Z ``@D`` overlay sets
this to ``&can0`` in ``actuation_module/boards/s32z270dc2_rtu0_r52@D.overlay``.
Real bus validation requires the board CAN pins to be connected to an external
CAN transceiver and bus.

``freertos-posix`` uses Linux SocketCAN when CAN output is enabled and
``SAFETY_ISLAND_CAN_IFACE`` is set. The in-memory mock is compiled only
into ``--can-output-test``. That test program
(``actuation_module/test/can_output_test.cpp``) validates the encoder,
output modes, and recorded mock frames.

Host SocketCAN (``vcan`` or a real adapter), the Zephyr FVP UDP tunnel, and
the CARLA ego bridge are specified in :doc:`can_carla_integration` and
:doc:`can_carla_implementation_plan`. Those paths sit on top of this
placeholder contract and carry the frames unchanged.
