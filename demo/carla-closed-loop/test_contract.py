#!/usr/bin/env python3
"""Privilege-free closed-loop contract checks. No CARLA, no Open AD Kit."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OVERLAY = ROOT / "overlay"
BRIDGE = ROOT.parent / "can_carla_bridge"

sys.path.insert(0, str(OVERLAY))
from patch_sensors_only import APPLY, patch_sensors_only  # noqa: E402

EXPECTED_INPUTS = {
    "vehicle/status/steering_status": "autoware_vehicle_msgs/msg/SteeringReport",
    "planning/scenario_planning/trajectory": "autoware_planning_msgs/msg/Trajectory",
    "system/operation_mode/state": "autoware_adapi_v1_msgs/msg/OperationModeState",
    "localization/kinematic_state": "nav_msgs/msg/Odometry",
    "localization/acceleration": "geometry_msgs/msg/AccelWithCovarianceStamped",
}


def test_overlay_skips_apply() -> None:
    sample = (
        "            try:\n"
        "                ego_action = self.sensor()\n"
        "            except SensorReceivedNoData as e:\n"
        "                raise RuntimeError(e)\n"
        "            self.ego_actor.apply_control(ego_action)\n"
        "        if self.running:\n"
        "            CarlaDataProvider.get_world().tick()\n"
    )
    patched = patch_sensors_only(sample)
    assert APPLY not in patched
    assert "pass" in patched
    assert "CarlaDataProvider.get_world().tick()" in patched
    assert patch_sensors_only(patched) == patched


def test_bridge_config_inputs() -> None:
    text = (ROOT / "bridge-config.yaml").read_text()
    assert "control/trajectory_follower/control_cmd" not in text
    for topic, msg_type in EXPECTED_INPUTS.items():
        assert f"{topic}:" in text
        assert msg_type in text
        idx = text.index(topic)
        chunk = text[idx : idx + 200]
        assert "from_domain: 1" in chunk
        assert "to_domain: 2" in chunk


def test_topics_matrix() -> None:
    text = (ROOT / "topics.yaml").read_text()
    assert "outputs: []" in text
    for topic, msg_type in EXPECTED_INPUTS.items():
        assert f"/{topic}" in text
        assert msg_type in text


def test_pins_carla_digest() -> None:
    pins = (ROOT / "pins.env").read_text()
    carla = [
        line
        for line in pins.splitlines()
        if line.startswith("CARLA_CONTAINER_IMAGE=")
    ]
    assert len(carla) == 1
    assert "@sha256:" in carla[0]
    assert "OPENADKIT_DEPLOYMENT=deployments/safety-island-carla-simulation" in pins


def test_overlay_files_exist() -> None:
    assert (OVERLAY / "patch_sensors_only.py").is_file()
    assert (OVERLAY / "carla-interface-entrypoint.sh").is_file()
    assert (BRIDGE / "bridge.py").is_file()


def main() -> int:
    test_overlay_skips_apply()
    test_bridge_config_inputs()
    test_topics_matrix()
    test_pins_carla_digest()
    test_overlay_files_exist()
    print("closed-loop contract passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
