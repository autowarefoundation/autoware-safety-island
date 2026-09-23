#!/usr/bin/env python3
"""Exercise the bridge boundary without a running CARLA or CAN interface."""

import argparse
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

import bridge
from test_decoder import pack_cycle


def can_message(can_id, data, **flags):
    fields = dict(is_fd=False, is_error_frame=False, is_remote_frame=False)
    fields.update(flags)
    return SimpleNamespace(
        arbitration_id=can_id, data=data, dlc=len(data), is_extended_id=False, **fields
    )


class BridgeTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
