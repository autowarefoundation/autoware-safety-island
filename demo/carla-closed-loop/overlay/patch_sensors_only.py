#!/usr/bin/env python3
"""Skip ego.apply_control() so the CAN-CARLA bridge is the sole driver."""

from __future__ import annotations

import sys
from pathlib import Path

APPLY = "self.ego_actor.apply_control(ego_action)"


def patch_sensors_only(text: str) -> str:
    if APPLY not in text:
        return text
    lines = []
    replaced = False
    for line in text.splitlines(keepends=True):
        if not replaced and APPLY in line and not line.lstrip().startswith("#"):
            indent = line[: len(line) - len(line.lstrip())]
            newline = "\n" if line.endswith("\n") else ""
            lines.append(f"{indent}pass{newline}")
            replaced = True
        else:
            lines.append(line)
    return "".join(lines)


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/opt/autoware")
    matches = sorted(root.glob("**/autoware_carla_interface/carla_autoware.py"))
    if len(matches) != 1:
        print(
            f"expected one carla_autoware.py under {root}, found {len(matches)}",
            file=sys.stderr,
        )
        return 1
    path = matches[0]
    original = path.read_text()
    updated = patch_sensors_only(original)
    if APPLY not in original:
        print(f"already sensors-only: {path}")
        return 0
    if updated == original:
        print("apply_control not patched", file=sys.stderr)
        return 1
    path.write_text(updated)
    print(f"sensors-only: patched {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
