..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##############
DDS topics
##############

Ground-truth contract between Autoware and the safety island. All values
are read directly from
``actuation_module/src/autoware/autoware_trajectory_follower_node/src/controller_node.cpp``
and ``demo/bridge/bridge-config.yaml``.

Both sides of the bridge use DDS type names derived from the ROS 2
message packages listed below. The safety island is on DDS domain 2;
Autoware is on DDS domain 1.

**********************
Subscriptions (inputs)
**********************

The controller subscribes to the four feedback topics in every configuration,
plus the startup-selected candidate and the re-enable topic
(``SI_SUPERVISION_MODE`` / ``SI_TRAJECTORY_SOURCE``; see
:doc:`vp_si_control_contract`). The classic demo bridge forwards the domain-1
inputs below; the E2E rig additionally bridges the VisionPilot topics with
``openadkit-e2e/deploy/config/bridge-config.yaml``.

.. list-table::
   :widths: 35 30 21 14
   :header-rows: 1

   * - Topic
     - Message type
     - Source / bridging
     - Active when
   * - ``/vehicle/status/steering_status``
     - ``autoware_vehicle_msgs/msg/SteeringReport``
     - domain 1 → 2 (demo bridge)
     - always
   * - ``/localization/kinematic_state``
     - ``nav_msgs/msg/Odometry``
     - domain 1 → 2 (demo bridge)
     - always
   * - ``/localization/acceleration``
     - ``geometry_msgs/msg/AccelWithCovarianceStamped``
     - domain 1 → 2 (demo bridge)
     - always
   * - ``/system/operation_mode/state``
     - ``autoware_adapi_v1_msgs/msg/OperationModeState``
     - domain 1 → 2 (demo bridge)
     - always
   * - ``/control/trajectory``
     - ``autoware_planning_msgs/msg/Trajectory``
     - domain 1 → 2 (demo bridge)
     - ``SI_CONTROL`` + ``SI_TRAJECTORY_SOURCE=autoware`` (build default)
   * - ``/vehicle/driving_reference``
     - ``visionpilot_msgs/msg/DrivingReference``
     - domain 1 → 2 (E2E rig; not in the demo bridge)
     - ``SI_CONTROL`` + ``SI_TRAJECTORY_SOURCE=vp``
   * - ``/vehicle/driving_command``
     - ``visionpilot_msgs/msg/DrivingCommand``
     - domain 1 → 2 (E2E rig; not in the demo bridge)
     - ``VP_CONTROL`` (``SI_SUPERVISION_MODE=vp``)
   * - ``/control/safety_island/reenable``
     - ``std_msgs/msg/Bool``
     - domain 2, published by the operator/rig
     - always

Every bridged topic keeps its name on both domains; ``bridge-config.yaml``
has no ``remap`` entries.

The bridge, and every other writer on domain 2, must load
``demo/bridge/cyclonedds-domain2.xml`` (released as ``cyclonedds-domain2.xml``),
which caps domain-2 datagrams at 1400 B. The Safety Island receives at most
2048 bytes per datagram, and a ``Trajectory`` sent unfragmented is truncated and
dropped, which ends in ``SI_STOP`` on the trajectory timeout. See :doc:`../user_guide/open_ad_kit`.

``/control/trajectory`` is the trajectory Autoware's own trajectory follower
reads: Autoware's planning output (``/planning/trajectory``) after the
trajectory gate in ``autoware_control_launch``. The Safety Island replaces the
follower, so it takes the same input. Run Autoware with
``trajectory_follower_mode: none`` in its control module preset so the follower
does not run alongside it (:doc:`../user_guide/open_ad_kit`).

.. note::

   Autoware older than the trajectory gate has no ``/control/trajectory``; it
   publishes the follower input on ``/planning/scenario_planning/trajectory``.
   For such a release, map that topic to ``/control/trajectory`` with a remap
   in your bridge configuration instead of the ``control/trajectory`` entry:

   .. code-block:: yaml

     planning/scenario_planning/trajectory:
       type: autoware_planning_msgs/msg/Trajectory
       from_domain: 1
       to_domain: 2
       remap: control/trajectory

   That configuration is a local adaptation, not the released contract.

