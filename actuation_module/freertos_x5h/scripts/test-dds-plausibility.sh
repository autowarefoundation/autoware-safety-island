#!/usr/bin/env bash
# test-dds-plausibility.sh -- host-runnable regression test for
# dds-plausibility.awk, the field-plausibility parser verify-x5h-dds.sh's
# `DDS_ROUNDTRIP_PASS` / `DDS_ROUNDTRIP_FAIL reason=implausible` gate
# delegates to (see verify-x5h-dds.sh's "field plausibility" section).
#
# This awk block had never executed against real board data before the
# defect it exists to catch: reading fields by positional index ($2/$5)
# instead of by the key name next to them silently misparsed every
# prefixed subscriber log line -- $2 landed on a literal '|' and an
# anchored /^accel:/ pattern matched zero prefixed lines -- turning a
# clean round trip into a false DDS_ROUNDTRIP_FAIL. This test runs the
# real dds-plausibility.awk file (not a re-implementation of its logic)
# against fixture log lines covering:
#   - the real prefixed shape (ANSI colour + timestamp + '|'; hand-written
#     to match the format actuation_module/test/dds_sub.cpp's log_success()
#     calls actually print, per the NDA boundary -- no captured board
#     output is checked into this repo)
#   - the bare, unprefixed shape
#   - an out-of-range steering, accel, and velocity value (one case each)
#   - a NaN/inf value
#   - a non-numeric token where a value belongs (the historical bug itself:
#     awk silently coerces a non-numeric string to 0, which would sail
#     through every range check -- must fail outright, not coerce)
#   - a log with zero matching lines (must fail, not vacuously pass)
#   - values sitting just inside every bound (guards against a parser
#     that merely finds *a* field without reading the right one)
#
# Invocation, matching the other host-side checks in this directory
# (check-dds-config.sh, check-elf-contract.sh, check-image-budget.sh): run
# directly, no external test framework, PASS/FAIL banner plus per-case
# reasons on a FAIL, exit 0/1.
set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
AWK_SCRIPT="${SCRIPT_DIR}/dds-plausibility.awk"

MAXSTEER=1.0
MAXACCEL=10.0
MAXVEL=100.0

[ -r "${AWK_SCRIPT}" ] || {
    echo "FAIL: test-dds-plausibility.sh" >&2
    echo "  - missing ${AWK_SCRIPT}" >&2
    exit 1
}

fail=0
fail_reasons=()

record_fail() {
    fail=1
    fail_reasons+=("$1")
}

# run_case <name> <expect: pass|fail> <fixture text>
run_case() {
    local name="$1" expect="$2" fixture="$3"
    local out rc
    out=$(printf '%s\n' "$fixture" |
              awk -v maxsteer="$MAXSTEER" -v maxaccel="$MAXACCEL" -v maxvel="$MAXVEL" \
                  -f "$AWK_SCRIPT" 2>&1)
    rc=$?
    case "$expect" in
        pass)
            [ "$rc" -eq 0 ] ||
                record_fail "${name}: expected PASS (exit 0), got exit ${rc}: ${out:-<no output>}"
            ;;
        fail)
            [ "$rc" -ne 0 ] ||
                record_fail "${name}: expected FAIL (non-zero exit), got exit 0 -- a vacuous or wrongly-plausible pass"
            ;;
        *)
            record_fail "${name}: bad test case, expect must be pass|fail, got '${expect}'"
            ;;
    esac
}

# --- case 1: the real prefixed shape, as actually captured on hardware ------
PREFIXED_FIXTURE=$'\033[32m[00:04:09.069] | steering_tire_angle: -0.236017 rad\n'\
$'\033[32m[00:04:09.069] | accel: 0.000000 m/s^2  velocity: 5.000000 m/s\n'\
$'\033[32m[00:04:09.219] | steering_tire_angle: -0.236017 rad\n'\
$'\033[32m[00:04:09.219] | accel: 0.000000 m/s^2  velocity: 5.000000 m/s'
run_case "real prefixed shape (ANSI + timestamp + |)" pass "$PREFIXED_FIXTURE"

# --- case 2: the bare, unprefixed shape --------------------------------------
BARE_FIXTURE='steering_tire_angle: -0.236017 rad
accel: 0.000000 m/s^2  velocity: 5.000000 m/s'
run_case "bare shape (no ANSI/timestamp prefix)" pass "$BARE_FIXTURE"

