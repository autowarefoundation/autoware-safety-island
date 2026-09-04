#!/usr/bin/env python3
"""find_ego role matching without the CARLA Python API."""

from __future__ import annotations

import sys
from types import SimpleNamespace

from bridge import find_ego


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

    print("bridge role lookup passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
