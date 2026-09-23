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
     - output mode, validity flags, sequence, timestamp
     - mode byte, flags byte, ``uint16`` sequence, ``uint32`` timestamp in ms

Non-finite command values are rejected before any CAN frame is sent.

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
placeholder contract; they do not change the frame layout.
