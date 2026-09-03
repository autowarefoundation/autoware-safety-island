..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

#########################
CAN and CARLA integration
#########################

Drive a CARLA ego vehicle from Safety Island control output over classic CAN.
CARLA has no native CAN interface, so a host bridge stands in for the vehicle
ECU: SocketCAN RX, decode the placeholder frames from :doc:`can_output`, apply
``carla.VehicleControl``.

This page is the design for the stacked work tracked in
https://github.com/autowarefoundation/autoware-safety-island/issues/43.

.. mermaid::

   graph LR
       SI[Safety Island<br/>freertos-posix]
       VCAN[SocketCAN vcan0]
       Bridge[Host CAN-CARLA bridge]
       CARLA[CARLA ego]

       SI -->|classic CAN 0x100 0x101 0x102| VCAN
       VCAN --> Bridge
       Bridge -->|VehicleControl| CARLA

**********************
Shared decisions
**********************

- **Actuation path is CAN.** The bridge talks to CARLA through the Python API,
  not by republishing ROS control commands. That keeps CAN first-class: CARLA
  is the simulated vehicle, not another ROS hop.
- **DDS inputs stay DDS.** Trajectory, odometry, steering, acceleration, and
  operation mode are unchanged.
- **Placeholder frame contract.** Keep IDs ``0x100`` / ``0x101`` / ``0x102``
  and the scaling in :doc:`can_output` until a vehicle DBC is agreed.
- **One host bridge, one sink.** Open-loop and closed-loop share the same
  SocketCAN → decode → CARLA Python API process. Closed-loop does not add a
  second command path through ``autoware_carla_interface``.
- **Local runtime is** ``freertos-posix`` **plus** ``vcan``. Zephyr FVP CAN is
  loopback-only and cannot reach the host. S32Z hardware CAN is out of this
  stack.
- **CI never launches CARLA.** GitHub Actions covers encode → ``vcan`` →
  decode. The visual CARLA loop is a documented host demo.

**********************
Open-loop (PR 1)
**********************

Goal: a Safety Island command visibly moves a CARLA ego without Autoware
planning or Open AD Kit.

Components
==========

1. **SocketCAN backend** for ``freertos-posix``. When
   ``SAFETY_ISLAND_CAN_IFACE`` is set (for example ``vcan0``), the platform
   layer opens Linux ``PF_CAN`` and sends classic frames. When unset, the
   existing in-memory mock remains so CI and ``--can-output-test`` stay
   privilege-free.
2. **Decoder** next to the encoder, same IDs, scaling, and sequence rules.
   Round-trip tests live in ``actuation_module/test/can_output_test.cpp``.
3. **Host bridge** (``demo/can_carla_bridge/``): ``python-can`` on ``vcan0``,
   assemble a command from the three frames, map to ``VehicleControl``, apply
   via the CARLA 0.9.16 client. Same CARLA version as Open AD Kit.
4. **Inputs to the controller** from the existing rosbag (or a canned DDS
   publisher) on domain 2. Do not start planning simulator or Open AD Kit in
   this loop.

Control mapping
===============

``autoware_control_msgs/Control`` is kinematic. CARLA ``VehicleControl`` is
actuator. The bridge uses a documented, deliberately simple map:

- ``steer = clamp(steering_tire_angle / max_steer_rad, -1, 1)``
- if ``acceleration >= 0``: ``throttle = clamp(acceleration / max_accel, 0, 1)``,
  ``brake = 0``
- if ``acceleration < 0``: ``brake = clamp(-acceleration / max_brake, 0, 1)``,
  ``throttle = 0``

Constants (max steer, max accel, max brake) are bridge configuration, defaulted
to the Autoware sample-vehicle ballpark. This is not an OEM vehicle model.

Demo and validation
===================

- Host: ``ip link add vcan0 type vcan`` and bring it up.
- Build ``freertos-posix`` with ``--control-output CAN_ONLY`` or
  ``DDS_AND_CAN``, ``SAFETY_ISLAND_CAN_IFACE=vcan0``.
- Run the bridge against CARLA 0.9.16, confirm the ego steers/throttles.
- CI: encoder/decoder unit tests plus a ``vcan`` send/receive test. No GPU,
  no CARLA container.

Known limits
============

- Placeholder CAN IDs, not a production DBC.
- Open-loop CARLA motion will not match rosbag odometry.
- Mapping ignores jerk and target velocity except as carried in the frames.
- FVP and S32Z are not this demo.

Closed-loop with Open AD Kit is the next stacked PR. CAN-FD is a parallel
sketch PR on the same SocketCAN base.

**********************
Closed-loop (PR 2)
**********************

Goal: Autoware planning against CARLA, Safety Island as the controller,
actuation still over CAN into the same host bridge.

This layer depends on the open-loop SocketCAN backend and bridge. It does
not add a second command sink.

.. mermaid::

   graph LR
       subgraph carla_stack [Open AD Kit CARLA]
           Sensors[autoware_carla_interface<br/>sensors only]
           Planning[Autoware planning]
           CARLA2[CARLA ego]
       end

       Bridge2[Host CAN-CARLA bridge]
       DDS[domain bridge]
       SI2[Safety Island<br/>CAN_ONLY]

       CARLA2 --> Sensors
       Sensors --> Planning
       Planning -->|trajectory odom steer accel| DDS
       DDS --> SI2
       SI2 -->|classic CAN| Bridge2
       Bridge2 -->|VehicleControl| CARLA2

Rules
=====

- Safety Island builds with ``CAN_ONLY`` so
  ``/control/trajectory_follower/control_cmd`` is not also published over DDS.
- Autoware's onboard trajectory follower stays stubbed, same pattern as
  ``demo/launch/control.launch.xml``.
- ``autoware_carla_interface`` keeps sensor and localization publication.
  Its vehicle-command apply path must be disabled or otherwise prevented from
  fighting the CAN bridge. If a launch flag is missing upstream, document the
  overlay used here rather than forking Open AD Kit.
- Same placeholder frames, same mapping, same ``vcan0`` iface.
- Compose glue lives in this repository and starts beside the Open AD Kit
  CARLA sample. Do not vendor Open AD Kit sources.

Validation
==========

- Manual: Town01 (or the Open AD Kit default), set a goal, engage, confirm
  the ego moves under SI CAN commands and that ``candump vcan0`` shows
  ``0x100`` / ``0x101`` / ``0x102``.
- Confirm Autoware is not also applying ``control_cmd`` to CARLA (no dual
  drive).
- Still no CARLA job in this repository's GitHub Actions.

Known limits
============

- GPU host and Open AD Kit image pull required.
- Humble-only while Open AD Kit CARLA is Humble-only.
- Placeholder DBC and simple actuator mapping still apply.
- CES / FVP virtual-CAN is not this PR.
