.. SPDX-License-Identifier: Apache-2.0

############################################
VisionPilot--Safety Island control contract
############################################

**Status:** draft for `SI issue #62
<https://github.com/autowarefoundation/autoware-safety-island/issues/62>`_.
The actuator boundary, initial camera/ego matching rule, applied-brake
event and initial 500 ms gate have been agreed; the remaining wire,
freshness and scenario limits below have not. This is not yet an
implementation-ready contract. The current deployed interfaces are
documented separately in :doc:`topics`.

Purpose and boundary
====================

No real vehicle or VCU interface has been selected. This document
defines a **vehicle-independent logical motion request**, not a claim
that a particular production vehicle accepts these fields directly.
When a target vehicle is chosen, its actual DBW/CAN/Ethernet contract,
enable/override rules and actuator feedback must be checked separately.
CARLA's API must not dictate the SI/VP motion-request semantics.

The first validation rig uses CARLA 0.9.16, its matching Python API, and
**no native CARLA ``--ros2``**. The CARLA bridge transports camera and
vehicle feedback to ROS 2; the DDS domain bridge forwards only
**SI-approved** requests to a **separate CARLA actuator component**. That
component is the only writer to the vehicle actor. It realizes SI's
normal steering/speed/signed-acceleration request as CARLA actuator
controls and applies SI's explicit stop/hold decision as braking. It
does not choose a different normal driver or independently detect a
fault. The CARLA bridge does not control speed, brake on a timeout,
choose the normal driver, or decide whether a command is safe. The
CAN--CARLA demo is a separate transport, not a second writer in this
rig. On a future real vehicle, this CARLA-specific component is replaced
by a separately verified vehicle/VCU-facing realization.

There are two SI control modes and three startup configurations:

.. list-table::
   :header-rows: 1
   :widths: 23 38 39

   * - Configuration
     - Normal candidate
     - SI decision
   * - ``SI_CONTROL`` + Autoware
     - Autoware Universe planning trajectory
     - Select only Autoware input; follower proposes the command
   * - ``SI_CONTROL`` + VisionPilot
     - VP path and speed/stop intent from one camera cycle
     - Select only VP reference; the same follower proposes the command
   * - ``VP_CONTROL`` + VisionPilot
     - One VP steering, VP-selected target-speed and acceleration request
     - Check and pass the accepted request; do not run the follower or
       recompute VP's normal decision

Only SI's output gate releases normal or stop commands. Faults latch a
stop/hold response without silently changing the mode or planning source;
resuming normal output requires an explicit re-enable action. While SI is
running, missing/invalid selected input, bad normal output, and stale
vehicle feedback must cause SI's fault response. The stop path must not
depend on a responsive follower/MPC. This design does **not** guarantee a
stop if SI or the bridge itself stops producing output.

Wire and source-identity decisions
==================================

The existing Autoware ``autoware_planning_msgs/msg/Trajectory`` input
and follower algorithm remain supported without an Autoware-side adapter.
Its topic is ``/planning/scenario_planning/trajectory`` (domain 1 to 2).
The VP adapter instead publishes a ``safety_island_msgs/msg/TrajectoryCandidate``
containing a standard trajectory and the original VP session/cycle on
``/planning/visionpilot/trajectory_candidate``. The same SI binary reads
``SI_SUPERVISION_MODE`` and ``SI_TRAJECTORY_SOURCE`` once at startup and
subscribes **only** to the selected input (or the VP command in VP_CONTROL).
Neither fault nor re-enable can change it; an invalid or missing startup
combination is fatal, not a silent default. Run both publishers concurrently
and inject selected-source loss to verify isolation.

The VP command must be **one compound message**, not independently
arriving steering, acceleration and speed samples. It must carry steering
in tire radians, VP's target speed in m/s, acceleration/deceleration in
m/s², source camera time and frame identity, a boot/session identity,
cycle number, and validity. Sign conventions, optional fields, bounds,
serialized size, QoS, the exact ROS/DDS type and topic must be frozen
before implementing the VP and SI producers/consumers.

