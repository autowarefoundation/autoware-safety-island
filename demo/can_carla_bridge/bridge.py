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


STOP_SPEED = 0.2


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
    if abs(command.velocity) <= STOP_SPEED and acceleration < 0.0:
        return {
            "steer": steer,
            "steer_speed": steer_speed,
            "speed": 0.0,
            "acceleration": min(acceleration, -cfg.brake_accel),
            "jerk": 0.0,
        }
    return {
        "steer": steer,
        "steer_speed": steer_speed,
        "speed": command.velocity,
        "acceleration": acceleration,
        "jerk": 0.0,
    }


def apply_ackermann(vehicle, carla, control):
    ack = carla.VehicleAckermannControl(**control)
    vehicle.apply_ackermann_control(ack)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interface", default="vcan0")
    parser.add_argument(
        "--can-format",
        choices=("classic", "fd"),
        default="classic",
        help="classic 0x100/0x101/0x102 batch or one CAN-FD 0x103 frame",
    )
    parser.add_argument("--timeout", type=float, default=0.5)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2000)
    ego = parser.add_mutually_exclusive_group()
    ego.add_argument("--ego-role", default="hero", help="exact CARLA vehicle role_name (default: hero)")
    ego.add_argument("--ego-id", type=int, help="select a CARLA vehicle by actor ID")
    parser.add_argument("--slew", type=float, default=0.3)
    parser.add_argument("--default-accel", type=float, default=1.0)
    parser.add_argument("--brake-accel", type=float, default=3.0)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def find_ego(world, role_name="hero", actor_id=None):
    if actor_id is not None:
        actor = world.get_actor(actor_id)
        if actor is None or not actor.type_id.startswith("vehicle."):
            raise RuntimeError(f"CARLA actor {actor_id} is not a vehicle or does not exist")
        return actor

    matches = [
        actor for actor in world.get_actors()
        if actor.type_id.startswith("vehicle.")
        and actor.attributes.get("role_name") == role_name
    ]
    if len(matches) != 1:
        raise RuntimeError(
            f"expected exactly one CARLA vehicle with role_name={role_name!r}, "
            f"found {len(matches)}; use --ego-id to select a specific vehicle"
        )
    return matches[0]


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
        vehicle = find_ego(world, args.ego_role, args.ego_id)

    decoder = ControlCommandDecoder()
    last_steer = 0.0
    bus_kwargs = {"channel": args.interface, "bustype": "socketcan"}
    if args.can_format == "fd":
        bus_kwargs["fd"] = True
    bus = can.Bus(**bus_kwargs)
    try:
        while True:
            now = time.monotonic()
            decoder.poll_watchdog(now, cfg.timeout_sec)
            msg = bus.recv(timeout=0.05)
            event = DecoderEvent.IGNORED
            if msg is not None and not (msg.is_error_frame or msg.is_remote_frame):
                if args.can_format == "fd":
                    if msg.is_fd:
                        event = decoder.feed_fd(
                            msg.arbitration_id,
                            bytes(msg.data),
                            msg.dlc,
                            time.monotonic(),
                        )
                elif not msg.is_fd:
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
            apply_ackermann(vehicle, carla, control)
    except KeyboardInterrupt:
        return 0
    finally:
        try:
            if vehicle is not None:
                apply_ackermann(vehicle, carla, map_ackermann(decoder.command, last_steer, cfg, True))
        finally:
            bus.shutdown()


if __name__ == "__main__":
    sys.exit(main())
