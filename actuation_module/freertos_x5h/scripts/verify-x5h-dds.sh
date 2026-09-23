#!/usr/bin/env bash
# verify-x5h-dds.sh -- DDS round-trip verification for the R-Car X5H CR52.
#
# Runs ON the AutoSD side of the board (invoke it over SSH from the companion
# host). Drives the arm64 edge_ecu_peer bundle against the actuation firmware
# running on the CR52, reachable only over the rpmsg-eth IP link.
#
# The round trip being asserted is: host publishes trajectory/odometry ->
# CR52 runs MPC/PID -> CR52 publishes control_cmd -> host receives it. Receiving
# control_cmd is the only thing that closes the loop; STEERING REPORT is the
# host publisher's own loopback and proves nothing about the board.
#
# Markers on stdout (grep-able, one per line):
#   DDS_ROUNDTRIP_PASS count=<n>
#   DDS_ROUNDTRIP_FAIL reason=<...>
#
# reason= vocabulary:
#   bad_args          unparsable option
#   no_bundle         peer bundle or a binary inside it is missing
#   no_xml            the CycloneDDS config is missing
#   link_down         rpmsg-eth inactive, or tap0 missing / without carrier
#   no_channel        the CR52's rpmsg-eth channel is not on the rpmsg bus
#   no_firstcontact   zero control_cmd samples in the short probe -- the board
#                     never answered, so the long run would only waste time
#   low_count         control_cmd arrived but fewer than the required count
#   implausible       a control_cmd field was non-finite or outside its range
#   oversize_drops    the daemon dropped oversize frames: MTU mismatch between
#                     the Linux and CR52 ends of the link
#
# Deliberate deviation from the plan, recorded here because it changes what is
# asserted: the plan's step 3 calls for a discovery check in which "dds_sub
# alone sees the CR52 participant". That check cannot fail meaningfully -- the
# board publishes control_cmd only in response to host input (see
# actuation_module/test/dds_sub.cpp's handle_control_cmd comment), so dds_sub on
# its own observes nothing whether discovery worked or not. Replaced with a
# short pub+sub probe requiring >=1 sample, which distinguishes "nothing works"
# from "works but too slowly", and is a gate that can actually go red.
set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

BUNDLE=${BUNDLE:-/var/tmp/edge-ecu-peer}
COUNT=${COUNT:-100}
WINDOW=${WINDOW:-120}
PROBE_WINDOW=${PROBE_WINDOW:-20}
SERVICE=${SERVICE:-rpmsg-eth}
IFACE=${IFACE:-tap0}

# Plausibility bounds. NOTE: verify-b2.sh, which the plan names as the source of
# these thresholds, contains no field-range assertions at all -- it only counts
# markers. These bounds are therefore new, and chosen to be loose enough that a
# working controller cannot trip them while still catching the failure they
# exist for: a garbage/uninitialised payload surviving the wire.
MAX_STEER_RAD=${MAX_STEER_RAD:-1.0}
MAX_ACCEL=${MAX_ACCEL:-10.0}
MAX_VEL=${MAX_VEL:-100.0}

usage() {
    echo "usage: verify-x5h-dds.sh [-b bundle_dir] [-n count] [-w window_s]" >&2
}

while [ $# -gt 0 ]; do
    case "$1" in
        -b) [ $# -ge 2 ] || { echo "DDS_ROUNDTRIP_FAIL reason=bad_args"; usage; exit 2; }
            BUNDLE=$2; shift 2 ;;
        -n) [ $# -ge 2 ] || { echo "DDS_ROUNDTRIP_FAIL reason=bad_args"; usage; exit 2; }
            COUNT=$2; shift 2 ;;
        -w) [ $# -ge 2 ] || { echo "DDS_ROUNDTRIP_FAIL reason=bad_args"; usage; exit 2; }
            WINDOW=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *)  echo "DDS_ROUNDTRIP_FAIL reason=bad_args"; usage; exit 2 ;;
    esac
done

fail() {
    echo "DDS_ROUNDTRIP_FAIL reason=$1"
    exit 1
}

# --- preconditions -----------------------------------------------------------
PUB="${BUNDLE}/edge_ecu_pub"
SUB="${BUNDLE}/edge_ecu_sub"
XML="${BUNDLE}/cyclonedds-x5h.xml"

[ -x "$PUB" ] || fail no_bundle
[ -x "$SUB" ] || fail no_bundle
[ -r "$XML" ] || fail no_xml

# The link must be up before DDS is asked to discover anything across it.
[ "$(systemctl is-active "$SERVICE" 2>/dev/null)" = active ] || fail link_down
link=$(ip -o link show "$IFACE" 2>/dev/null) || fail link_down
case "$link" in
    *NO-CARRIER*) fail link_down ;;
esac
case "$link" in
    *LOWER_UP*) ;;
    *) fail link_down ;;
esac

