#!/usr/bin/env bash
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Smoke test for the standalone rosidl message generator. Generates the
# `autoware_common_msgs` package (chosen because at autoware_msgs 1.3.0 it
# has only one self-contained .msg with no transitive dependencies) and
# checks that both the C header and the C++ header land in the expected
# locations.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "${OUT}"' EXIT

"${ROOT}/.venv-rosidl/bin/python" "${ROOT}/scripts/run_rosidl_generator.py" \
  --pkg autoware_common_msgs \
  --in-dir "${ROOT}/external/autoware_msgs/autoware_common_msgs" \
  --out-dir "${OUT}" \
  --rosidl-dir "${ROOT}/external/rosidl" \
  --lang both

echo "=== Generated files ==="
find "${OUT}/include" -type f | sort

test -f "${OUT}/include/autoware_common_msgs/msg/response_status.h" \
  || { echo "FAIL: expected .h not generated"; exit 1; }
test -f "${OUT}/include/autoware_common_msgs/msg/response_status.hpp" \
  || { echo "FAIL: expected .hpp not generated"; exit 1; }
echo "PASS"
