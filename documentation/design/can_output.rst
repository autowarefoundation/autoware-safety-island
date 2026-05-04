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

- FVP Zephyr target: ``DDS_ONLY``.
- S32Z Zephyr target: ``DDS_AND_CAN``.
- FreeRTOS POSIX simulator: ``DDS_AND_CAN`` so CI exercises the mock CAN path.

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

FreeRTOS POSIX uses an in-memory mock backend. The test program
``actuation_module/test/can_output_test.cpp`` validates the encoder, output
modes, and recorded mock frames. CI builds and runs this as the FreeRTOS CAN
output test phase.