The VP path and speed/stop intent likewise come from one VP cycle. Their
original source identity must survive the adapter's conversion to an SI
trajectory; old, mismatched or reordered data must not become fresh just
because an adapter or DDS bridge republishes it. Define the VP-to-adapter
reference type and the metadata carried through the VP-specific SI ingress.
The E2E adapter rejects/reports invalid references without publishing a
fabricated stop trajectory. A genuine VP-chosen stop in a valid path/speed
cycle remains a normal candidate. SI checks the original VP session/cycle
and progress of the camera source stamp before refreshing its selected
candidate watchdog. Equal source stamps (replayed frames) do not refresh
it; regressions latch SI_STOP. The source stamp uses CARLA simulation time
and is compared only with earlier source stamps, **not** SI wall time.
The selected arrival watchdog is separate. Native Autoware trajectories
have no session/cycle; SI checks progress of their header stamps instead,
without inventing an Autoware producer identity.

SI publishes through **one supervised DDS command output** for all three
configurations. The wire contract must carry a unique SI session and
output sequence so the test rig can distinguish the normal request from
SI's stop response and correlate each output with a CARLA frame. The
legacy raw follower output must not be routed to CARLA around this gate.
The selected DDS/CAN output mode is configured per run; the CAN demo is
not enabled simultaneously with the first DDS rig.

Implementations and wire layout
-------------------------------

The VP side is **implemented** on the fork branch
``feat/vp-si-interface`` (``visionpilot_msgs``). SI's supervised output
and VP-specific candidate ingress are implemented. The clean rig replay
and cross-DDS wire check are recorded in the E2E evidence separately.

