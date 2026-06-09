#!/usr/bin/env bash
# Shared helpers for runtime CI phases.
# Each runtime workflow step sources this file once at the top.
#
# Usage:
#   source .github/scripts/ci-helpers.sh
#   run_with_timeout <binary> <log_path> <timeout_seconds>
#   run_command_with_timeout <log_path> <timeout_seconds> <command> [args...]
#   require_marker   <log_path> <fixed-string marker>
#   kill_with_timeout <pid> [grace_seconds]

set -euo pipefail

# SIGTERM grace period before timeout(1) / kill_with_timeout escalate to SIGKILL.
# Bounds every CI step's worst-case wall time so a blocked-on-SIGTERM binary
# cannot hang the runner.
CI_KILL_AFTER_SECONDS="${CI_KILL_AFTER_SECONDS:-5}"

dump_log() {
  local log="$1"
  if [ -f "$log" ]; then
    echo "----- log: $log -----"
    cat "$log"
    echo "----- end log -----"
  fi
}

# Run a command with a wall-clock timeout. Exit 0 (clean) and 124 (SIGTERM on
# timeout) are both considered success. GNU timeout may return 137 after
# --kill-after escalates to SIGKILL, so treat that as a bounded timeout too.
is_success_or_timeout() {
  local rc="$1"
  [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]
}

# Run a command with a wall-clock timeout. timeout(1) escalates to SIGKILL after
# CI_KILL_AFTER_SECONDS so a command that ignores SIGTERM still terminates.
# Non-timeout exits dump the log and fail.
run_command_with_timeout() {
  local log="$1"
  local secs="$2"
  shift 2

  rm -f "$log"
  set +e
  timeout --kill-after="${CI_KILL_AFTER_SECONDS}s" "${secs}s" "$@" >"$log" 2>&1
  local rc=$?
  set -e

  if ! is_success_or_timeout "$rc"; then
    dump_log "$log"
    echo "Unexpected exit status $rc from command: $*" >&2
    exit "$rc"
  fi
}

# Run a binary with a wall-clock timeout. Non-timeout exits dump the log and fail.
run_with_timeout() {
  local bin="$1"
  local log="$2"
  local secs="$3"

  if [ ! -x "$bin" ]; then
    echo "Missing or non-executable binary: $bin" >&2
    exit 1
  fi

  run_command_with_timeout "$log" "$secs" "$bin"
}

# Send SIGTERM to a backgrounded pid and wait up to `grace_seconds` (default
# CI_KILL_AFTER_SECONDS) for it to exit. If still alive after the grace window,
# escalate to SIGKILL. Always reaps the pid via `wait` so the step cannot hang
# on an unbounded `wait $pid`.
kill_with_timeout() {
  local pid="$1"
  local grace="${2:-$CI_KILL_AFTER_SECONDS}"

  kill -TERM "$pid" 2>/dev/null || true
  local i=0
  while [ "$i" -lt "$grace" ]; do
    if ! kill -0 "$pid" 2>/dev/null; then
      break
    fi
    sleep 1
    i=$((i + 1))
  done
  kill -KILL "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

# Fail if the fixed-string marker is missing from the log.
require_marker() {
  local log="$1"
  local marker="$2"
  if ! grep -Fq -- "$marker" "$log"; then
    dump_log "$log"
    echo "Missing marker in $log: $marker" >&2
    exit 1
  fi
}
