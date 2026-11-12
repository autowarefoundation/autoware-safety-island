#!/usr/bin/env bash
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Closed-loop check of the FreeRTOS POSIX controller: replay the recorded
# follower inputs into it over DDS, record the commands it publishes, and
# compare them with what Autoware's own follower produced for the same bag.
# When the bag ends the inputs stop, so the same run checks that the
# controller latches SI_STOP and keeps publishing the stop command.
#
# Needs ROS 2 and the Autoware message packages (the devcontainer has both)
# and the controller binary: pass its path, or build build/freertos-posix
# first.

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
CONTROLLER="${1:-${ROOT_DIR}/build/freertos-posix/actuation_freertos}"
OUT_DIR="${ROOT_DIR}/build/freertos-posix-replay"
ROSBAG_DIR="${ROOT_DIR}/actuation_module/test/rosbag_test"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

if [ ! -x "${CONTROLLER}" ]; then
  echo "Controller binary not found: ${CONTROLLER}" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"
controller_log="${OUT_DIR}/controller.log"
commands_csv="${OUT_DIR}/control_cmd.csv"
bag_log="${OUT_DIR}/bag.log"
recorder_log="${OUT_DIR}/recorder.log"
rm -f "${controller_log}" "${commands_csv}" "${bag_log}" "${recorder_log}"

# The ROS setup scripts read unset variables.
set +u
# shellcheck disable=SC1091
source "/opt/ros/${ROS_DISTRO:-humble}/setup.bash"
# shellcheck disable=SC1091
source /opt/autoware/setup.bash
set -u

# The controller is built for domain 2 on the loopback interface. The ROS side
# joins it there and discovers it over unicast, so the test does not depend on
# multicast being enabled on "lo". MaxMessageSize matches the controller's
# receive limit (demo/bridge/cyclonedds-domain2.xml).
export ROS_DOMAIN_ID=2
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI='<CycloneDDS><Domain id="any"><General><Interfaces><NetworkInterface name="lo"/></Interfaces><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize></General><Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>60</MaxAutoParticipantIndex><Peers><Peer address="localhost"/></Peers></Discovery></Domain></CycloneDDS>'

controller_pid=""
recorder_pid=""
cleanup() {
  [ -n "${recorder_pid}" ] && kill "${recorder_pid}" 2>/dev/null || true
  [ -n "${controller_pid}" ] && kill_with_timeout "${controller_pid}" || true
}
trap cleanup EXIT

"${CONTROLLER}" >"${controller_log}" 2>&1 &
controller_pid=$!
python3 "${ROOT_DIR}/.github/scripts/record-control-cmd.py" "${commands_csv}" \
  >"${recorder_log}" 2>&1 &
recorder_pid=$!
# Let the controller, the recorder and discovery settle before the first sample.
sleep 5

# The bag carries the follower input under its older name; the Safety Island
# reads /planning/trajectory (see rosbag_test/play_rosbag.sh).
set +e
timeout --kill-after=5s 120s ros2 bag play "${ROSBAG_DIR}/rosbag" \
  --topics \
    /vehicle/status/steering_status \
    /planning/scenario_planning/trajectory \
    /localization/kinematic_state \
    /localization/acceleration \
    /system/operation_mode/state \
  --remap /planning/scenario_planning/trajectory:=/planning/trajectory \
  >"${bag_log}" 2>&1
bag_rc=$?
set -e
if [ "${bag_rc}" -ne 0 ]; then
  dump_log "${bag_log}"
  echo "ros2 bag play failed: ${bag_rc}" >&2
  exit "${bag_rc}"
fi

# The inputs have stopped: give the source watchdogs time to latch SI_STOP and
# the recorder time to see the stop commands that follow.
sleep 5

kill -INT "${recorder_pid}" 2>/dev/null || true
wait "${recorder_pid}" 2>/dev/null || true
recorder_pid=""
kill_with_timeout "${controller_pid}"
controller_pid=""

if ! python3 "${ROOT_DIR}/.github/scripts/check-rosbag-replay.py" \
  "${commands_csv}" "${controller_log}" \
  "${ROSBAG_DIR}/test_control_cmd_outputs/autoware_original_control_cmd.yaml"; then
  dump_log "${controller_log}"
  dump_log "${recorder_log}"
  exit 1
fi
