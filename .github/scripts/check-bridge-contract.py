#!/usr/bin/env python3
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
"""Check the Safety Island bridge contract.

The contract is demo/bridge/bridge-config.yaml (topics) plus
demo/bridge/cyclonedds-domain2.xml (the DDS setting every domain-2 writer
must use). Both are published as release assets.

bridge-config.yaml is published as a release asset and pinned by downstream
integrations (Open AD Kit) as the Safety Island's topic contract, so the
documented contract must not drift from it. For every topic the bridge
forwards, topics.rst must list the same message type and bridge direction;
every row topics.rst marks as carried by the demo bridge must be in the
config. Every topic must keep its name on both domains: no ``remap``.

cyclonedds-domain2.xml must hold only <Domain id="2"><General>
<MaxMessageSize>, with no network interface, so it merges with an
integrator's own configuration, and MaxMessageSize must fit the Safety
Island's receive buffer (cfg.rmsg_chunk_size in config.hpp).

Usage: check-bridge-contract.py [bridge-config.yaml] [topics.rst] [cyclonedds-domain2.xml]
"""

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[2]
CDDS_NS = "{https://cdds.io/config}"
DDS_CONFIG_HPP = ROOT / "actuation_module/include/common/dds/config.hpp"
# CycloneDDS memory-size units: kB is 2^10, as KiB.
UNITS = {"B": 1, "kB": 1 << 10, "KiB": 1 << 10, "MB": 1 << 20, "MiB": 1 << 20}


def si_receive_limit():
    """The largest datagram the Safety Island can receive, in bytes."""
    m = re.search(r"cfg\.rmsg_chunk_size\s*=\s*(\d+)\s*(?:\*\s*(\d+))?\s*;", DDS_CONFIG_HPP.read_text())
    if not m:
        sys.exit(f"cannot find cfg.rmsg_chunk_size in {DDS_CONFIG_HPP}")
    return int(m.group(1)) * int(m.group(2) or 1)


def check_domain2_xml(path, limit):
    errors = []
    root = ET.parse(path).getroot()
    if root.tag != CDDS_NS + "CycloneDDS":
        return [f"{path.name}: root element is {root.tag}, expected CycloneDDS in {CDDS_NS}"]
    domains = list(root)
    if [(d.tag, d.get("id")) for d in domains] != [(CDDS_NS + "Domain", "2")]:
        errors.append(f"{path.name}: must contain exactly one <Domain id=\"2\"> and nothing else")
    for domain in domains:
        children = [c.tag for c in domain]
        if children != [CDDS_NS + "General"]:
            errors.append(f"{path.name}: <Domain> may hold only <General>, found {children}")
        for general in domain.findall(CDDS_NS + "General"):
            settings = [c.tag for c in general]
            if settings != [CDDS_NS + "MaxMessageSize"]:
                errors.append(f"{path.name}: <General> may hold only <MaxMessageSize> "
                              f"(no interface, so it merges with the integrator's config), found {settings}")
            size = general.findtext(CDDS_NS + "MaxMessageSize", "").strip()
            m = re.fullmatch(r"(\d+)\s*(B|kB|KiB|MB|MiB)", size)
            if not m:
                errors.append(f"{path.name}: MaxMessageSize '{size}' is not a size with a unit")
            elif int(m.group(1)) * UNITS[m.group(2)] > limit:
                errors.append(f"{path.name}: MaxMessageSize {size} exceeds the Safety Island receive limit of {limit} B")
    return errors


def load_bridge(path):
    cfg = yaml.safe_load(path.read_text())
    topics = {}
    remapped = []
    for name, entry in (cfg.get("topics") or {}).items():
        topic = "/" + name.lstrip("/")
        topics[topic] = (
            entry["type"],
            int(entry["from_domain"]),
            int(entry["to_domain"]),
        )
        if "remap" in entry:
            remapped.append(f"{topic}: remapped to '{entry['remap']}'; the contract bridges every topic 1:1")
    return topics, remapped


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
    xml_path = Path(sys.argv[3]) if len(sys.argv) > 3 else ROOT / "demo/bridge/cyclonedds-domain2.xml"

    bridge, errors = load_bridge(bridge_path)
    rows = load_rst_rows(rst_path)
    limit = si_receive_limit()
    errors += check_domain2_xml(xml_path, limit)

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
    print(f"bridge contract OK: {len(bridge)} topics match {rst_path.name}; "
          f"{xml_path.name} caps domain 2 within the {limit} B receive limit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
