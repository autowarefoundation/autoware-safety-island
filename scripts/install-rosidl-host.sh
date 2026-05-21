#!/usr/bin/env bash
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Install host-side Python tools needed to run rosidl_generator_c and
# rosidl_generator_cpp standalone (without a ROS 2 environment).
#
# Usage: ./scripts/install-rosidl-host.sh
#
# Idempotent. Installs into a project-local venv at .venv-rosidl/.
#
# Design note: on the Humble branch of ros2/rosidl that we pin via
# external/rosidl, the helper Python packages (`rosidl_adapter`,
# `rosidl_parser`, `rosidl_cmake`) ship without pip-installable
# `pyproject.toml`/`setup.py`. The ament build would normally install them;
# here we expose their inner modules via a .pth file inside the venv.
# `rosidl_pycommon` did not exist yet on Humble (it's a later rename of
# `rosidl_cmake`'s helpers).

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
VENV="${ROOT_DIR}/.venv-rosidl"
ROSIDL_DIR="${ROOT_DIR}/external/rosidl"

# Ensure the rosidl submodule sources are present.
if [ ! -f "${ROSIDL_DIR}/rosidl_adapter/setup.cfg" ]; then
  echo "Initializing external/rosidl submodule..."
  git -C "${ROOT_DIR}" submodule update --init external/rosidl
fi

if [ ! -d "${VENV}" ]; then
  python3 -m venv "${VENV}"
fi
# shellcheck disable=SC1091
source "${VENV}/bin/activate"

pip install --upgrade pip
# External support libraries:
# - empy 3.x is the templating engine the generators require
# - lark-parser is the IDL grammar parser
# - catkin_pkg parses package.xml in rosidl_adapter
pip install \
  'empy<4' \
  lark-parser \
  catkin_pkg

# Expose the helper rosidl Python modules to the venv via a .pth file.
# Each path is a directory that contains the importable package directory
# (e.g. `${ROSIDL_DIR}/rosidl_adapter` holds `rosidl_adapter/__init__.py`).
PY_VER=$("${VENV}/bin/python" -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
PTH="${VENV}/lib/python${PY_VER}/site-packages/rosidl_humble.pth"
{
  echo "${ROSIDL_DIR}/rosidl_adapter"
  echo "${ROSIDL_DIR}/rosidl_parser"
  echo "${ROSIDL_DIR}/rosidl_cmake"
} > "${PTH}"

echo "Host rosidl tooling installed at ${VENV}"
