#!/usr/bin/env bash
# Start domain_bridge with the Safety Island topic contract
# (/autoware/bridge-config.yaml). When /autoware/bridge-overlay.yaml is
# mounted, its topics are merged in first; an overlay may add topics but not
# replace a contract topic.
set -eo pipefail

source "/opt/ros/$ROS_DISTRO/setup.bash"
source "/opt/autoware/setup.bash"

config=/autoware/bridge-config.yaml
overlay=/autoware/bridge-overlay.yaml

if [ -f "${overlay}" ]; then
  merged=/tmp/bridge-config.merged.yaml
  python3 - "${config}" "${overlay}" "${merged}" <<'PY'
import sys
import yaml

config_path, overlay_path, merged_path = sys.argv[1:4]
with open(config_path) as f:
    config = yaml.safe_load(f)
with open(overlay_path) as f:
    overlay = yaml.safe_load(f) or {}
topics = config.setdefault("topics", {})
for name, entry in (overlay.get("topics") or {}).items():
    if name in topics:
        sys.exit(f"bridge overlay: '{name}' is already in the contract; overlays only add topics")
    topics[name] = entry
    print(f"bridge overlay: + {name}" + (f" -> {entry['remap']}" if "remap" in entry else ""))
with open(merged_path, "w") as f:
    yaml.safe_dump(config, f, sort_keys=False)
PY
  config="${merged}"
fi

exec ros2 run domain_bridge domain_bridge --wait-for-publisher false "${config}"
