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
- **PR 1 local runtime is** ``freertos-posix`` **plus** ``vcan``.
- **PR 3 local runtime is** ``zephyr-fvp --network tap`` plus a UDP
  batch tunnel onto the same ``vcan0`` decoder. Native FVP CAN stays
  loopback-only. S32Z hardware CAN is out of this stack.
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
- FVP TAP UDP is PR 3, not this demo.
- S32Z hardware CAN is not this demo.

Closed-loop with Open AD Kit is PR 2. Zephyr FVP is PR 3. CAN-FD is PR 4.

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
- CES / FVP virtual-CAN is PR 3, not this PR.

**********************
Zephyr FVP (PR 3)
**********************

Goal: the same open-loop and closed-loop CARLA demos as PR 1 and PR 2,
with Safety Island on ``zephyr-fvp --network tap`` instead of
``freertos-posix``. Frames still terminate on ``vcan0``. The host
decoder, watchdog, and CARLA bridge do not change.

Native FVP CAN stays ``zephyr,can-loopback`` (unit tests only). This PR
is a UDP batch tunnel over the existing TAP Ethernet path.

.. mermaid::

   graph LR
       FVP[zephyr-fvp TAP]
       TAP[LAN91C111 tap0]
       GW[UDP-to-CAN gateway]
       VCAN[SocketCAN vcan0]
       Bridge[Host CAN-CARLA bridge]
       CARLA[CARLA ego]

       FVP -->|48-byte UDP batch| TAP
       TAP --> GW
       GW -->|0x100 0x101 0x102| VCAN
       VCAN --> Bridge
       Bridge -->|VehicleAckermannControl| CARLA

Open-loop
=========

Same as PR 1, except the producer:

- Build ``zephyr-fvp`` with ``--network tap --control-output CAN_ONLY``
  (or ``DDS_AND_CAN``).
- FVP ``192.168.10.2`` sends one UDP datagram per command to
  ``192.168.10.1:5555``.
- ``demo/can_tunnel_bridge/`` validates the datagram and injects classic
  frames onto ``vcan0``.
- Rosbag or canned DDS inputs on domain 2. No Open AD Kit.

Closed-loop
===========

Same Open AD Kit rules as PR 2 (``CAN_ONLY``, sensors-only
``autoware_carla_interface``, no dual-drive). Safety Island is
``zephyr-fvp`` TAP plus the gateway, not ``freertos-posix``.

Kconfig
=======

``CONTROL_CMD_OUTPUT_CAN_ONLY`` / ``DDS_AND_CAN`` must not ``select CAN``.
Only ``CONFIG_CONTROL_CMD_CAN_TRANSPORT_NATIVE`` selects ``CAN``.
``CONFIG_CONTROL_CMD_CAN_TRANSPORT_UDP_TUNNEL`` selects ``NET_UDP`` /
``NET_IPV4`` and does not require ``zephyr,canbus``.
``platform_can.h`` includes ``zephyr_can_udp_tunnel.h`` on the TAP path
so ``zephyr_can.h`` is not compiled.

Watchdog
========

POSIX stays at 0.5 s. FVP is not real-time; the FVP demo and TAP
integration job use a much larger configured timeout.

Validation
==========

- Privilege-free: UDP pack/unpack unit tests; native loopback
  ``--can-output-test`` unchanged.
- Privileged: FVP TAP + gateway → ``vcan0`` → same decoder as PR 1.
  No CARLA in CI.
- Manual: PR 1 and PR 2 host demos with ``zephyr-fvp`` as the producer.

Known limits
============

- CAN-over-UDP, not classic CAN end-to-end.
- S32Z hardware CAN remains out of this stack.
- CAN-FD is PR 4.
