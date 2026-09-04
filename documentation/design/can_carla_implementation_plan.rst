..
 # Copyright (c) 2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

########################################
CAN and CARLA implementation plan
########################################

Corrected dual-runtime plan for classic CAN from the Safety Island into a
CARLA ego. This page is the implementation source of truth for the stacked
work in https://github.com/autowarefoundation/autoware-safety-island/issues/43.
It supersedes the original open-loop sketch in :doc:`can_carla_integration`
where the two disagree.

The placeholder frame contract in :doc:`can_output` is unchanged: IDs
``0x100`` / ``0x101`` / ``0x102``, classic 8-byte DLC, existing scaling.

.. mermaid::

   graph TB
       Encoder[common encoder<br/>0x100 0x101 0x102]
       Batch[complete 3-frame batch]
       Encoder --> Batch

       Batch --> POSIX[freertos-posix<br/>nonblocking PF_CAN]
       Batch --> FVP[zephyr-fvp TAP<br/>one UDP batch datagram]
       Batch --> S32Z[zephyr-s32z<br/>native zephyr,canbus]

       POSIX --> VCAN[SocketCAN vcan0]
       FVP --> TAP[LAN91C111 tap0]
       TAP --> GW[host UDP-to-CAN gateway]
       GW --> VCAN
       S32Z --> HW[physical CAN bus]

       VCAN --> Decoder[shared assembler<br/>commit sequence watchdog]
       Decoder --> CARLA[VehicleAckermannControl]

**********************
Why this plan
**********************

The original PR 1 design assumed ``freertos-posix`` plus ``vcan`` only, with
the in-memory mock as the unset-interface fallback, and treated Zephyr FVP as
unreachable. Cross-check against the current code and issue #43 found that
that sketch cannot ship as written:

- ``platform_can.h`` dispatches every FreeRTOS target through one header.
  Linux ``PF_CAN`` must never enter the S32Z2/X5H ``arm-none-eabi`` builds.
- The mock always reports successful ``can_init()``. If it remains the
  runtime fallback, ``CAN_ONLY`` cannot fail-fast and commands disappear
  into memory.
- ``freertos-posix`` currently defaults to ``DDS_AND_CAN`` only to exercise
  that mock. Once the mock is test-only, the unconfigured runtime default
  must be ``DDS_ONLY``.
- Sequence lives only in ``0x102``. One UDP datagram per CAN frame lets
  loss or reordering pair stale ``0x100`` / ``0x101`` data with a later
  commit.
- Existing CAN CI records mock frames. It does not create ``vcan0``, send
  SocketCAN, or run a decoder. Creating ``vcan0`` needs ``CAP_NET_ADMIN``;
  FVP TAP also needs ``/dev/net/tun``. That path is not privilege-free.
- Autoware steering is CCW/left-positive. CARLA Ackermann steering is
  right-positive. Velocity and steering rate must not be discarded.
- ``0x102`` is the only cycle marker. The decoder must require fresh
  ``0x100`` and ``0x101``, contiguous modulo-65536 sequence, and a
  receive-timeout safe stop.

Zephyr FVP still has no host-reachable CAN IP. The existing TAP Ethernet
path (``192.168.10.2`` on FVP, ``192.168.10.1`` on the host) is the only
virtual way to reach the host. Native ``zephyr-s32z`` already speaks real
CAN and stays on that path.

**********************
Shared decisions
**********************

- **Actuation is CAN.** The host talks to CARLA through the Python API, not
  by republishing ROS control commands.
- **DDS inputs stay DDS.** Trajectory, odometry, steering, acceleration, and
  operation mode are unchanged.
- **One sink.** Open-loop and closed-loop share the same ``vcan0`` decoder
  and CARLA process. Closed-loop does not add a second command path through
  ``autoware_carla_interface``.
