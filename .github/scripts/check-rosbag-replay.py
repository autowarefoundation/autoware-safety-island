#!/usr/bin/env python3
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
"""Check a rosbag replay through the Safety Island against the Autoware reference.

The replay feeds the recorded follower inputs to the controller and records
the control_cmd it publishes. This script checks three things:

  1. While the bag plays, the controller follows: enough commands that are not
     the stop command, with steering close to what Autoware's own follower
     produced for the same bag.
  2. When the bag ends and the inputs stop, the controller latches SI_STOP on a
     source watchdog (its log says which).
  3. From then on it publishes only the stop command, and the speed target
     never rises.

Usage: check-rosbag-replay.py COMMANDS.csv CONTROLLER.log REFERENCE.yaml
"""

import csv
import re
import sys

# The stop command's acceleration: -SI_DEFAULT_STOP_DECEL_MPS2. HOLD (before
# the first trajectory) and SI_STOP both carry it; the follower's own output
# never sits exactly on it.
STOP_ACCELERATION = -1.5
MIN_FOLLOWED_COMMANDS = 150
# Mean absolute steering difference to the Autoware reference, after aligning
# the two sequences. The replay measured 0.005 rad; the bound leaves room for
# scheduling differences between runs, not for a different controller.
MAX_MEAN_STEERING_DIFF_RAD = 0.02
MAX_ALIGNMENT_SHIFT = 20
MIN_STOP_COMMANDS = 5
WATCHDOG_SOURCES = ("odometry", "acceleration", "steering report", "trajectory")


def fail(message: str) -> None:
    print(f"rosbag replay check FAILED: {message}", file=sys.stderr)
    sys.exit(1)


def load_commands(path: str):
    with open(path, newline="") as source:
        return [
            (float(row["steering_tire_angle"]), float(row["velocity"]), float(row["acceleration"]))
            for row in csv.DictReader(source)
        ]


def load_reference_steering(path: str):
    with open(path) as source:
        return [
            float(value)
            for value in re.findall(
                r"^\s*steering_tire_angle:\s*(-?[\d.eE+-]+)\s*$", source.read(), re.M
            )
        ]


def is_stop(command) -> bool:
    return abs(command[2] - STOP_ACCELERATION) < 1e-6


def mean_steering_diff(followed, reference):
    best = None
    for shift in range(-MAX_ALIGNMENT_SHIFT, MAX_ALIGNMENT_SHIFT + 1):
        pairs = [
            (followed[i], reference[i + shift])
            for i in range(len(followed))
            if 0 <= i + shift < len(reference)
        ]
        if len(pairs) < MIN_FOLLOWED_COMMANDS:
            continue
        diff = sum(abs(a - b) for a, b in pairs) / len(pairs)
        if best is None or diff < best[0]:
            best = (diff, shift)
    return best


def main() -> int:
    commands = load_commands(sys.argv[1])
    with open(sys.argv[2], errors="replace") as source:
        log = source.read()
    reference = load_reference_steering(sys.argv[3])

    if not commands:
        fail("no control_cmd was recorded")
    if not reference:
        fail("the reference has no steering_tire_angle values")

    followed_indexes = [i for i, command in enumerate(commands) if not is_stop(command)]
    if len(followed_indexes) < MIN_FOLLOWED_COMMANDS:
        fail(
            f"only {len(followed_indexes)} followed commands, need {MIN_FOLLOWED_COMMANDS}: "
            "the controller did not follow the replayed trajectory"
        )
    followed = [commands[i][0] for i in followed_indexes]
    alignment = mean_steering_diff(followed, reference)
    if alignment is None:
        fail("the recorded and reference sequences do not overlap enough to compare")
    diff, shift = alignment
    if diff > MAX_MEAN_STEERING_DIFF_RAD:
        fail(
            f"steering differs from the Autoware reference by {diff:.4f} rad on average "
            f"(limit {MAX_MEAN_STEERING_DIFF_RAD})"
        )

    latch = re.search(r"SI_STOP latched: ([^(\n]+?) \(fault_id (\d+)\)", log)
    if latch is None:
        fail("the controller did not latch SI_STOP after its inputs stopped")
    if latch.group(1) not in WATCHDOG_SOURCES:
        fail(f"SI_STOP latched on '{latch.group(1)}', expected a source watchdog")

    tail = commands[followed_indexes[-1] + 1:]
    if len(tail) < MIN_STOP_COMMANDS:
        fail(f"only {len(tail)} commands after the last followed one, need {MIN_STOP_COMMANDS}")
    velocities = [command[1] for command in tail]
    if any(later > earlier + 1e-6 for earlier, later in zip(velocities, velocities[1:])):
        fail("the speed target rose during the stop")
    if velocities[-1] < 0.0:
        fail("the stop commanded a negative speed")

    print(
        f"rosbag replay OK: {len(followed)} followed commands, steering within "
        f"{diff:.4f} rad of the Autoware reference (shift {shift}); SI_STOP latched on "
        f"{latch.group(1)}, then {len(tail)} stop commands"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
