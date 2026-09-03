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
``carla.VehicleAckermannControl``.

This page is the design for the stacked work tracked in
https://github.com/autowarefoundation/autoware-safety-island/issues/43.
Implementation details, backend isolation, the Zephyr FVP UDP tunnel, and
the decoder/watchdog rules live in :doc:`can_carla_implementation_plan`.

.. mermaid::

   graph LR
       POSIX[freertos-posix]
       VCAN[SocketCAN vcan0]
       Bridge[Host CAN-CARLA bridge]
       CARLA[CARLA ego]

       POSIX -->|classic CAN 0x100 0x101 0x102| VCAN
       VCAN --> Bridge
       Bridge -->|VehicleAckermannControl| CARLA

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
- **PR 1 local runtime is** ``freertos-posix`` **plus** ``vcan``. Zephyr
  FVP TAP UDP is a follow-on PR on the same ``vcan0`` decoder; native FVP
  CAN stays loopback-only. S32Z hardware CAN is out of this stack.
- **CI never launches CARLA.** GitHub Actions covers encode → transport →
  decode. The visual CARLA loop is a documented host demo.
- **Mock is test-only.** Runtime CAN output does not fall back to the
  in-memory recorder when no interface is configured.

**********************
Open-loop (PR 1)
**********************

Goal: a Safety Island command visibly moves a CARLA ego without Autoware
planning or Open AD Kit.

Components
==========

1. **SocketCAN backend** for ``freertos-posix``. Isolated with
   ``PLATFORM_FREERTOS_POSIX`` so Linux ``PF_CAN`` never enters S32Z2/X5H
   builds. ``SAFETY_ISLAND_CAN_IFACE`` (for example ``vcan0``) is required
   at runtime; missing or unopenable interface fails ``can_init()``. The
   in-memory mock is compiled only into ``--can-output-test``.
2. **Decoder** next to the encoder. ``0x102`` is the commit marker: require
   fresh ``0x100`` and ``0x101``, contiguous modulo-65536 sequence, and a
   0.5 s monotonic receive-timeout safe stop. Tests live in
   ``actuation_module/test/can_output_test.cpp`` plus a privileged ``vcan``
   integration phase.
3. **Host bridge** (``demo/can_carla_bridge/``): ``python-can`` on ``vcan0``,
   assemble a command from the three frames, map to
   ``VehicleAckermannControl``, apply via the CARLA 0.9.16 client. Same
   CARLA version as Open AD Kit.
4. **Inputs to the controller** from the existing rosbag (or a canned DDS
   publisher) on domain 2. Do not start planning simulator or Open AD Kit in
   this loop.

Follow-on (not #49): Zephyr FVP UDP tunnel over TAP, host gateway onto
``vcan0``, same decoder. Native ``zephyr,can-loopback`` stays the FVP
unit-test path. Requires updating issue #43 out-of-scope.

Control mapping
===============

``autoware_control_msgs/Control`` is kinematic and left-positive. CARLA
Ackermann control is right-positive. The bridge uses
``carla.VehicleAckermannControl``. Sign conversion is
``steer = -steering_tire_angle`` only: CARLA takes ``Abs(steer_speed)``.
Zero ``steer_speed`` snaps steer; zero ``acceleration`` means vehicle max,
not "hold". Safe stop is ``speed = 0`` with configured brake and last
steer, not all zeros. Details are in :doc:`can_carla_implementation_plan`.

Demo and validation
===================

- Host: ``ip link add vcan0 type vcan`` and bring it up.
- Build ``freertos-posix`` with ``--control-output CAN_ONLY`` or
  ``DDS_AND_CAN``, ``SAFETY_ISLAND_CAN_IFACE=vcan0``.
- Run the bridge against CARLA 0.9.16, confirm the ego steers/throttles.
- CI: privilege-free encoder/decoder/mock tests, plus a capability-gated
  ``vcan`` integration job. No GPU, no CARLA container.

Known limits
============

- Placeholder CAN IDs, not a production DBC.
- Open-loop CARLA motion will not match rosbag odometry.
- Mapping cannot recover jerk from the current frames.
- FVP TAP UDP is a follow-on PR, not this demo.
- S32Z hardware CAN is not this demo.

Closed-loop with Open AD Kit is the next stacked PR. CAN-FD is a parallel
sketch PR on the same SocketCAN base.
