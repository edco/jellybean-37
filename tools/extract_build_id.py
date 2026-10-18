#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""
Extract GNU Build-ID from linked ELF binary and generate build_stamp.txt.
Calculates the 24-bit diagnostic build stamp and 8-character color codes
for Row 0 of the ST7789 LCD display.
"""

import sys
import os
import re
import shutil
import argparse
import subprocess

COLOR_NAMES_3BIT = [
    "K",  # 0 (000): Black
    "B",  # 1 (001): Blue
    "G",  # 2 (010): Green
    "C",  # 3 (011): Cyan
    "R",  # 4 (100): Red
    "M",  # 5 (101): Magenta
    "Y",  # 6 (110): Yellow
    "W",  # 7 (111): White
]

def find_readelf(candidate=None):
    if candidate and os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    for name in ["llvm-readelf", "readelf", "arm-none-eabi-readelf"]:
        path = shutil.which(name)
        if path:
            return path
    return None

def extract_build_id_readelf(readelf_bin, elf_path):
    # Try -n then --notes
    for flag in ["-n", "--notes"]:
        try:
            res = subprocess.run(
                [readelf_bin, flag, elf_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True
            )
            match = re.search(r"Build ID:\s*([0-9a-fA-F]+)", res.stdout)
            if match:
                return match.group(1).lower()
        except Exception:
            continue
    return None

def extract_build_id_fallback(elf_path):
    """Fallback parser reading .note.gnu.build-id directly from 32-bit ELF if readelf fails."""
    try:
        with open(elf_path, "rb") as f:
            data = f.read()
        # Search for "GNU\0" note descriptor
        idx = 0
        while True:
            idx = data.find(b"GNU\x00", idx)
            if idx == -1:
                break
            # Check 12 bytes preceding "GNU\0": namesz(4), descsz(4), type(4)
            if idx >= 12:
                namesz = int.from_bytes(data[idx-12:idx-8], "little")
                descsz = int.from_bytes(data[idx-8:idx-4], "little")
                ntype  = int.from_bytes(data[idx-4:idx], "little")
                if namesz == 4 and ntype == 3 and descsz in (16, 20):
                    desc = data[idx+4 : idx+4+descsz]
                    return desc.hex().lower()
            idx += 4
    except Exception:
        pass
    return None

def main():
    parser = argparse.ArgumentParser(description="Extract GNU Build-ID from linked ELF")
    parser.add_argument("--elf", required=True, help="Path to ELF binary")
    parser.add_argument("--output", required=True, help="Path to output build stamp file")
    parser.add_argument("--readelf", default=None, help="Path to readelf executable")
    args = parser.parse_args()

    readelf_bin = find_readelf(args.readelf)
    build_id = None
    if readelf_bin:
        build_id = extract_build_id_readelf(readelf_bin, args.elf)

    if not build_id:
        build_id = extract_build_id_fallback(args.elf)

    if not build_id:
        sys.stderr.write(f"Error: Could not extract GNU build ID from {args.elf}\n")
        sys.exit(1)

    # First 24 bits (first 3 bytes)
    raw_bytes = bytes.fromhex(build_id[:6])
    stamp24 = (raw_bytes[0] << 16) | (raw_bytes[1] << 8) | raw_bytes[2]

    # Extract 8 3-bit color chunks (MSB to LSB)
    color_chars = []
    for i in range(7, -1, -1):
        chunk = (stamp24 >> (i * 3)) & 0x7
        color_chars.append(COLOR_NAMES_3BIT[chunk])

    color_code_8char = "".join(color_chars)
    color_code_spaced = " ".join(color_chars)

    out_content = (
        f"BUILD_ID={build_id}\n"
        f"BUILD_STAMP_24BIT=0x{stamp24:06X}\n"
        f"BUILD_STAMP_COLORS={color_code_8char}\n"
        f"BUILD_STAMP_COLORS_SPACED={color_code_spaced}\n"
    )

    out_dir = os.path.dirname(os.path.abspath(args.output))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    with open(args.output, "w") as f:
        f.write(out_content)

    print(f"=== Build Stamp: 0x{stamp24:06X} -> Row 0 Colors: [{color_code_spaced}] ({color_code_8char}) ===")
    print(f"    Build ID: {build_id}")

if __name__ == "__main__":
    main()
