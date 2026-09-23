#!/usr/bin/env python3
"""Exercise the bridge boundary without a running CARLA or CAN interface."""

import argparse
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

import bridge
from decoder import DecodedControlCommand
from test_decoder import pack_cycle


def can_message(can_id, data, **flags):
    fields = dict(is_fd=False, is_error_frame=False, is_remote_frame=False)
    fields.update(flags)
    return SimpleNamespace(
        arbitration_id=can_id, data=data, dlc=len(data), is_extended_id=False, **fields
    )


class BridgeTest(unittest.TestCase):
    def test_actor_selection_arguments(self):
        for options, role, actor_id in (
            ([], "hero", None),
            (["--ego-role", "ego_vehicle"], "ego_vehicle", None),
            (["--ego-id", "42"], "hero", 42),
        ):
            with self.subTest(options=options), patch.object(sys, "argv", ["bridge.py", *options]):
                args = bridge.parse_args()
                self.assertEqual((args.ego_role, args.ego_id), (role, actor_id))

    def run_bridge(self, messages):
        bus = Mock()
        bus.recv.side_effect = [*messages, KeyboardInterrupt()]
        vehicle = Mock()
        can_module = SimpleNamespace(Bus=Mock(return_value=bus))
        carla_module = SimpleNamespace(
            Client=Mock(), VehicleAckermannControl=Mock(side_effect=lambda **fields: fields)
        )
        args = argparse.Namespace(
            slew=0.3, default_accel=1.0, brake_accel=3.0, timeout=0.5,
            host="127.0.0.1", port=2000, interface="vcan0", dry_run=False,
            ego_role="hero", ego_id=None,
        )
        with patch.dict(sys.modules, {"can": can_module, "carla": carla_module}), \
                patch.object(bridge, "parse_args", return_value=args), \
                patch.object(bridge, "find_ego", return_value=vehicle):
            self.assertEqual(bridge.main(), 0)
        bus.shutdown.assert_called_once()
        return [call.args[0] for call in vehicle.apply_ackermann_control.call_args_list]

    def test_interrupt_brakes_with_last_steer(self):
        controls = self.run_bridge([can_message(*frame) for frame in pack_cycle(7)])
        self.assertEqual(len(controls), 2)
        self.assertEqual(controls[0]["speed"], 12.25)
        self.assertEqual(controls[0]["acceleration"], -1.5)
        self.assertEqual(controls[0]["steer"], -0.125)
        self.assertEqual(controls[1]["speed"], 0.0)
        self.assertEqual(controls[1]["steer"], -0.125)
        self.assertEqual(controls[1]["acceleration"], 3.0)

    def test_non_classic_frames_do_not_complete_a_command(self):
        lateral, longitudinal, status = pack_cycle(0)
        for flag in ("is_fd", "is_error_frame", "is_remote_frame"):
            with self.subTest(flag=flag):
                controls = self.run_bridge([
                    can_message(*lateral, **{flag: True}),
                    can_message(*longitudinal),
                    can_message(*status),
                ])
                self.assertEqual(len(controls), 1)
                self.assertEqual(controls[0]["speed"], 0.0)

    def test_ego_selection_requires_one_exact_role(self):
        hero = SimpleNamespace(id=10, type_id="vehicle.tesla.model3", attributes={"role_name": "hero"})
        other = SimpleNamespace(id=11, type_id="vehicle.audi.a2", attributes={"role_name": "npc"})
        ego = SimpleNamespace(id=12, type_id="vehicle.toyota.prius", attributes={"role_name": "ego_vehicle"})
        world = Mock()
        world.get_actors.return_value = [other, hero, ego]
        self.assertIs(bridge.find_ego(world), hero)
        self.assertIs(bridge.find_ego(world, "npc"), other)
        self.assertIs(bridge.find_ego(world, "ego_vehicle"), ego)
        with self.assertRaisesRegex(RuntimeError, "found 0"):
            bridge.find_ego(world, "missing")
        world.get_actors.return_value = [other, hero]
        with self.assertRaisesRegex(RuntimeError, "found 0"):
            bridge.find_ego(world, "ego_vehicle")
        world.get_actors.return_value = [hero, hero]
        with self.assertRaisesRegex(RuntimeError, "found 2"):
            bridge.find_ego(world)

    def test_ego_id_requires_an_existing_vehicle(self):
        vehicle = SimpleNamespace(id=42, type_id="vehicle.audi.a2")
        world = Mock()
        world.get_actor.return_value = vehicle
        self.assertIs(bridge.find_ego(world, actor_id=42), vehicle)
        world.get_actor.return_value = None
        with self.assertRaisesRegex(RuntimeError, "does not exist"):
            bridge.find_ego(world, actor_id=42)
        world.get_actor.return_value = SimpleNamespace(type_id="walker.pedestrian.0001")
        with self.assertRaisesRegex(RuntimeError, "not a vehicle"):
            bridge.find_ego(world, actor_id=42)

    def test_goal_stop_preserves_acceleration_and_brakes_on_low_speed_deceleration(self):
        cfg = bridge.BridgeConfig(slew=0.3, default_accel=1.0, brake_accel=3.0, timeout_sec=0.5)
        for velocity, acceleration in ((4.0, 0.5), (2.0, -1.5), (0.1, 0.4)):
            with self.subTest(velocity=velocity):
                command = DecodedControlCommand(
                    velocity=velocity, acceleration=acceleration, acceleration_defined=True
                )
                control = bridge.map_ackermann(command, 0.0, cfg, False)
                self.assertEqual(control["speed"], velocity)
                self.assertEqual(control["acceleration"], acceleration)

        stopped = DecodedControlCommand(
            velocity=0.0, acceleration=-1.5, acceleration_defined=True
        )
        control = bridge.map_ackermann(stopped, 0.0, cfg, False)
        self.assertEqual(control["speed"], 0.0)
        self.assertEqual(control["acceleration"], -3.0)


if __name__ == "__main__":
    unittest.main()
