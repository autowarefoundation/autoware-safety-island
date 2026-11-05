#!/bin/bash

# shellcheck disable=SC1091
source "/opt/ros/humble/setup.bash"
source "/opt/autoware/setup.bash"

# The bag carries the follower input under its older name,
# /planning/scenario_planning/trajectory. The Safety Island reads
# /planning/trajectory, so replay it under that name.
ros2 bag play --start-paused /autoware-safety-island/actuation_module/test/rosbag_test/rosbag \
    --topics \
    /vehicle/status/steering_status \
    /planning/scenario_planning/trajectory \
    /localization/kinematic_state \
    /localization/acceleration \
    /system/operation_mode/state \
    --remap /planning/scenario_planning/trajectory:=/planning/trajectory
    # /control/trajectory_follower/control_cmd