.. list-table::
   :header-rows: 1
   :widths: 34 39 27

   * - Topic / direction
     - Type
     - State
   * - ``/vehicle/driving_command`` (1)
     - ``visionpilot_msgs/msg/DrivingCommand``
     - Implemented: steering tyre angle, VP-selected target speed, signed
       acceleration, source capture stamp, VP session + cycle
   * - ``/vehicle/driving_reference`` (1)
     - ``visionpilot_msgs/msg/DrivingReference``
     - Implemented: lane polynomial (a, b, c, x_max), speed horizon
       (20 × 0.05 s), same identity fields
   * - ``/planning/scenario_planning/trajectory`` (1 → 2)
     - ``autoware_planning_msgs/msg/Trajectory``
     - Existing Autoware-only input
    * - ``/planning/visionpilot/trajectory_candidate`` (1 → 2)
      - ``safety_island_msgs/msg/TrajectoryCandidate``
      - VP-only adapter result containing a standard ``Trajectory`` and
        the original VP ``source_session`` / ``source_cycle``;
        Autoware needs no wrapper
   * - ``/control/safety_island/approved_request`` (2 → 1)
     - ``safety_island_msgs/msg/ApprovedRequest``
     - Implemented (SI IDL): the only actuator-facing output; SI-only
       authoring with SI ``session`` + strictly increasing
       ``output_sequence``, decision ``NORMAL`` / ``SI_STOP`` / ``HOLD``,
       configured mode, selected source, VP cycle identity and ``fault_id``.
        Legacy ``control_cmd`` stays on domain 2 only; the single-writer
        CARLA actuator consumes ``ApprovedRequest`` directly there (E2E #2).

The VP messages are published once per processed camera cycle and carry
the source stamp of the image that produced the decision; the legacy
Float64 command topics stay unchanged. ``target_speed_mps`` is the speed
at the end of the plan's 1 s schedule: VP's own statement of where the
current plan is heading, not a consumer re-derivation.
``DrivingReference``'s path and speed horizon always come from one cycle.

SI-facing sizes must stay within the SI runtime's 1400 B DDS message
limit. ``DrivingCommand`` is a few hundred bytes; a 13-point VP candidate
with ``map`` frame ID serializes to **1188 B**, within the adapter's 1300 B
budget; the SI output stays a ``Control``-sized sample. The SI-side IDL mirror of
``visionpilot_msgs`` was generated from the fork's ``.msg`` definitions
with ``rosidl_adapter`` and compiles in the SI build (0.11 idlc; the
``@verbatim`` comments are skipped with the usual warnings). The
``ApprovedRequest`` and ``TrajectoryCandidate`` IDLs have matching ROS-side
``safety_island_msgs`` definitions in the E2E rig. Build/replay must check
ROS-to-SI serialization interop; a generated type alone is not live proof.

Measured cadence and identity (VPS, 2026-09-25)
-----------------------------------------------

One 150 s run of the implemented VP image with the E2E camera bridge on
Town04 (CARLA 20 Hz fixed ticks, 1920 × 1280 camera), measured with a
Humble/CycloneDDS reader:

.. list-table::
   :header-rows: 1
   :widths: 34 12 12 12 12 12

   * - Topic
     - Rate
     - p50
     - p95
     - max
     - p95 age
   * - ``/vehicle/driving_command``
     - 8.82 Hz
     - 103 ms
     - 202 ms
     - 627 ms
     - 113 ms
   * - ``/vehicle/driving_reference``
     - 8.82 Hz
     - 103 ms
     - 201 ms
     - 627 ms
     - 114 ms
   * - ``/carla/hero/main_cam/image``
     - 9.55 Hz
     - 101 ms
     - 126 ms
     - 479 ms
     - 38 ms
   * - ``/localization/kinematic_state``
     - 20.0 Hz
     - 50 ms
     - 75 ms
     - 100 ms
     - 22 ms
   * - ``/vehicle/speed``
     - 20.0 Hz
     - 50 ms
     - 75 ms
     - 107 ms
     - —

Identity over 1324 command/reference pairs: every command had a
reference from the **same** cycle with the **same** source stamp; no
missing source stamp, one stable session, no cycle regression, the speed
horizon was always 20 × 0.05 s, and no invalid path or reference was
seen. The VP output rate (8.8 Hz) tracks the camera (9.6 Hz); VP
processes most, not all, camera frames.

Consequences for the first thresholds (**proposed values that also serve
as the implemented SI supervisor defaults; approval pending before
closing #62**):

* A 0.5 s source-age timeout on VP outputs would false-trip: the
  measured maximum gap is 627 ms (682 ms in the re-run below). Values:
  **1.0 s** for the selected candidate, **0.4 s** default for the 20 Hz
  ego/speed feedback (within the agreed 0.3-0.5 s band), **0.5 s** for
  the 10 Hz steering report and operation mode. These are per-source
  watchdogs, not the fault-to-brake gate.
* The 500 ms gate stays as agreed: it starts at SI fault **detection**,
  the control tick that first observes the stale/invalid source, after
  the source has already been declared stale.

The first measurement used the bridge's host-time image stamps. The E2E
bridge now stamps camera and ego samples with the **CARLA simulated
time** of the capture/frame and publishes one ego sample per world frame.
Re-measured over 90 s: every one of 820 VP command cycles had an exact
ego sample with the **same source stamp (100%)**; 806/820 source stamps
were also seen on the camera stream by the probe (loss of large image
samples in transport, not a VP fault); all identity checks above still
held. The adapter can therefore require an exact same-stamp ego sample
and reject the reference when it is missing, without inventing a stop.

Camera, ego and deadline clocks
===============================

CARLA camera frames expose an episode frame number and a simulated
capture time. CARLA world snapshots expose a frame number and simulated
world time. The bridge must retain this **source** identity on camera and
ego samples; using its publication time for both hides acquisition and
sampling skew. VP must preserve the input frame identity through
inference and planning. The first-rig adapter uses only an exact-match
ego sample, never just the latest available pose.

On two pinned 0.9.16 VPS smoke runs (Town04, 20 Hz fixed world ticks,
10 Hz RGB camera at 640 × 480 and at the rig's 1920 × 1280 resolution),
each run's 35 camera frames matched an ego world snapshot with the **same**
frame number and exactly the same CARLA source time (maximum measured
source-time skew 0.0 s). For the **first validation
rig**, require the same episode and frame number for camera and ego;
reject the VP reference if the exact ego frame is missing. Do not match
an identical frame number across a CARLA episode reset. Bounded
interpolation may be designed later if full-rig evidence shows missing
exact frames; it is **not** a permitted first-rig fallback.
This tolerance is for CARLA source samples, not permission to replace
their capture stamps with host arrival time. These short probes
demonstrate source-frame joinability, **not** ROS/VP delivery latency or
full E2E processing performance.

Simulation timestamps, ROS wall time and a host monotonic clock are
different time bases. A CARLA episode time in seconds must not be
subtracted from SI's Unix/system clock. Specify the conversion or
correlation used for source age, and the monotonic clock used for elapsed
fault-to-applied time. The POSIX CARLA rig is on one host; later hardware
needs its own verified clock-sync contract. A repeated camera frame or
VP cycle is not fresh merely because it was received again.

CARLA actuation and measured application
========================================

The CARLA-facing interface is **not frozen**. A single-vehicle VPS probe
used the pinned 0.9.16 server digest from the E2E rig, its checksum-verified
Python 3.10 wheel, Town04, synchronous 0.05 s ticks paced at 20 Hz, and a
Tesla Model 3. Python ``apply_ackermann_control`` did eventually drive
and stop the car, but it did **not** directly honor a signed deceleration
request:

* After changing from target speed 3 m/s to 0 m/s with
  ``acceleration=-3 m/s²``, the actor continued to receive throttle for
  about 0.85 s of wall time. The first observed brake greater than 0.05
  appeared after about 1.15 s, at CARLA frame 163 (request after frame
  139). This is one smoke run, not a worst-case latency bound.
* CARLA 0.9.16 ``AckermannController.cpp::SetTargetPoint`` stores
  ``FMath::Abs(UserTargetPoint.Acceleration)``: negative and positive
  values with equal magnitude give the same acceleration **limit** to its
  internal speed controller. They do not directly specify braking intent.
* On the same VPS, a direct Python ``VehicleControl`` with throttle 0 and
  brake 0.6 appeared as those same actuator fields in the **first**
  CARLA world frame following the call (about 2.3 ms in that one probe;
  not a fault-to-actuation integration bound).

Thus the original preference for the Ackermann interface is superseded
for this rig: CARLA 0.9.16 Ackermann **cannot be assumed** to realize
signed VP acceleration/deceleration or prompt SI brake intent. The
agreed architecture places vehicle-specific actuator realization in a
**separate single-writer CARLA component**, after SI's vehicle-independent
approved output, not inside SI and not in the camera/feedback bridge.
For CARLA this component can use ``VehicleControl`` as its final actor
API: normal speed/acceleration-to-pedal behavior and immediate mapping of
an explicit SI stop/hold to brake must be implemented and **validated**
before integration. The direct brake smoke does **not** validate normal
driving, a fixed brake strength or fault-to-applied timing. This adapter
must not invent a new timeout-brake policy or make a safety decision when
SI/bridge messages stop; that silence is measured separately and is not
claimed safe.

For each fault injection, record the SI fault-detection event, SI
decision and unique output sequence, actuator receipt, and the first
CARLA world frame whose actor control **applies a brake command**. The
initial candidate threshold is throttle at most 0.01 and brake greater
than 0.05 in the actor's ``get_control()`` for that frame (to exclude
negligible controller chatter). This is evidence of a simulator-applied
control request, not measured wheel brake torque, deceleration or a
physical-vehicle result. Instrument the single final CARLA writer and
frame observer to correlate that control to the SI stop sequence;
pre-existing normal braking is not proof that the fault response arrived.
A publisher callback, bridge queue, Python RPC return or accepted
zero-speed setpoint is not application evidence. A missing applied frame
fails the run. This event is different from the later moment when the car
reaches zero speed.

**Initial CARLA pass/fail gate, agreed for the first scenarios:** while
SI is running, at most **500 ms** of elapsed wall-monotonic time from
SI's fault-detection event to the first CARLA world frame with the
above applied-brake condition. The bound is defined *before* the
integration run, not selected from a passing run. It is an initial
simulation acceptance gate, not a real-vehicle safety claim. The
SI POSIX detection timestamp and the CARLA frame observer must use
correlated monotonic clocks on the same test host, not SI's existing
``system_clock``/ROS epoch stamp or CARLA simulated elapsed seconds.
Record and reject missing clock correlation. The
150 ms existing SI cycle plus one nominal 50 ms actuator loop and one
50 ms CARLA step leave approximately 250 ms for scheduling, DDS and test
instrumentation. These are budgeting assumptions to verify, not
guaranteed component response times. Also report fault onset to SI
detection separately, so a slow watchdog cannot be hidden by a fast
post-detection response.

Decision status
===============

.. list-table::
   :header-rows: 1
   :widths: 37 63

   * - Item
     - Decision / evidence required
   * - VP command and reference schema
     - **Implemented** in fork branch ``feat/vp-si-interface``
       (``visionpilot_msgs``; topics and fields above). The SI-side IDL
       mirrors are implemented and the ROS↔SI wire path is exercised on the
       E2E rig (VP command passthrough: 69 matched cycles, 0 mismatches).
   * - SI candidate and output schema
     - Output **implemented** as the ``ApprovedRequest`` IDL: one
       actuator-facing output per control cycle with session,
       ``output_sequence``, decision and fault correlation; the legacy
       ``control_cmd`` topic carries the gate-approved payload for
       transition. The distinct VP candidate ingress is also implemented:
       ``safety_island_msgs/msg/TrajectoryCandidate`` on
       ``/planning/visionpilot/trajectory_candidate`` carries a standard
       trajectory plus the original VP session/cycle (13 points =
       1188 B CDR, validated end-to-end on the rig).
   * - Source-age watchdogs
     - **Implemented** in the SI supervisor with the proposed defaults
       (candidate 1.0 s, ego 0.4 s, steering/opmode 0.5 s), per-source by
       design. Final-ingress rig runs report SI-stated selected-source ages
       of 1.01–1.10 s at detection; the injection→latch figure is reported
       separately because a multi-node source container adds its own
       shutdown time.
   * - Restart and re-enable rules
     - **Implemented** and rig-validated: session change handling,
       cycle-regression rejection, SI_STOP latch with ``fault_id`` and an
       explicit ``/control/safety_island/reenable`` (``std_msgs/Bool``)
       that requires all sources fresh again. Elapsed fault-to-brake timing
       uses SI system-clock stamps inside ``ApprovedRequest``; the E2E rig
       measured **6–14 ms** from SI detection to the first applied CARLA
       brake frame on the final binary (see
       ``openadkit-e2e/docs/e2e2-stop-gate.md``).
   * - CARLA actuator implementation
     - **Implemented** in the E2E rig (``deploy/nodes/carla_actuator.py``):
       the sole CARLA control writer, consuming ``ApprovedRequest`` directly
       on domain 2 and realizing SI's NORMAL/SI_STOP/HOLD decisions without
       any local watchdog or mode logic. Rig-validated with the three
       final-ingress gates and concurrent-publisher isolation runs.
   * - Per-scenario pass/fail gates
     - Initially 500 ms from SI fault detection to first CARLA braking
       frame while SI is running; missing or late application fails.
       If scenarios need different limits, agree on them before
       integration, not after examining a failing run

The 500 ms limit is **not** copied from the former 0.5 s all-inputs
staleness gate: that gate was refreshed by *any* input and could not
reliably detect loss of the selected source, which is why the supervisor
replaced it with the per-source checks above. The 500 ms gate begins only
after SI has actually detected a fault. End-to-end fault-onset-to-brake
latency and physical stopping distance need separate reporting and
scenario-specific limits.
