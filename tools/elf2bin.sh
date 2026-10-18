#!/bin/bash
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

set -e

# Base directory setup
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

INPUT_ELF="${1:-build/app.elf}"
OUTPUT_BIN="${2:-build/app.bin}"

if [ -n "${JIELI_TOOLCHAIN_PATH:-}" ] && [ -f "$JIELI_TOOLCHAIN_PATH/bin/objcopy" ]; then
    TOOLCHAIN_OBJCOPY="$JIELI_TOOLCHAIN_PATH/bin/objcopy"
elif [ -f "$REPO_ROOT/toolchain/pi32v2/bin/objcopy" ]; then
    TOOLCHAIN_OBJCOPY="$REPO_ROOT/toolchain/pi32v2/bin/objcopy"
else
    TOOLCHAIN_OBJCOPY="objcopy"
fi

if [ ! -f "$INPUT_ELF" ]; then
    echo "Error: Input ELF '$INPUT_ELF' not found." >&2
    exit 1
fi

echo "=== Extracting Binary from ELF ==="
echo "Input:  $INPUT_ELF"
echo "Output: $OUTPUT_BIN"

mkdir -p "$(dirname "$OUTPUT_BIN")"

# The toolchain's objcopy (pi32v2-elf-objcopy) only allows ONE -j flag.
# So we extract .text and .data separately and must REBUILD the true
# contiguous loadable image ourselves.
#
# The linker aligns .data to 32 bytes, so there is normally a zero-filled
# GAP between the end of .text and the start of .data inside the load
# region (e.g. 0x18 bytes for the RAM image, 0x2fc for the flash image).
# Both loaders (tools/run_ram.sh -> jlrunner at the RAM origin, and the
# U-Boot flash image) place this blob CONTIGUOUSLY, so a naive
# "cat text data" concatenation silently drops that gap and shifts EVERY
# initialized global by its length -> garbage globals -> crash/reboot.
#
# We therefore compute the exact gap from the ELF section headers and pad
# it back in, so the blob matches the linked load addresses byte-for-byte.

# Step 1: Extract .text section
"$TOOLCHAIN_OBJCOPY" -O binary -j .text "$INPUT_ELF" "${OUTPUT_BIN}.text.tmp"

# Step 2: Extract .data section (if it has content)
DATA_SIZE=$("$TOOLCHAIN_OBJCOPY" -O binary -j .data "$INPUT_ELF" "${OUTPUT_BIN}.data.tmp" 2>&1; wc -c < "${OUTPUT_BIN}.data.tmp" 2>/dev/null || echo 0)

# Step 3: Rebuild contiguous image = text + gap + data.
# The gap between .text and .data in the physical load image is determined by
# the Load Memory Addresses (LMAs): gap = _data_lma - _text_end.
# Note: ELF file offsets (Off) reflect disk alignment/padding within the ELF container,
# NOT load memory layout! Using file offsets caused an artificial 732-byte zero gap in flash images.
python3 - "$INPUT_ELF" "${OUTPUT_BIN}.text.tmp" "${OUTPUT_BIN}.data.tmp" "$OUTPUT_BIN" <<'PYEOF'
import re, subprocess, sys
elf, text_tmp, data_tmp, out = sys.argv[1:5]

readelf = None
for cand in ("/usr/bin/llvm-readelf", "llvm-readelf", "readelf"):
    try:
        subprocess.run([cand, "--version"], capture_output=True, check=True)
        readelf = cand
        break
    except Exception:
        continue
if readelf is None:
    sys.exit("elf2bin: no readelf found to parse symbols/headers")

text = open(text_tmp, "rb").read()
data = open(data_tmp, "rb").read()
t_size = len(text)
d_size = len(data)

if d_size == 0:
    gap = 0
else:
    # First, attempt to read linker-defined LMA symbols: _text_end and _data_lma
    sym_out = subprocess.run([readelf, "-sW", elf], capture_output=True, text=True).stdout
    syms = {}
    for line in sym_out.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[7] in ("_text_end", "_data_lma"):
            syms[parts[7]] = int(parts[1], 16)

    gap = None
    if "_text_end" in syms and "_data_lma" in syms:
        gap = syms["_data_lma"] - syms["_text_end"]

    if gap is None:
        # Fall back to section LMA parsing via objdump
        objdump = None
        for cand in ("/usr/bin/llvm-objdump", "llvm-objdump", "objdump"):
            try:
                subprocess.run([cand, "--version"], capture_output=True, check=True)
                objdump = cand
                break
            except Exception:
                continue
        if objdump is not None:
            odh = subprocess.run([objdump, "-h", elf], capture_output=True, text=True).stdout
            sec_info = {}
            for line in odh.splitlines():
                parts = line.split()
                if len(parts) >= 5 and parts[1] in (".text", ".data"):
                    name = parts[1]
                    size = int(parts[2], 16)
                    vma = int(parts[3], 16)
                    lma = int(parts[4], 16) if len(parts) >= 6 and parts[4] not in ("TEXT", "DATA", "BSS") else vma
                    sec_info[name] = (size, lma)
            if ".text" in sec_info and ".data" in sec_info:
                t_sz, t_lma = sec_info[".text"]
                d_sz, d_lma = sec_info[".data"]
                gap = d_lma - (t_lma + t_sz)

    if gap is None:
        sys.exit("elf2bin: failed to determine load memory addresses for .text and .data")

    if gap < 0:
        sys.exit(f"elf2bin: negative gap {gap} (_data_lma < _text_end)")

with open(out, "wb") as f:
    f.write(text)
    if gap:
        f.write(b"\x00" * gap)
    f.write(data)
print(f"[elf2bin] text={t_size:#x} gap={gap:#x} data={d_size:#x} total={t_size+gap+d_size:#x}")
PYEOF

# Clean up
rm -f "${OUTPUT_BIN}.text.tmp" "${OUTPUT_BIN}.data.tmp"

echo "=== Extraction Successful ==="
echo "Generated Binary: $(wc -c < "$OUTPUT_BIN") bytes"
