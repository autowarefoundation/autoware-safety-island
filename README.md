<!--
# Copyright (c) 2021-2026, Arm Limited.
#
# SPDX-License-Identifier: Apache-2.0
-->

# Autoware Safety Island

[![Documentation](https://img.shields.io/badge/docs-latest-brightgreen)](https://autowarefoundation.github.io/autoware-safety-island/)

A standalone Zephyr RTOS application that runs Autoware's trajectory
follower (MPC lateral, PID longitudinal) on an Arm safety-class processor
and exchanges control commands with Autoware over DDS. No changes to the
Autoware codebase are required.

### Workflow

```mermaid
graph TD
    subgraph Inputs
        Trajectory["Trajectory<br/>(TrajectoryMsg_Raw)"]
        Odometry["Odometry<br/>(OdometryMsg)"]
        Steering["Steering<br/>(SteeringReportMsg)"]
        Acceleration["Acceleration<br/>(AccelWithCovarianceStampedMsg)"]
        OperationMode["Operation Mode<br/>(OperationModeStateMsg)"]
    end

    subgraph "Actuation Module"
        ControllerNode["Controller Node<br/><br/>Lateral Controller: MPC <br/>Longitudinal Controller: PID"]
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
```

## Main Components

| Component | Version |
|--------------|---------------|
| Zephyr RTOS  | [3.6.0](https://github.com/zephyrproject-rtos/zephyr/commit/6aeb7a2b96c2b212a34f00c0ad3862ac19e826e8) |
| CycloneDDS  | [0.11.x](https://github.com/eclipse-cyclonedds/cyclonedds/commit/7c253ad3c4461b10dc4cac36a257b097802cd043) |
| Autoware    | [2025.02](https://github.com/autowarefoundation/autoware/tree/2025.02) |
| Autoware.Universe | [0.40.0](https://github.com/autowarefoundation/autoware.universe/tree/0.40.0) |
| Autoware.msgs | [1.3.0](https://github.com/autowarefoundation/autoware_msgs/tree/1.3.0) |

## Autoware Components

| Component | Description |
|-----------|---------|
| autoware_msgs | Autoware Messages |
| autoware_osqp_interface | OSQP Interface |
| autoware_universe_utils | Universe Utils |
| autoware_motion_utils | Motion Utils |
| autoware_interpolation | Interpolation Utils |
| autoware_vehicle_info_utils | Vehicle Info Utils |
| autoware_trajectory_follower_base | Trajectory Follower Base |
| autoware_mpc_lateral_controller | MPC Lateral Controller |
| autoware_pid_longitudinal_controller | PID Longitudinal Controller |
| autoware_trajectory_follower_node | Trajectory Follower Node |

## Getting Started

See the [documentation](https://autowarefoundation.github.io/autoware-safety-island/) — the [Quickstart](https://autowarefoundation.github.io/autoware-safety-island/user_guide/quickstart.html) builds and runs the FVP target in a few commands.
