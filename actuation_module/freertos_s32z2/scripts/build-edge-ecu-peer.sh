#!/usr/bin/env bash
# Build the host-side DDS peer used by the B-2 verification.
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "${REPO_ROOT}"

rm -rf build-edge-ecu-peer
cmake -S actuation_module/freertos_s32z2/edge_ecu_peer -B build-edge-ecu-peer
cmake --build build-edge-ecu-peer -j

test -x build-edge-ecu-peer/edge_ecu_pub
test -x build-edge-ecu-peer/edge_ecu_sub
echo "Edge ECU peer built: build-edge-ecu-peer/edge_ecu_{pub,sub}"
