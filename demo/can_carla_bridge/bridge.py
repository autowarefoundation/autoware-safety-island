#!/usr/bin/env python3
"""SocketCAN RX → decode placeholder frames → CARLA VehicleAckermannControl."""

from __future__ import annotations

import argparse
import os
import sys
import time
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from decoder import ControlCommandDecoder, DecodedControlCommand, DecoderEvent


@dataclass
class BridgeConfig:
    slew: float
    default_accel: float
    brake_accel: float
    timeout_sec: float


def map_ackermann(
    command: DecodedControlCommand,
    last_steer: float,
    cfg: BridgeConfig,
    safe_stop: bool,
):
    if safe_stop:
        return {
            "steer": last_steer,
            "steer_speed": cfg.slew,
            "speed": 0.0,
            "acceleration": cfg.brake_accel,
            "jerk": 0.0,
        }
    steer = -command.steering_tire_angle
    steer_speed = (
        abs(command.steering_tire_rotation_rate) if command.steering_rate_defined else cfg.slew
    )
    acceleration = command.acceleration if command.acceleration_defined else cfg.default_accel
    return {
        "steer": steer,
        "steer_speed": steer_speed,
        "speed": command.velocity,
        "acceleration": acceleration,
        "jerk": 0.0,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", default="vcan0")
    parser.add_argument("--timeout", type=float, default=0.5)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2000)
    parser.add_argument("--slew", type=float, default=0.3)
    parser.add_argument("--default-accel", type=float, default=1.0)
    parser.add_argument("--brake-accel", type=float, default=3.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--role", default="ego_vehicle")
    return parser.parse_args()


def find_ego(world, role: str = "ego_vehicle"):
    vehicles = [
        actor for actor in world.get_actors() if "vehicle" in getattr(actor, "type_id", "")
    ]
    for wanted in (role, "ego_vehicle", "hero"):
        for actor in vehicles:
            if actor.attributes.get("role_name") == wanted:
                return actor
    if not vehicles:
        raise RuntimeError("no CARLA vehicle found")
    return vehicles[0]


def main() -> int:
    args = parse_args()
    cfg = BridgeConfig(
        slew=args.slew,
        default_accel=args.default_accel,
        brake_accel=args.brake_accel,
        timeout_sec=args.timeout,
    )

    try:
        import can
    except ImportError:
        print("python-can is required", file=sys.stderr)
        return 1

    vehicle = None
    carla = None
    if not args.dry_run:
        try:
            import carla as carla_mod
        except ImportError:
            print("CARLA Python API is required unless --dry-run", file=sys.stderr)
            return 1
        carla = carla_mod
        client = carla.Client(args.host, args.port)
        client.set_timeout(10.0)
        world = client.get_world()
        world.wait_for_tick(seconds=10.0)
        vehicle = find_ego(world, args.role)

    decoder = ControlCommandDecoder()
    last_steer = 0.0
    bus = can.Bus(channel=args.interface, bustype="socketcan")
    try:
        while True:
            now = time.monotonic()
            decoder.poll_watchdog(now, cfg.timeout_sec)
            msg = bus.recv(timeout=0.05)
            event = DecoderEvent.IGNORED
            if msg is not None:
                event = decoder.feed(
                    msg.arbitration_id,
                    bytes(msg.data),
                    msg.dlc,
                    msg.is_extended_id,
                    time.monotonic(),
                )
            if decoder.in_safe_stop:
                control = map_ackermann(decoder.command, last_steer, cfg, True)
                event = DecoderEvent.SAFE_STOP
            elif event == DecoderEvent.ACCEPTED:
                last_steer = -decoder.command.steering_tire_angle
                control = map_ackermann(decoder.command, last_steer, cfg, False)
            else:
                continue
            if args.dry_run:
                print(event.name, control)
                continue
            ack = carla.VehicleAckermannControl(
                steer=control["steer"],
                steer_speed=control["steer_speed"],
                speed=control["speed"],
                acceleration=control["acceleration"],
                jerk=control["jerk"],
            )
            vehicle.apply_ackermann_control(ack)
    except KeyboardInterrupt:
        return 0
    finally:
        bus.shutdown()


if __name__ == "__main__":
    sys.exit(main())
