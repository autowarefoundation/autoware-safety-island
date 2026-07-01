..
 # Copyright (c) 2021-2026, Arm Limited.
 #
 # SPDX-License-Identifier: Apache-2.0

##########
Overview
##########

The Autoware Safety Island is an actuation module that runs Autoware's
trajectory follower on an Arm safety-class processor. It consumes planning,
localization, and vehicle-state topics from the Autoware main compute, runs
MPC lateral and PID longitudinal control, and publishes control commands back
out over DDS. No changes to the upstream Autoware codebase are required.

The module supports Zephyr and FreeRTOS runtime targets for both local
validation and S32Z hardware development. A platform abstraction layer
(``actuation_module/include/platform/``) keeps the controller logic shared
between runtime backends.

.. list-table:: Runtime targets
   :widths: 20 40 40
   :header-rows: 1

   * - Runtime
     - Local validation
     - S32Z hardware
   * - Zephyr
     - ``zephyr-fvp``
     - ``zephyr-s32z``
   * - FreeRTOS
     - ``freertos-posix``
     - ``freertos-s32z2``

``zephyr-fvp`` and ``freertos-posix`` are validated local development targets.
``zephyr-fvp`` also covers AVH workflows. ``zephyr-s32z`` is the existing S32Z
Zephyr hardware target.
``freertos-s32z2`` is hardware-specific and requires NXP-licensed SDK inputs.

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
and compiled as part of the safety island application.

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
ROS RCL abstraction mapping
*********************************

Autoware code is written against ROS 2's ``rcl`` layer. In this project the
equivalents are built directly on runtime primitives, so no ROS 2 runtime is
needed on the safety island runtime target.

.. list-table::
   :widths: 50 50
   :header-rows: 1

   * - ROS 2 (rcl)
     - Safety-island equivalent
   * - Logging
     - Custom logger (``include/common/logger/logger.hpp``)
   * - Node
     - POSIX-style thread wrapper with platform-provided stacks
       (``include/common/node/node.hpp`` and ``include/platform/``)
   * - Timers
     - Runtime timer wrapper (``include/common/node/timer.hpp``)
   * - Publisher / Subscriber
     - CycloneDDS (``include/common/dds/``)
