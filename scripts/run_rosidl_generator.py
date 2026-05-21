#!/usr/bin/env python3
# Copyright (c) 2026, Arm Limited.
# SPDX-License-Identifier: Apache-2.0
"""Standalone driver for rosidl_generator_c / rosidl_generator_cpp.

Invokes the Humble-pinned generators without an ament environment.
Handles the .msg -> .idl adaptation via rosidl_adapter, then runs the
chosen language generator(s) via their bin/ entry scripts.

The wrapping script (`scripts/install-rosidl-host.sh`) provides a venv
with the helper rosidl Python modules exposed via a .pth file, so the
`rosidl_adapter`, `rosidl_parser`, `rosidl_cmake` packages are importable.

Output layout (matches what MicroRosMessages.cmake expects):
  <out-dir>/
    idl/<pkg>/msg/*.idl            (adapter output)
    include/<pkg>/msg/*.h          (rosidl_generator_c output)
    include/<pkg>/msg/*.hpp        (rosidl_generator_cpp output)
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def run_adapter(pkg: str, msg_files: list[Path], src_dir: Path, out_dir: Path) -> list[str]:
    """Run rosidl_adapter to convert each .msg into a .idl file.

    Returns the list of idl tuples ("<basepath>:<relpath>") that the
    generators consume.
    """
    idl_out = out_dir / "idl"
    idl_out.mkdir(parents=True, exist_ok=True)

    # Adapter args file: {"non_idl_tuples": ["<base>:msg/Foo.msg", ...]}
    non_idl_tuples = [f"{src_dir}:msg/{m.name}" for m in msg_files]
    adapter_args = out_dir / "rosidl_adapter_args.json"
    adapter_args.write_text(json.dumps({"non_idl_tuples": non_idl_tuples}))

    adapter_output = out_dir / "adapted.idls"
    cmd = [
        sys.executable, "-m", "rosidl_adapter",
        "--package-name", pkg,
        "--arguments-file", str(adapter_args),
        "--output-dir", str(idl_out / pkg),
        "--output-file", str(adapter_output),
    ]
    subprocess.run(cmd, check=True)
    return [line.strip() for line in adapter_output.read_text().splitlines() if line.strip()]


def run_generator(lang: str, pkg: str, idl_tuples: list[str], out_dir: Path,
                  rosidl_dir: Path, deps: list[str]) -> None:
    """Invoke rosidl_generator_c or rosidl_generator_cpp via its bin script."""
    gen_pkg = {"c": "rosidl_generator_c", "cpp": "rosidl_generator_cpp"}[lang]
    gen_root = rosidl_dir / gen_pkg
    if not gen_root.exists():
        raise FileNotFoundError(f"{gen_root} not found — submodule init missing?")

    output_dir = out_dir / "include" / pkg
    output_dir.mkdir(parents=True, exist_ok=True)

    args = {
        "package_name": pkg,
        "output_dir": str(output_dir),
        "template_dir": str(gen_root / "resource"),
        "idl_tuples": idl_tuples,
        "ros_interface_dependencies": deps,
        "target_dependencies": [],
        "additional_files": [],
    }
    args_file = out_dir / f"{gen_pkg}_args.json"
    args_file.write_text(json.dumps(args))

    bin_script = gen_root / "bin" / gen_pkg
    cmd = [sys.executable, str(bin_script), "--generator-arguments-file", str(args_file)]
    subprocess.run(cmd, check=True)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--pkg", required=True)
    p.add_argument("--in-dir", required=True, type=Path,
                   help="Directory containing msg/*.msg")
    p.add_argument("--out-dir", required=True, type=Path)
    p.add_argument("--rosidl-dir", required=True, type=Path)
    p.add_argument("--lang", choices=["c", "cpp", "both"], default="both")
    p.add_argument("--deps", nargs="*", default=[])
    args = p.parse_args()

    in_dir: Path = args.in_dir.resolve()
    msg_dir: Path = in_dir / "msg"
    msgs = sorted(msg_dir.glob("*.msg"))
    if not msgs:
        print(f"No .msg files under {msg_dir}", file=sys.stderr)
        return 1

    out_dir: Path = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    rosidl_dir: Path = args.rosidl_dir.resolve()

    idl_tuples = run_adapter(args.pkg, msgs, in_dir, out_dir)
    langs = ["c", "cpp"] if args.lang == "both" else [args.lang]
    for lang in langs:
        run_generator(lang, args.pkg, idl_tuples, out_dir, rosidl_dir, args.deps)
    print(f"Generated {len(idl_tuples)} message(s) for {args.pkg} [{','.join(langs)}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