# The CR52 must actually be on the rpmsg bus. Without this a link_down pass
# would be blamed on DDS.
#
# Deliberately a glob rather than `ls | grep -q`: under `pipefail`, grep -q
# exits the moment it matches, ls takes SIGPIPE (141), and pipefail reports the
# pipeline as failed *because the match succeeded* -- firing no_channel on a
# healthy board. Three separate defects of exactly this shape have already been
# found in this project's scripts, so it is not a hypothetical.
channel_found=0
for d in /sys/bus/rpmsg/devices/*"${SERVICE}"*; do
    [ -e "$d" ] || continue
    channel_found=1
    break
done
[ "$channel_found" = 1 ] || fail no_channel

export CYCLONEDDS_URI="file://${XML}"
export LD_LIBRARY_PATH="${BUNDLE}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

echo "INFO bundle=${BUNDLE} count>=${COUNT} window=${WINDOW}s uri=${CYCLONEDDS_URI}"
echo "INFO link: $(echo "$link" | cut -c1-100)"

# --- daemon oversize-drop baseline -------------------------------------------
# An MTU mismatch between the two ends shows up as oversize drops during the
# DDS discovery burst, which is larger than anything ping produces. Sampled
# before and after so only drops from THIS run count.
drops_before=$(journalctl -u "${SERVICE}.service" --no-pager -n 200 2>/dev/null |
                   sed -n 's/.*dropped_oversize=\([0-9][0-9]*\).*/\1/p' | tail -1)
[ -n "$drops_before" ] || drops_before=0

PUB_LOG=$(mktemp /tmp/x5h-dds-pub.XXXXXX.log)
SUB_LOG=$(mktemp /tmp/x5h-dds-sub.XXXXXX.log)
PROBE_LOG=$(mktemp /tmp/x5h-dds-probe.XXXXXX.log)
PUB_PID=""
cleanup() {
    [ -n "$PUB_PID" ] && kill "$PUB_PID" 2>/dev/null
    wait "$PUB_PID" 2>/dev/null
    return 0
}
trap cleanup EXIT INT TERM

start_pub() {
    "$PUB" >"$1" 2>&1 &
    PUB_PID=$!
    # Readiness, not a fixed sleep: the publisher must still be alive a moment
    # later. A pub that dies immediately (bad config, missing lib) would
    # otherwise look like a board-side failure.
    i=0
    while [ "$i" -lt 10 ]; do
        kill -0 "$PUB_PID" 2>/dev/null || return 1
        i=$((i + 1))
        sleep 1
    done
    return 0
}

count_cmds() {
    grep -c 'CONTROL CMD' "$1" 2>/dev/null || true
}

# --- probe: does the board answer at all? ------------------------------------
echo "INFO probe: ${PROBE_WINDOW}s pub+sub, need >=1 control_cmd"
start_pub "$PUB_LOG" || fail no_firstcontact
timeout "${PROBE_WINDOW}s" "$SUB" >"$PROBE_LOG" 2>&1
probe_n=$(count_cmds "$PROBE_LOG")
echo "INFO probe control_cmd=${probe_n}"
if [ "${probe_n:-0}" -lt 1 ]; then
    echo "--- probe sub log tail ---" >&2
    tail -20 "$PROBE_LOG" >&2
    echo "--- pub log tail ---" >&2
    tail -20 "$PUB_LOG" >&2
    fail no_firstcontact
fi

# --- full run ----------------------------------------------------------------
echo "INFO full run: ${WINDOW}s, need >=${COUNT} control_cmd"
timeout "${WINDOW}s" "$SUB" >"$SUB_LOG" 2>&1
n=$(count_cmds "$SUB_LOG")
echo "INFO full run control_cmd=${n}"

# --- oversize drops during this run ------------------------------------------
drops_after=$(journalctl -u "${SERVICE}.service" --no-pager -n 200 2>/dev/null |
                  sed -n 's/.*dropped_oversize=\([0-9][0-9]*\).*/\1/p' | tail -1)
[ -n "$drops_after" ] || drops_after=$drops_before
if [ "$drops_after" -gt "$drops_before" ]; then
    echo "oversize drops rose ${drops_before} -> ${drops_after} during the run" >&2
    fail oversize_drops
fi
echo "INFO oversize_drops unchanged at ${drops_after}"

if [ "${n:-0}" -lt "$COUNT" ]; then
    echo "--- sub log tail ---" >&2
    tail -20 "$SUB_LOG" >&2
    fail low_count
fi

# --- field plausibility ------------------------------------------------------
# Guards against a payload that arrives but is garbage: NaN/inf, or values no
# controller would emit. awk does the numeric work because the values are
# floating point and the shell cannot compare those.
#
# The parser itself lives in dds-plausibility.awk, not inlined here: it reads
# each value by the KEY NAME next to it (steering_tire_angle:/accel:/
# velocity:), not by positional field index, so it survives real log lines
# carrying an ANSI colour escape + timestamp + '|' prefix ahead of the key
# (see that file's header for the defect a positional read produced on real
# hardware) -- and keeping it in its own file lets
# scripts/test-dds-plausibility.sh exercise this exact parser directly.
if ! awk -v maxsteer="$MAX_STEER_RAD" -v maxaccel="$MAX_ACCEL" -v maxvel="$MAX_VEL" \
    -f "${SCRIPT_DIR}/dds-plausibility.awk" "$SUB_LOG"; then
    fail implausible
fi

echo "DDS_ROUNDTRIP_PASS count=${n}"
