#!/usr/bin/env python3
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
"""Record the Safety Island's control_cmd topic to a CSV file.

One row per received command: receive time (monotonic seconds), steering tire
angle, velocity, acceleration. Runs until it is sent SIGINT or SIGTERM.

Usage: record-control-cmd.py OUTPUT.csv
"""

import csv
import signal
import sys
import time

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy

from autoware_control_msgs.msg import Control

TOPIC = "/control/trajectory_follower/control_cmd"


def main() -> int:
    rclpy.init()
    node = Node("control_cmd_recorder")
    with open(sys.argv[1], "w", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(["received_s", "steering_tire_angle", "velocity", "acceleration"])

        def on_command(msg: Control) -> None:
            writer.writerow(
                [
                    f"{time.monotonic():.6f}",
                    repr(msg.lateral.steering_tire_angle),
                    repr(msg.longitudinal.velocity),
                    repr(msg.longitudinal.acceleration),
                ]
            )
            out.flush()

        node.create_subscription(
            Control, TOPIC, on_command, QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE)
        )
        signal.signal(signal.SIGTERM, lambda *_: rclpy.try_shutdown())
        try:
            rclpy.spin(node)
        except (KeyboardInterrupt, ExternalShutdownException):
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
