#!/usr/bin/env bash
# FreeRTOS POSIX runtime CI phases.

set -euo pipefail

ROOT_DIR="${GITHUB_WORKSPACE:-$(pwd)}"
BUILD_ROOT="${ROOT_DIR}/build/freertos-posix"

source "${ROOT_DIR}/.github/scripts/ci-helpers.sh"

cd "${ROOT_DIR}"

echo "Phase 1 - FreeRTOS POSIX full controller build + runtime smoke"
"${ROOT_DIR}/build.sh" --platform freertos-posix -d "${BUILD_ROOT}"
run_with_timeout "${BUILD_ROOT}/actuation_freertos" \
  "${BUILD_ROOT}/controller.log" 20
require_marker "${BUILD_ROOT}/controller.log" "FreeRTOS POSIX simulator starting..."
require_marker "${BUILD_ROOT}/controller.log" "Starting Controller Node..."
require_marker "${BUILD_ROOT}/controller.log" "Controller Node Started"
require_marker "${BUILD_ROOT}/controller.log" "Actuation Safety Island is Live"
echo "Controller smoke OK: all 4 markers observed"

echo "Phase 2 - FreeRTOS POSIX unit_test build + run"
"${ROOT_DIR}/build.sh" --platform freertos-posix -d "${BUILD_ROOT}-unit" --unit-test
run_with_timeout "${BUILD_ROOT}-unit/actuation_freertos" \
  "${BUILD_ROOT}-unit/unit.log" 30
require_marker "${BUILD_ROOT}-unit/unit.log" "=== All Tests Passed ==="
echo "unit_test OK"

echo "Phase 3 - FreeRTOS POSIX DDS pub/sub paired build + run"
"${ROOT_DIR}/build.sh" --platform freertos-posix -d "${BUILD_ROOT}-pub" --dds-publisher
"${ROOT_DIR}/build.sh" --platform freertos-posix -d "${BUILD_ROOT}-sub" --dds-subscriber

sub_log="${BUILD_ROOT}-sub/sub.log"
pub_log="${BUILD_ROOT}-pub/pub.log"
rm -f "${sub_log}" "${pub_log}"

# Subscriber first so discovery is hot when the publisher boots
"${BUILD_ROOT}-sub/actuation_freertos" >"${sub_log}" 2>&1 &
sub_pid=$!
sleep 2

# Publisher publishes /vehicle/status/steering_status every 2s.
# --kill-after escalates to SIGKILL if the binary ignores SIGTERM.
set +e
timeout --kill-after=5s 15s \
  "${BUILD_ROOT}-pub/actuation_freertos" >"${pub_log}" 2>&1
pub_rc=$?
set -e
if ! is_success_or_timeout "$pub_rc"; then
  dump_log "${pub_log}"
  dump_log "${sub_log}"
  kill_with_timeout "${sub_pid}"
  echo "Publisher exited unexpectedly: ${pub_rc}" >&2
  exit "${pub_rc}"
fi

# Drain time for the subscriber to consume in-flight messages,
# then bounded teardown (SIGTERM -> SIGKILL after grace period).
sleep 2
kill_with_timeout "${sub_pid}"

require_marker "${pub_log}" "Starting DDS publisher"

count=$(grep -c "STEERING REPORT" "${sub_log}" || true)
if [ "${count}" -lt 2 ]; then
  dump_log "${pub_log}"
  dump_log "${sub_log}"
  echo "Subscriber observed ${count} STEERING REPORTs; need >= 2" >&2
  exit 1
fi
echo "DDS pair OK: subscriber observed ${count} STEERING REPORTs"

echo "Phase 4 - FreeRTOS POSIX CAN output tests"
"${ROOT_DIR}/build.sh" --platform freertos-posix -d "${BUILD_ROOT}-can" \
  --can-output-test --control-output DDS_AND_CAN
run_with_timeout "${BUILD_ROOT}-can/actuation_freertos" \
  "${BUILD_ROOT}-can/can.log" 20
require_marker "${BUILD_ROOT}-can/can.log" "CAN output tests passed"
echo "CAN test OK"

echo "FreeRTOS POSIX runtime validation OK"
