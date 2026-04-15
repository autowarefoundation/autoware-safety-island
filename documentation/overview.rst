..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##########
Overview
##########

The Autoware Safety Island is a Zephyr RTOS application that runs Autoware's
trajectory follower on an Arm safety-class processor. It consumes planning,
localization, and vehicle-state topics from the Autoware main compute, runs
MPC lateral and PID longitudinal control, and publishes control commands back
out over DDS. No changes to the upstream Autoware codebase are required.

The main compute and the safety island run on separate DDS domains. A
domain bridge on the main compute forwards the relevant topics between them,
which isolates the real-time controller from the rest of the Autoware graph.

********
Workflow
********

.. mermaid::

   graph TD
       subgraph Inputs
           Trajectory["Trajectory<br/>(TrajectoryMsg_Raw)"]
           Odometry["Odometry<br/>(OdometryMsg)"]
           Steering["Steering<br/>(SteeringReportMsg)"]
           Acceleration["Acceleration<br/>(AccelWithCovarianceStampedMsg)"]
           OperationMode["Operation Mode<br/>(OperationModeStateMsg)"]
       end

       subgraph "Actuation Module"
           ControllerNode["Controller Node<br/><br/>Lateral Controller: MPC<br/>Longitudinal Controller: PID"]
       end

       subgraph Outputs
           ControlCommand["Control Command<br/>(ControlMsg)"]
       end

       Trajectory --> ControllerNode
       Odometry --> ControllerNode
       Steering --> ControllerNode
       Acceleration --> ControllerNode
       OperationMode --> ControllerNode

       ControllerNode --> ControlCommand

See :doc:`design/topics` for the full list of DDS topics with message types
and domain IDs, and :doc:`design/architecture` for the runtime design.

***************
Main Components
***************

.. list-table::
   :widths: 50 50
   :header-rows: 1

   * - Component
     - Version
   * - Zephyr RTOS
     - `3.6.0 <https://github.com/zephyrproject-rtos/zephyr/commit/6aeb7a2b96c2b212a34f00c0ad3862ac19e826e8>`_
   * - CycloneDDS
     - `0.11.x <https://github.com/eclipse-cyclonedds/cyclonedds/commit/7c253ad3c4461b10dc4cac36a257b097802cd043>`_
   * - Autoware
     - `2025.02 <https://github.com/autowarefoundation/autoware/tree/2025.02>`_
   * - Autoware.Universe
     - `0.40.0 <https://github.com/autowarefoundation/autoware.universe/tree/0.40.0>`_
   * - Autoware.msgs
     - `1.3.0 <https://github.com/autowarefoundation/autoware_msgs/tree/1.3.0>`_

*******************
Autoware Components
*******************

The following Autoware packages are vendored into ``actuation_module/src/autoware/``
and compiled as part of the Zephyr application.

.. list-table::
   :widths: 50 50
   :header-rows: 1

   * - Component
     - Role
   * - autoware_msgs
     - Message definitions (IDL)
   * - autoware_osqp_interface
     - OSQP solver wrapper for MPC
   * - autoware_universe_utils
     - General utilities
   * - autoware_motion_utils
     - Motion primitives
   * - autoware_interpolation
     - Trajectory interpolation
   * - autoware_vehicle_info_utils
     - Vehicle model parameters
   * - autoware_trajectory_follower_base
     - Controller base classes
   * - autoware_mpc_lateral_controller
     - MPC lateral controller
   * - autoware_pid_longitudinal_controller
     - PID longitudinal controller
   * - autoware_trajectory_follower_node
     - Controller node entry point

*********************************
ROS RCL to Zephyr mapping
*********************************

Autoware code is written against ROS 2's ``rcl`` layer. In this project the
equivalents are built directly on Zephyr primitives, so no ROS 2 runtime is
needed on the safety island.

.. list-table::
   :widths: 50 50
   :header-rows: 1

   * - ROS 2 (rcl)
     - Zephyr equivalent
   * - Logging
     - Custom logger (``include/common/logger/logger.hpp``)
   * - Node
     - POSIX threads on Zephyr stacks (``include/common/node/node.hpp``)
   * - Timers
     - Zephyr software timers
   * - Publisher / Subscriber
     - CycloneDDS (``include/common/dds/``)
