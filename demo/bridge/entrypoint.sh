#!/usr/bin/env bash
# Start domain_bridge with the Safety Island topic contract
# (/autoware/bridge-config.yaml).
set -eo pipefail

source "/opt/ros/$ROS_DISTRO/setup.bash"
source "/opt/autoware/setup.bash"

exec ros2 run domain_bridge domain_bridge --wait-for-publisher false /autoware/bridge-config.yaml