``/control/safety_island/reenable`` is the explicit re-enable of a latched
``SI_STOP``: the SI resumes NORMAL only when every source is fresh within
``reenable_window_s`` (default 5 s) of a ``data: true`` press. Nothing in the
classic demo publishes it, so a latched demo SI stays stopped until someone
does, for example::

  ros2 topic pub --once /control/safety_island/reenable std_msgs/msg/Bool "{data: true}"

``SI_OPERATION_MODE_DURABILITY`` (``transient_local`` | ``volatile``, build
default ``transient_local``) selects the durability the SI requests on
``/system/operation_mode/state``. It must match the deployment's bridge: the
demo bridge retains the on-change state with ``transient_local`` so a
late-joining SI still sees it, while a bridge that publishes ``volatile``
needs this set to ``volatile`` (a ``transient_local`` request does not match a
``volatile`` writer at all, so no state would ever arrive).

**********************
Publications (outputs)
**********************

The controller publishes the supervised output plus the legacy command topic
and two debug streams.

.. list-table::
   :widths: 42 33 25
   :header-rows: 1

   * - Topic
     - Message type
     - Bridge direction
   * - ``/control/safety_island/approved_request``
     - ``safety_island_msgs/msg/ApprovedRequest``
     - domain 2 only (the E2E actuator consumes it there)
   * - ``/control/trajectory_follower/control_cmd``
     - ``autoware_control_msgs/msg/Control``
     - 2 → 1, legacy policy below
   * - ``/control/trajectory_follower/lateral/debug/processing_time_ms``
     - ``tier4_debug_msgs/msg/Float64Stamped``
     - (not bridged)
   * - ``/control/trajectory_follower/longitudinal/debug/processing_time_ms``
     - ``tier4_debug_msgs/msg/Float64Stamped``
     - (not bridged)

``/control/safety_island/approved_request`` is the authoritative supervised
output: exactly one NORMAL / SI_STOP / HOLD decision per control cycle, carrying
the SI session, output sequence, mode, selected source, fault id and the
approved control payload. It is never filtered.

``/control/trajectory_follower/control_cmd`` is the transitional legacy DDS
surface: it carries the same gate-approved payload. ``SI_LEGACY_CONTROL_CMD``
selects between ``always`` (build default: NORMAL, HOLD and SI_STOP all reach
it) and ``stop_only`` (only SI_STOP, for a consumer that lets a fresh SI
command win over its own driver). CAN output is not affected by this policy.

The debug processing-time topics stay on domain 2; connect a DDS tool
directly to that domain (for example over the VPN) to monitor them.

**********************
Rates and timing
**********************

- Control loop period: **150 ms** (``ctrl_period`` parameter, default
  ``0.15`` seconds).
- Supervised output: one ``ApprovedRequest`` per control cycle.
- Per-source arrival watchdogs: selected candidate **1.0 s**, odometry and
  acceleration **0.4 s**, steering report **0.5 s**. They are measured on the
  SI clock against arrival time, never against message stamps. The operation
  mode has **no age limit**: it is published only when the mode changes and
  the deployment retains it, so its age says nothing about liveness; only
  "seen at least once" applies.
- Stale-output timeout parameter: **0.5 s** (``timeout_thr_sec`` default).

``timeout_thr_sec`` is declared by the controller, and the ``isTimeOut`` helper
still exists, but the timeout guard is currently disabled in the timer callback.
The supervisor replaced the old all-inputs staleness gate: before an input has
ever arrived the SI publishes HOLD; once a source has been seen, its own
watchdog governs, and a stale selected candidate or vehicle feedback latches
``SI_STOP`` until the explicit re-enable. See
:doc:`vp_si_control_contract`.
