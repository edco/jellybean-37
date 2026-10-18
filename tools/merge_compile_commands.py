#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""Merge native and MCU compilation databases for language servers (clangd)."""

import json
import sys
from pathlib import Path


def merge_compile_commands(native_path: str, mcu_path: str, out_path: str) -> None:
    native_file = Path(native_path)
    mcu_file = Path(mcu_path)
    out_file = Path(out_path)

    commands = []
    seen = set()

    # 1. Native commands first (app, hal, sim, external) - standard host compiler flags
    if native_file.exists():
        with open(native_file, "r") as f:
            try:
                native_cmds = json.load(f)
                for entry in native_cmds:
                    commands.append(entry)
                    seen.add(entry.get("file"))
            except Exception as e:
                print(f"Warning: Failed to load {native_file}: {e}", file=sys.stderr)

    # 2. MCU commands for files not present in native (mcu/*, vendor drivers)
    if mcu_file.exists():
        with open(mcu_file, "r") as f:
            try:
                mcu_cmds = json.load(f)
                for entry in mcu_cmds:
                    file_path = entry.get("file")
                    if file_path not in seen:
                        commands.append(entry)
                        seen.add(file_path)
            except Exception as e:
                print(f"Warning: Failed to load {mcu_file}: {e}", file=sys.stderr)

    with open(out_file, "w") as f:
        json.dump(commands, f, indent=2)


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: merge_compile_commands.py <native_cdb> <mcu_cdb> <out_cdb>")
        sys.exit(1)
    merge_compile_commands(sys.argv[1], sys.argv[2], sys.argv[3])
