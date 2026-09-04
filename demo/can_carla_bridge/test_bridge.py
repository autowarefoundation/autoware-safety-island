#!/usr/bin/env python3
"""find_ego role matching without the CARLA Python API."""

from __future__ import annotations

import sys
from types import SimpleNamespace

from bridge import BridgeConfig, find_ego, map_ackermann
from decoder import DecodedControlCommand


class FakeWorld:
    def __init__(self, actors):
        self._actors = actors

    def get_actors(self):
        return self._actors


def vehicle(role, type_id="vehicle.toyota.prius"):
    return SimpleNamespace(type_id=type_id, attributes={"role_name": role})


def main() -> int:
    ego = vehicle("ego_vehicle")
    hero = vehicle("hero")
    npc = vehicle("autopilot")
    world = FakeWorld([npc, hero, ego])
    assert find_ego(world, "ego_vehicle") is ego
    assert find_ego(world, "hero") is hero

    hero_only = FakeWorld([npc, hero])
    assert find_ego(hero_only, "ego_vehicle") is hero

    empty = FakeWorld([SimpleNamespace(type_id="sensor.lidar", attributes={})])
    try:
        find_ego(empty, "ego_vehicle")
    except RuntimeError:
        pass
    else:
        raise AssertionError("expected no-vehicle error")

    cfg = BridgeConfig(slew=0.3, default_accel=1.0, brake_accel=3.0, timeout_sec=0.5)
    drive = DecodedControlCommand(velocity=4.0, acceleration=0.5, acceleration_defined=True)
    mapped = map_ackermann(drive, 0.0, cfg, False)
    assert mapped["speed"] == 4.0
    creep = DecodedControlCommand(velocity=0.1, acceleration=0.4, acceleration_defined=True)
    mapped = map_ackermann(creep, 0.0, cfg, False)
    assert mapped["speed"] == 0.0
    assert mapped["acceleration"] <= -3.0
    brake = DecodedControlCommand(velocity=2.0, acceleration=-1.5, acceleration_defined=True)
    mapped = map_ackermann(brake, 0.0, cfg, False)
    assert mapped["speed"] == 2.0
    assert mapped["acceleration"] == -1.5

    print("bridge role lookup passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