# --- case 3: an out-of-range steering value (limit MAX_STEER_RAD=1.0) -------
OOR_STEER_FIXTURE=$'\033[32m[00:04:09.069] | steering_tire_angle: 2.500000 rad\n'\
$'\033[32m[00:04:09.069] | accel: 0.000000 m/s^2  velocity: 5.000000 m/s'
run_case "out-of-range steering_tire_angle" fail "$OOR_STEER_FIXTURE"

# --- case 3b: an out-of-range accel value (limit MAX_ACCEL=10.0) ------------
OOR_ACCEL_FIXTURE=$'\033[32m[00:04:09.069] | steering_tire_angle: -0.236017 rad\n'\
$'\033[32m[00:04:09.069] | accel: 25.000000 m/s^2  velocity: 5.000000 m/s'
run_case "out-of-range accel" fail "$OOR_ACCEL_FIXTURE"

# --- case 3c: an out-of-range velocity value (limit MAX_VEL=100.0) ----------
OOR_VEL_FIXTURE=$'\033[32m[00:04:09.069] | steering_tire_angle: -0.236017 rad\n'\
$'\033[32m[00:04:09.069] | accel: 0.000000 m/s^2  velocity: 250.000000 m/s'
run_case "out-of-range velocity" fail "$OOR_VEL_FIXTURE"

# --- case 4: a NaN/inf value --------------------------------------------------
NAN_FIXTURE=$'\033[32m[00:04:09.069] | steering_tire_angle: -0.236017 rad\n'\
$'\033[32m[00:04:09.069] | accel: nan m/s^2  velocity: inf m/s'
run_case "NaN accel / inf velocity" fail "$NAN_FIXTURE"

# --- case 4b: a non-numeric token where the value belongs --------------------
# This is the exact class of historical bug, reproduced directly: a garbage
# token sitting where the numeric value belongs. awk's implicit string ->
# number coercion turns any non-numeric string into the number 0, which
# then sails through every subsequent range check -- so a naive fix that
# merely reads the right field by key name (rather than also validating
# its shape) can still produce a false PASS on exactly this input.
# Confirmed empirically against the parser as it stood at this branch's
# base commit (8b05474): "0.5abc" parsed as 0 and the run reported PASS.
# A single '|' was tried too and, by coincidence of mawk's string
# comparison rules (a bare non-numeric string compares against a limit as
# text, and '|' sorts after any ASCII digit), already failed there for the
# wrong reason -- "0.5abc" is the fixture that actually isolates the
# coercion defect this requirement targets.
NONNUMERIC_FIXTURE=$'\033[32m[00:04:09.069] | steering_tire_angle: 0.5abc rad\n'\
$'\033[32m[00:04:09.069] | accel: 0.000000 m/s^2  velocity: 5.000000 m/s'
run_case "non-numeric token where steering value belongs" fail "$NONNUMERIC_FIXTURE"

# --- case 5: zero matching lines (the vacuous-pass guard must still fire) ---
EMPTY_FIXTURE='INFO bundle=/var/tmp/edge-ecu-peer count>=100 window=120s
INFO probe control_cmd=1'
run_case "zero matching lines (vacuous-pass guard)" fail "$EMPTY_FIXTURE"

# --- case 6: values sitting just inside every bound --------------------------
# Also guards the exact historical defect a step further than case 1: proves
# the VALUE read is the one next to the key, not merely that some field
# parsed -- a parser that grabbed the wrong field would fail this with
# different bounds even if it happened to pass case 1.
INBOUND_FIXTURE=$'\033[32m[00:04:09.069] | steering_tire_angle: -0.999999 rad\n'\
$'\033[32m[00:04:09.069] | accel: 9.999999 m/s^2  velocity: 99.999999 m/s'
run_case "values just inside every bound" pass "$INBOUND_FIXTURE"

if [ "$fail" -ne 0 ]; then
    echo "FAIL: test-dds-plausibility.sh" >&2
    for reason in "${fail_reasons[@]}"; do
        echo "  - ${reason}" >&2
    done
    exit 1
fi

echo "PASS: test-dds-plausibility.sh"
echo "  10/10 cases: prefixed shape, bare shape, out-of-range steering/accel/velocity," \
     "NaN/inf, non-numeric token, zero-match guard, in-bound values"
