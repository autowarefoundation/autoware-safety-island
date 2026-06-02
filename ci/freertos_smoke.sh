#!/usr/bin/env bash
set -euo pipefail

binary=${1:-build-freertos/app/actuation_freertos}
log_file=${2:-build-freertos/app/freertos-smoke.log}

dump_log() {
  if [ -f "$log_file" ]; then
    cat "$log_file"
  fi
}

fail() {
  printf '%s\n' "$1" >&2
  dump_log >&2
  exit 1
}

require_marker() {
  local marker="$1"
  if ! grep -Fq -- "$marker" "$log_file"; then
    fail "Missing startup marker: $marker"
  fi
}

[ -e "$binary" ] || fail "Smoke binary missing: $binary"
[ -x "$binary" ] || fail "Smoke binary is not executable: $binary"

rm -f "$log_file"

set +e
timeout 20s "$binary" >"$log_file" 2>&1
status=$?
set -e

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
  fail "Smoke binary exited with unexpected status: $status"
fi

require_marker 'FreeRTOS POSIX simulator starting...'
require_marker 'Starting Controller Node...'

printf '%s\n' 'Smoke OK: startup markers observed'
