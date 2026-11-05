#!/usr/bin/env python3
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
"""Check the demo's interim copy of autoware-main.yaml against its pinned image.

TODO(autoware_launch PR "feat(autoware_launch): expose
diagnostic_graph_aggregator_graph_path in planning_simulator"): delete this
script, .github/workflows/demo-diag-graph.yml and demo/launch/autoware-main.yaml
once the demo image's planning_simulator.launch.xml declares
diagnostic_graph_aggregator_graph_path.

demo/launch/autoware-main.yaml must be:
  - between its BEGIN/END markers, the demo image's
    autoware_launch/config/system/diagnostics/autoware-main.yaml, byte for byte;
  - after the END marker, exactly the edits of
    demo/launch/autoware-safety-island.yaml.
The image is the safety-island-autoware service image in
demo/docker-compose.yaml. Needs docker.

Usage: check-demo-diag-graph.py [--upstream FILE]
  --upstream FILE  compare against FILE instead of reading it from the image.
"""

import argparse
import subprocess
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
COMPOSE = ROOT / "demo/docker-compose.yaml"
COPY = ROOT / "demo/launch/autoware-main.yaml"
E2E_STYLE = ROOT / "demo/launch/autoware-safety-island.yaml"
UPSTREAM_PATH = "/opt/autoware/autoware_launch/share/autoware_launch/config/system/diagnostics/autoware-main.yaml"
BEGIN = "# ---- BEGIN verbatim copy: autoware-main.yaml from the demo image ----\n"
END = "# ---- END verbatim copy ----\n"


def demo_image():
    compose = yaml.safe_load(COMPOSE.read_text())
    return compose["services"]["safety-island-autoware"]["image"]


def upstream_from_image(image):
    result = subprocess.run(
        ["docker", "run", "--rm", "--entrypoint", "cat", image, UPSTREAM_PATH],
        check=True, capture_output=True, text=True)
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", type=Path)
    args = parser.parse_args()

    image = demo_image()
    upstream = args.upstream.read_text() if args.upstream else upstream_from_image(image)
    text = COPY.read_text()
    errors = []

    if text.count(BEGIN) != 1 or text.count(END) != 1 or text.index(BEGIN) > text.index(END):
        print(f"{COPY.name}: missing or misplaced BEGIN/END markers", file=sys.stderr)
        return 1
    verbatim = text[text.index(BEGIN) + len(BEGIN):text.index(END)]
    if verbatim != upstream:
        errors.append(f"the text between the markers differs from {UPSTREAM_PATH} in {image}; "
                      "copy the image's file verbatim")

    copy_edits = yaml.safe_load(text).get("edits")
    e2e_edits = yaml.safe_load(E2E_STYLE.read_text()).get("edits")
    if copy_edits != e2e_edits:
        errors.append(f"edits {copy_edits} differ from {E2E_STYLE.name}'s {e2e_edits}")
    if "edits" in yaml.safe_load(upstream):
        errors.append(f"{UPSTREAM_PATH} in {image} now has its own edits; merge them by hand")

    if errors:
        for e in errors:
            print(f"{COPY.name}: {e}", file=sys.stderr)
        return 1
    print(f"{COPY.name} OK: verbatim {UPSTREAM_PATH} from {image} plus the edits of {E2E_STYLE.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
