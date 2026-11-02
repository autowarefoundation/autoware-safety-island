#!/usr/bin/env python3
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
"""Check that documentation/design/topics.rst matches demo/bridge/bridge-config.yaml.

bridge-config.yaml is published as a release asset and pinned by downstream
integrations (Open AD Kit) as the Safety Island's topic contract, so the
documented contract must not drift from it. For every topic the bridge
forwards, topics.rst must list the same message type and bridge direction;
every row topics.rst marks as carried by the demo bridge must be in the
config.

Usage: check-bridge-contract.py [bridge-config.yaml] [topics.rst]
"""

import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]


def load_bridge(path):
    cfg = yaml.safe_load(path.read_text())
    topics = {}
    for name, entry in (cfg.get("topics") or {}).items():
        topics["/" + name.lstrip("/")] = (
            entry["type"],
            int(entry["from_domain"]),
            int(entry["to_domain"]),
        )
    return topics


def load_rst_rows(path):
    """Return {topic: (type, direction cell)} from the rst list-tables."""
    rows = {}
    cells = []

    def flush():
        if len(cells) >= 3 and cells[0].startswith("``/"):
            topic = cells[0].strip("`")
            rows[topic] = (cells[1].strip("`"), cells[2])

    for line in path.read_text().splitlines():
        m = re.match(r"^\s*\* - (.*)$", line)
        if m:
            flush()
            cells = [m.group(1).strip()]
            continue
        m = re.match(r"^\s+- (.*)$", line)
        if m and cells:
            cells.append(m.group(1).strip())
            continue
        if not line.strip() or not line.startswith(" "):
            flush()
            cells = []
    flush()
    return rows


def main():
    bridge_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "demo/bridge/bridge-config.yaml"
    rst_path = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "documentation/design/topics.rst"

    bridge = load_bridge(bridge_path)
    rows = load_rst_rows(rst_path)
    errors = []

    for topic, (msg_type, src, dst) in sorted(bridge.items()):
        if topic not in rows:
            errors.append(f"{topic}: bridged in {bridge_path.name} but missing from {rst_path.name}")
            continue
        doc_type, direction = rows[topic]
        if doc_type != msg_type:
            errors.append(f"{topic}: type {msg_type} in {bridge_path.name}, {doc_type} in {rst_path.name}")
        if f"{src} → {dst}" not in direction:
            errors.append(f"{topic}: bridged {src} → {dst}, documented as '{direction}'")

    for topic, (_, direction) in sorted(rows.items()):
        documented_as_bridged = "(demo bridge)" in direction or re.search(r"\b2 → 1\b", direction)
        if documented_as_bridged and topic not in bridge:
            errors.append(f"{topic}: documented as bridged ('{direction}') but not in {bridge_path.name}")

    if errors:
        for e in errors:
            print(f"bridge contract mismatch: {e}", file=sys.stderr)
        return 1
    print(f"bridge contract OK: {len(bridge)} topics match {rst_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
