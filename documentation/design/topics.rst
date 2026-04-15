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

The controller subscribes to five topics on domain 2. The bridge
forwards them from domain 1.

.. list-table::
   :widths: 35 40 25
   :header-rows: 1

   * - Topic
     - Message type
     - Bridge direction
   * - ``/vehicle/status/steering_status``
     - ``autoware_vehicle_msgs/msg/SteeringReport``
     - 1 → 2
   * - ``/planning/scenario_planning/trajectory``
     - ``autoware_planning_msgs/msg/Trajectory``
     - 1 → 2
   * - ``/localization/kinematic_state``
     - ``nav_msgs/msg/Odometry``
     - 1 → 2
   * - ``/localization/acceleration``
     - ``geometry_msgs/msg/AccelWithCovarianceStamped``
     - 1 → 2
   * - ``/system/operation_mode/state``
     - ``autoware_adapi_v1_msgs/msg/OperationModeState``
     - 1 → 2

**********************
Publications (outputs)
**********************

The controller publishes the primary control command plus two debug
streams.

.. list-table::
   :widths: 45 35 20
   :header-rows: 1

   * - Topic
     - Message type
     - Bridge direction
   * - ``/control/trajectory_follower/control_cmd``
     - ``autoware_control_msgs/msg/Control``
     - 2 → 1
   * - ``/control/trajectory_follower/lateral/debug/processing_time_ms``
     - ``tier4_debug_msgs/msg/Float64Stamped``
     - (not bridged)
   * - ``/control/trajectory_follower/longitudinal/debug/processing_time_ms``
     - ``tier4_debug_msgs/msg/Float64Stamped``
     - (not bridged)

The debug processing-time topics stay on domain 2; connect a DDS tool
directly to that domain (for example over the VPN) to monitor them.

**********************
Rates and timing
**********************

- Control loop period: **150 ms** (``ctrl_period`` parameter, default
  ``0.15`` seconds).
- Stale-output timeout: **0.5 s** (``timeout_thr_sec`` parameter).

The timeout applies to the control command produced by the lateral and
longitudinal controllers: if the last output is older than
``timeout_thr_sec``, the tick is skipped. Missing inputs are handled
separately. See :doc:`architecture`.