- **PR 1 (#49) local runtime is** ``freertos-posix`` **plus** ``vcan``.
  That matches issue #43 / PR #49 acceptance.
- **Zephyr FVP TAP UDP is a follow-on PR**, not #49. It reuses the same
  ``vcan0`` decoder after #43's "FVP host-bridged CAN" out-of-scope line
  is updated. Native FVP CAN stays loopback-only.
- **Native Zephyr S32Z is unchanged.** It keeps ``zephyr,canbus`` / ``&can0``.
  Hardware CAN into the host is out of this stack.
- **Embedded FreeRTOS CAN is out of this stack.** ``freertos-s32z2`` and
  ``freertos-x5h`` stay ``DDS_ONLY``. Selecting ``CAN_ONLY`` or
  ``DDS_AND_CAN`` on those targets must fail at configure time.
- **CI never launches CARLA.** GitHub Actions covers encode, transport,
  decode, and watchdog. The visual CARLA loop is a documented host demo.
- **Mock is test-only.** It is compiled into ``--can-output-test``, not
  selected by an unset environment variable.

**********************
Common transport contract
**********************

Keep ``encode_control_command()`` and the three-frame layout. Change how
frames leave the controller.

``ControlCommandCanOutput::send()`` today encodes, then calls
``platform::can_send()`` once per frame while holding a mutex, and
increments ``sequence_`` before encoding can fail
(``control_command_can_output.hpp``). Replace that with a batch send:

1. Encode the complete command.
2. Call ``platform::can_send_batch(frames, count)``.
3. Increment ``sequence_`` only after the batch succeeds.

``can_send_batch()`` is the platform boundary. Semantics:

- Never send ``0x102`` unless ``0x100`` and ``0x101`` were handed to the
  transport. A later write failure may still leave the data frames
  visible on ``vcan0``; only the UDP datagram is all-or-nothing.
- Sends are nonblocking, or bounded to a short deadline. Do not stall the
  controller timer callback on SocketCAN or UDP backpressure.
- A failed batch leaves ``sequence_`` unchanged so the next successful
  command stays contiguous. Unit-test this: encode or mid-batch send
  fails, then the next successful command reuses the same sequence.

Keep ``platform::can_send(frame)`` only where a backend truly is
frame-at-a-time (native Zephyr CAN, the test mock). The POSIX SocketCAN
and FVP UDP backends implement the batch entry point directly.

**********************
Decoder and CARLA mapping
**********************

The decoder lives next to the encoder and is the only assembler. Both
runtimes inject classic frames onto ``vcan0``; the bridge never sees UDP.

Assembly
========

- ``0x102`` is the commit marker. Do not apply a command until a commit
  arrives.
- Require a fresh ``0x100`` and a fresh ``0x101`` since the previous
  accepted commit. Duplicate IDs in the same cycle replace the pending
  frame; they do not count as a second cycle.
- Sequence is the little-endian ``uint16`` at bytes 2-3 of ``0x102``.
  Accept contiguous modulo-65536 values, including wrap from 65535 to 0.
- On first command after start or after a watchdog safe-stop, accept the
  first valid complete cycle as the baseline. Do not require sequence 0.
- After a rejected commit, drop pending ``0x100`` / ``0x101``. The next
  complete cycle is accepted immediately if its sequence is the next
  expected value. A further gap keeps rejecting until the watchdog
  fires, then the first complete valid cycle re-baselines.
- Unknown CAN IDs are ignored and do not affect the cycle.
- IDs ``0x100`` / ``0x101`` / ``0x102`` with DLC other than 8, extended
  ID, or non-classic flags are discarded: that ID is not fresh. They do
  not themselves trigger safe-stop.

Watchdog
========

- Use host monotonic receive time, not the payload timestamp.
  ``timestamp_ms_modulo()`` is producer metadata. FreeRTOS POSIX uses host
  epoch time; Zephyr FVP defaults to time since boot. Those clocks are not
  comparable.
- If no accepted commit arrives within the configured timeout, apply a
  safe stop and hold it until a new valid cycle is accepted.
- POSIX default timeout is 0.5 s, matching controller
  ``timeout_thr_sec`` and covering a skipped 150 ms tick (~300 ms to the
  next frame). Do not use 200 ms: that is tighter than one missed period.
- FVP is not real-time. The FVP demo and TAP integration job use a much
  larger configured timeout; they do not share the 0.5 s POSIX default.

CARLA boundary
==============

Autoware ``Lateral.idl`` is CCW/left-positive. CARLA Ackermann steering is
right-positive. Use ``carla.VehicleAckermannControl``, not
``VehicleControl``.

CARLA 0.9.16 ``AckermannController`` does not treat zeros as "hold":

- ``Abs(steer_speed) < 0.001`` snaps steer to the target immediately.
- ``Abs(acceleration) < 0.0001`` clips the speed PID with the vehicle's
  max accel/decel, not "no acceleration".
- The controller takes ``Abs(steer_speed)``, so negating Autoware's rate
  does not reverse steering direction. Sign conversion is only
  ``steer = -steering_tire_angle``.

Apply:

- ``steer = -steering_tire_angle``
- ``steer_speed = abs(steering_tire_rotation_rate)`` when the rate flag
  is set. If unset, use a configured slew (not 0) so CARLA does not snap.
- ``speed = velocity``
- ``acceleration`` when the acceleration flag is set. If unset, use a
  configured magnitude (not 0).
- ``jerk = 0``: the current frames do not carry a scaled jerk field.

Safe stop: ``speed = 0``, a configured braking acceleration (not 0),
hold last steer, and a configured slew. Do not command
``steer = 0, steer_speed = 0`` (that snaps wheels to center).

Constants remain bridge configuration, defaulted to the Autoware
sample-vehicle ballpark. This is not an OEM vehicle model.

**********************
FreeRTOS POSIX
**********************

Backend isolation
=================

``platform_network.h`` already special-cases ``PLATFORM_FREERTOS_S32Z2``
and ``PLATFORM_FREERTOS_X5H`` before the generic FreeRTOS header.
``platform_can.h`` must do the same:

- ``actuation_module/freertos/CMakeLists.txt`` defines
  ``PLATFORM_FREERTOS_POSIX=1`` in addition to ``PLATFORM_FREERTOS``.
- ``platform_can.h`` includes a POSIX-only SocketCAN header when
  ``PLATFORM_FREERTOS_POSIX`` is set, a test mock when the CAN-output-test
  target is built, and keeps embedded targets off Linux ``PF_CAN``.
- ``freertos-s32z2`` and ``freertos-x5h`` CMake must ``FATAL_ERROR`` if
  ``CONFIG_CONTROL_CMD_OUTPUT_MODE`` is not ``DDS_ONLY``.

SocketCAN backend
=================

- Runtime CAN output requires ``SAFETY_ISLAND_CAN_IFACE`` (for example
  ``vcan0``). Missing, empty, or unopenable interface: ``can_init()``
  returns false. ``CAN_ONLY`` then exits in ``controller_node.cpp``;
  ``DDS_AND_CAN`` logs and continues on DDS.
- Open ``PF_CAN`` / ``SOCK_RAW`` / ``CAN_RAW``, bind to the named
  interface, set ``O_NONBLOCK``.
- ``can_send_batch()`` writes ``0x100``, ``0x101``, then ``0x102``. If a
  write fails before the commit, do not send ``0x102``.
- Default ``CONFIG_CONTROL_CMD_OUTPUT_MODE`` for ``freertos-posix``
  becomes ``DDS_ONLY``. Demo and integration builds pass
  ``--control-output CAN_ONLY`` or ``DDS_AND_CAN``.

Mock backend
============

Compile the in-memory recorder only into ``BUILD_TEST=4``
(``--can-output-test``). Keep ``reset_recorded_can_frames()`` and friends
behind that target so the existing encoder and recording assertions stay
privilege-free.

**********************
Zephyr FVP UDP tunnel
**********************

Native FVP CAN remains ``zephyr,can-loopback`` and is still the
``--can-output-test`` path. It cannot reach the host. The CARLA path is a
separate transport over the existing TAP network.

Selection
=========

Do not key both Zephyr backends on ``PLATFORM_ZEPHYR`` alone.
``zephyr_can.h`` requires ``chosen { zephyr,canbus }`` and must stay the
S32Z / loopback-test backend.

Today ``CONTROL_CMD_OUTPUT_CAN_ONLY`` and
``CONTROL_CMD_OUTPUT_DDS_AND_CAN`` both ``select CAN``.
``zephyr_can.h`` then ``#error``s unless ``chosen { zephyr,canbus }``
exists. FVP TAP has no canbus node unless the loopback overlay is
applied, so a transport choice that "does not select CAN" is not enough:
the output-mode ``select`` still pulls in the CAN stack.

Change Kconfig:

- Remove ``select CAN`` from the output-mode choice.
- ``CONFIG_CONTROL_CMD_CAN_TRANSPORT_NATIVE`` (default) ``select CAN``
  and is the current ``zephyr,canbus`` path.
- ``CONFIG_CONTROL_CMD_CAN_TRANSPORT_UDP_TUNNEL`` selects ``NET_UDP`` /
  ``NET_IPV4``, does not ``select CAN``, and does not require
  ``zephyr,canbus``.
- ``platform_can.h`` includes ``zephyr_can_udp_tunnel.h`` vs
  ``zephyr_can.h`` from the transport choice so the native header is not
  compiled on the TAP path.

``build.sh --platform zephyr-fvp --network tap --control-output CAN_ONLY``
(or ``DDS_AND_CAN``) applies a new conf fragment
``fvp_baser_aemv8r_smp_can_udp_tunnel.conf`` on top of
``fvp_baser_aemv8r_smp_tap_network.conf``. Reject the UDP tunnel without
``--network tap``. Extend ``--control-output`` to ``zephyr-fvp``; keep it
invalid for ``zephyr-s32z`` in this stack.

Wire format
===========

One datagram is one command. Do not serialize ``CanFrame`` as a C++
object: native endianness, ``bool``, and padding are not a protocol.

Fixed little-endian layout, exact length 48 bytes:

.. list-table::
   :header-rows: 1
   :widths: 15 15 70

   * - Offset
     - Size
     - Field
    * - 0
     - 2
     - magic bytes ``[0]=0x43, [1]=0x54`` (ASCII ``CT``). Do not encode
       this as little-endian ``uint16`` ``0x4354`` (that is ``TC`` on
       the wire).
   * - 2
     - 1
     - version ``1``
   * - 3
     - 1
     - flags (bit 0 = classic batch; other bits reserved 0)
   * - 4
     - 2
     - batch sequence (same value encoded in ``0x102``)
   * - 6
     - 2
     - reserved 0
   * - 8
     - 13
     - frame 0: ``uint32`` id, ``uint8`` dlc, 8 data bytes
   * - 21
     - 13
     - frame 1: same
   * - 34
     - 13
     - frame 2: same
   * - 47
     - 1
     - reserved 0

Frame order in the datagram is ``0x100``, ``0x101``, ``0x102``. The
gateway rejects any other length, magic, version, flags, DLC, ID, or
frame order, and does not inject a partial cycle.

Sender
======

- New header ``zephyr_can_udp_tunnel.h``, selected by the transport
  choice.
- ``can_init()`` creates a UDP ``net_context`` (or BSD socket) to
  ``CONFIG_CONTROL_CMD_CAN_TUNNEL_PEER`` default ``192.168.10.1`` and
  ``CONFIG_CONTROL_CMD_CAN_TUNNEL_PORT`` default ``5555``.
- ``can_send_batch()`` packs the 48-byte datagram and sends once,
  nonblocking. Do not retry inside the controller callback.
- ``main.cpp`` currently ignores ``configure_network()`` failures. For
  the UDP tunnel, a failed TAP/IP bring-up must exit before
  ``can_init()``. The TAP conf already sets
  ``CONFIG_NET_IFACE1_ADDR="192.168.10.2"``.

Host gateway
============

``demo/can_tunnel_bridge/`` binds UDP ``192.168.10.1:5555``, validates a
complete datagram, and injects the three classic frames onto ``vcan0`` in
order. If SocketCAN injection fails before ``0x102``, do not send the
commit. This process is not the CARLA bridge; it only creates the same
SocketCAN stream that ``freertos-posix`` writes natively.

**********************
Host CARLA bridge
**********************

``demo/can_carla_bridge/`` stays on ``vcan0`` via ``python-can``. It does
not know which runtime produced the frames.

- Receive classic frames.
- Run the assembler, sequence check, and watchdog above.
- Map to ``VehicleAckermannControl`` and apply through the CARLA 0.9.16
  client, same version as Open AD Kit.
- Inputs to the Safety Island still come from the existing rosbag or a
  canned DDS publisher on domain 2. Open-loop does not start planning
  simulator or Open AD Kit.

**********************
Work items
**********************

Land in this order so each step is testable without CARLA.

PR 1 (#49) — ``freertos-posix`` + ``vcan``
==========================================

1. **Common batch API and sequence fix**
   ``can_send_batch()``, increment sequence only on success, keep
   per-frame ``can_send()`` as a backend helper. Test: failed encode or
   mid-batch send does not advance ``sequence_``.

2. **POSIX backend isolation**
   ``PLATFORM_FREERTOS_POSIX``, test-only mock, SocketCAN source that
   cannot compile into S32Z2/X5H, ``DDS_ONLY`` default, CMake
   ``FATAL_ERROR`` on embedded CAN modes.

3. **Shared decoder**
   C++ (preferred, next to the encoder) used by tests and optionally by
   the Python bridge via a thin wrapper, or a Python port with golden
   vectors generated from the C++ encoder. Cover startup, duplicates,
   missing/reordered frames, bad DLC, wrap ``65535 -> 0``, ignored
   unknown IDs, contiguous accept after a rejected cycle, timeout
   safe-stop, and re-baseline after timeout.

4. **FreeRTOS ``vcan`` integration**
   Privileged test: create ``vcan0``, set ``SAFETY_ISLAND_CAN_IFACE``,
   send a real command, decode on the host. No CARLA.

5. **CARLA bridge (POSIX demo)**
   Ackermann mapping, 0.5 s watchdog, documented host demo for
   ``freertos-posix``.

6. **Docs**
   Keep this page, :doc:`can_carla_integration`, and :doc:`can_output`
   aligned. Closed-loop (PR 2) and CAN-FD (PR 3) stay stacked on the
   same ``vcan0`` decoder.

PR 2 (#50) — Open AD Kit closed-loop
====================================

Autoware planning against CARLA, Safety Island as the controller, actuation
still over classic CAN into the same ``vcan0`` bridge as PR 1. Autoware plus
CARLA compose lives in Open AD Kit
(``deployments/safety-island-carla-simulation/``). This repository keeps the
SI binary, domain-bridge, ``vcan0``, and ``demo/can_carla_bridge``.

7. **Sensors-only CARLA interface**
   ``autoware_carla_interface`` has no upstream sensor-only flag.
   ``SensorLoop`` calls ``ego_actor.apply_control()`` every tick. Overlay
   ``demo/carla-closed-loop/overlay/patch_sensors_only.py`` skips that call
   so the CAN bridge is the sole CARLA driver. Remap
   ``input_control_cmd`` / ``output_actuation_cmd`` off the live topics.

8. **Stub Autoware follower**
   Mount existing ``demo/launch/control.launch.xml`` into the Open AD Kit
   ``control`` service, same pattern as the planning-simulator demo.

9. **SI ``CAN_ONLY`` and domain-bridge**
   Build ``freertos-posix --control-output CAN_ONLY``. Bridge the five
   DDS inputs domain 1 → 2. Do not rely on ``control_cmd`` 2 → 1.
   Closed-loop CycloneDDS pins both domains to ``lo`` (Open AD Kit default).

10. **Host CAN-CARLA bridge**
    Same ``demo/can_carla_bridge/`` as PR 1. Default ``--role ego_vehicle``
    matches Open AD Kit. ``VehicleAckermannControl`` mapping unchanged.

11. **Pins and topic contract**
    First bring-up uses current Open AD Kit tags (CARLA is already
    digest-pinned). After a working host run, record the Open AD Kit git
    SHA and image digests in ``demo/carla-closed-loop/pins.env``. Privilege-free
    tests check the five-input topic matrix against ``bridge-config.yaml``.

12. **Docs**
    Closed-loop launch is two-repo. CI never starts CARLA or Open AD Kit.

Follow-on PR — Zephyr FVP TAP tunnel
====================================

Update issue #43 / PR #49 out-of-scope before landing this PR.

13. **Zephyr UDP transport and gateway**
   Kconfig as above (output modes do not ``select CAN``), TAP conf
   fragment, ``build.sh`` flags, 48-byte pack/unpack unit tests, host
   gateway.

14. **Zephyr FVP TAP integration**
   Run FVP with hostbridge TAP, gateway, ``vcan0``, same decoder, FVP
   watchdog timeout. No CARLA.

15. **CARLA bridge (FVP demo)**
   Same bridge as PR 1, documented TAP + gateway host demo.

**********************
Tests and CI
**********************

Privilege-free (existing job, no extra capabilities)
====================================================

- Encoder payload, mode helpers, non-finite rejection.
- Mock recorder on ``freertos-posix --can-output-test``.
- Failed encode or mid-batch send does not advance ``sequence_``.
- Zephyr FVP ``zephyr,can-loopback`` on ``--can-output-test``.
- Decoder state-machine unit tests, including wrap, ignored unknown
  IDs, contiguous accept after reject, and timeout.
- Follow-on PR: UDP datagram pack/unpack and rejection of malformed
  lengths.

Privileged integration (explicit capabilities)
==============================================

Do not fold this into the current unprivileged container job. Either add
``CAP_NET_ADMIN`` (and ``/dev/net/tun`` for FVP TAP) or run on a host
runner that already has them. Document the skip path if the kernel has
no ``vcan`` / ``tun``.

- PR 1: FreeRTOS POSIX ``ip link add vcan0 type vcan``,
  ``SAFETY_ISLAND_CAN_IFACE=vcan0``, encode → SocketCAN → decode.
  Watchdog: stop the producer and assert safe-stop (0.5 s).
- Follow-on PR: Zephyr FVP TAP ``tap0`` at ``192.168.10.1/24``, FVP at
  ``192.168.10.2``, gateway → ``vcan0`` → same decoder, with the FVP
  timeout.

Never launch CARLA, a GPU job, or an Open AD Kit compose in GitHub
Actions. PR 2 adds privilege-free contract tests for the sensors-only
overlay, ego ``role_name``, and the five-input topic matrix.

**********************
Known limits
**********************

- Placeholder CAN IDs, not a production DBC.
- Open-loop CARLA motion will not match rosbag odometry.
- FVP CARLA path is CAN-over-UDP, not classic CAN end-to-end. No
  arbitration, error frames, or bit timing. Message semantics (ID, DLC,
  data) are preserved.
- Jerk is flagged in ``0x102`` but not carried as a scaled field.
- ``zephyr-s32z`` hardware CAN to a host USB-CAN adapter is possible and
  out of this stack.
- ``freertos-s32z2`` / ``freertos-x5h`` CAN is out of this stack.
