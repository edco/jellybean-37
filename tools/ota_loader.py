#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""
tools/ota_loader.py - JieLi OTA Loader Image Packer

Re-implementation of JieLi's proprietary ota_loader.exe.
Produces byte-identical output to ota_loader.exe across all supported modes:
  - Single-file and multi-file packaging
  - compression vs nocompression
  - ram vs sfc vs sdram targets
  - Automatic double-buffer LZ4-HC streaming (level 6, 4096-byte blocks)
  - Optional dict.bin support if present in working directory

Usage:
    ota_loader.py <addr> <bin_path> <compression|nocompression> <ram|sfc> [...]
    ota_loader.py --edit <ota_path> <addr> <bin_path> <compression|nocompression> <ram|sfc>
    ota_loader.py -o <out_path> <addr> <bin_path> <compression|nocompression> <ram|sfc> [...]
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import struct
import sys
from pathlib import Path


def jl_crc16(data: bytes, init: int = 0x0000) -> int:
    """JieLi standard CRC-16 (poly 0x1021, init 0x0000)."""
    crc = init
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def make_sdfile_head(d_crc: int, a: int, b: int, attr: int, idx: int, name: bytes) -> bytes:
    """Constructs a 32-byte JieLi sdfile_file_head structure.

    Layout:
      0x00: u16 h_crc (CRC-16 of bytes 0x02..0x20)
      0x02: u16 d_crc (CRC-16 of data payload)
      0x04: u32 a     (Outer: file offset; Inner: uncompressed length)
      0x08: u32 b     (Outer: file size with inner hdr; Inner: run/load address)
      0x0C: u8  attr  (Outer: attributes 0x01/0x09/0x41/0x49; Inner: 0x00)
      0x0D: u8  rsv   (Reserved: 0x00)
      0x0E: u16 idx   (Outer: file index 0..N-1; Inner: 0x0000)
      0x10: 16s name  (Filename truncated to 16 bytes, null-padded)
    """
    name_field = name[:16].ljust(16, b"\x00")
    body = struct.pack("<HIIBBH16s", d_crc, a, b, attr, 0, idx, name_field)
    h_crc = jl_crc16(body)
    return struct.pack("<H", h_crc) + body


class LZ4StreamHC:
    """LZ4-HC streaming compressor matching JieLi's ota_loader.exe.

    Uses LZ4 HC compression level 6 with a 2-block ping-pong buffer (8192 bytes)
    matching the exact stack memory layout and streaming state machine in ota_loader.exe.
    """

    def __init__(self, lib_path: str | None = None, dict_data: bytes | None = None):
        self.lib = self._load_lz4_library(lib_path)
        self.dict_data = dict_data

        # Configure ctypes signatures
        self.lib.LZ4_createStreamHC.restype = ctypes.c_void_p
        self.lib.LZ4_freeStreamHC.argtypes = [ctypes.c_void_p]
        self.lib.LZ4_resetStreamHC.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.LZ4_compress_HC_continue.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
        ]
        self.lib.LZ4_compress_HC_continue.restype = ctypes.c_int

        if hasattr(self.lib, "LZ4_loadDictHC"):
            self.lib.LZ4_loadDictHC.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
            self.lib.LZ4_loadDictHC.restype = ctypes.c_int

    @staticmethod
    def _load_lz4_library(lib_path: str | None = None) -> ctypes.CDLL:
        candidates = []
        if lib_path:
            candidates.append(lib_path)

        find_res = ctypes.util.find_library("lz4")
        if find_res:
            candidates.append(find_res)

        candidates.extend([
            "liblz4.so.1",
            "/usr/lib/x86_64-linux-gnu/liblz4.so.1",
            "/usr/lib64/liblz4.so.1",
            "/usr/local/lib/liblz4.so",
            "/usr/lib/liblz4.so.1",
        ])

        for c in candidates:
            try:
                return ctypes.CDLL(c)
            except (OSError, TypeError):
                continue

        raise RuntimeError(
            "Could not load liblz4.so.1. Please ensure liblz4-1 is installed (e.g. `sudo apt install liblz4-1`)."
        )

    def compress_payload(self, data: bytes) -> bytes:
        """Compresses file data into 4096-byte chunk frames: [clen: u32][dlen: u32][payload]."""
        if not data:
            return b""

        stream = self.lib.LZ4_createStreamHC()
        try:
            self.lib.LZ4_resetStreamHC(stream, 6)
            if self.dict_data and hasattr(self.lib, "LZ4_loadDictHC"):
                self.lib.LZ4_loadDictHC(stream, self.dict_data, len(self.dict_data))

            # Ping-pong double buffer of 8192 bytes
            ping_pong = ctypes.create_string_buffer(8192)
            dst_buf = ctypes.create_string_buffer(8192 * 2)

            out = bytearray()
            n_blocks = (len(data) + 4095) // 4096
            for block_idx in range(n_blocks):
                chunk = data[block_idx * 4096 : (block_idx + 1) * 4096]
                toggle = block_idx % 2
                offset = toggle * 4096
                ctypes.memmove(ctypes.byref(ping_pong, offset), chunk, len(chunk))
                src_ptr = ctypes.cast(ctypes.addressof(ping_pong) + offset, ctypes.c_char_p)

                res_len = self.lib.LZ4_compress_HC_continue(
                    stream, src_ptr, dst_buf, len(chunk), len(dst_buf)
                )
                if res_len <= 0:
                    raise RuntimeError(f"LZ4 compression error on block {block_idx}")

                c_chunk = dst_buf.raw[:res_len]
                out += struct.pack("<II", res_len, len(chunk)) + c_chunk

            return bytes(out)
        finally:
            self.lib.LZ4_freeStreamHC(stream)


def parse_attr(is_compression: bool, target: str) -> int:
    """Computes outer sdfile_file_head attr byte.

    Rules derived from ota_loader.exe:
      - Bit 0x01 is always set.
      - Bit 0x08 is set if target == "sfc".
      - Bit 0x40 is set if compression is True.
    """
    attr = 0x01
    if target.strip().lower() == "sfc":
        attr |= 0x08
    if is_compression:
        attr |= 0x40
    return attr


def pack_ota_image(
    file_specs: list[tuple[int, str, bool, str]],
    output_path: str = "ota.bin",
    dict_path: str | None = None,
    quiet: bool = False,
) -> bytes:
    """Packs multiple input binaries into a JieLi OTA container byte-identically to ota_loader.exe.

    Args:
        file_specs: List of (address, filepath, is_compression, target)
        output_path: Destination path for ota.bin
        dict_path: Optional path to dict.bin (defaults to checking ./dict.bin)
        quiet: If True, suppress diagnostic printing
    """
    # Check for dictionary
    dict_data = None
    resolved_dict = None
    if dict_path and os.path.exists(dict_path):
        resolved_dict = dict_path
    elif os.path.exists("dict.bin"):
        resolved_dict = "dict.bin"

    if resolved_dict:
        with open(resolved_dict, "rb") as f:
            dict_data = f.read()
    elif not quiet:
        print("can't find dict")

    compressor = LZ4StreamHC(dict_data=dict_data)

    entries = []
    for addr, filepath, is_comp, target in file_specs:
        path = Path(filepath)
        if not path.exists():
            raise FileNotFoundError(f"Input file not found: {filepath}")

        raw_data = path.read_bytes()
        fname = path.name.encode("latin1", "replace")

        if is_comp:
            payload = compressor.compress_payload(raw_data)
        else:
            payload = raw_data

        attr = parse_attr(is_comp, target)

        # Inner header: dcrc = crc16(raw_data), a = len(raw_data), b = addr, attr/rsv = dict_len, idx = 0
        inner_dcrc = jl_crc16(raw_data)
        dict_len = len(dict_data) & 0xFFFF if dict_data else 0
        name_field = fname[:16].ljust(16, b"\x00")
        inner_body = struct.pack("<HIIHH16s", inner_dcrc, len(raw_data), addr, dict_len, 0, name_field)
        inner_hdr = struct.pack("<H", jl_crc16(inner_body)) + inner_body
        file_body = inner_hdr + payload

        inner_hcrc = struct.unpack("<H", inner_hdr[:2])[0]
        comp_str = "compression" if is_comp else "nocompression"
        if not quiet:
            print("file_info:")
            print(f"{path.name} {addr:x} {len(raw_data):x} {inner_dcrc:x} {inner_hcrc:x} {comp_str}")
            ratio = (len(file_body) / len(raw_data) * 100.0) if len(raw_data) > 0 else float("inf")
            print(f"{path.name}:{len(raw_data)}-->{len(file_body)} {ratio:.2f}")

        entries.append({
            "fname": fname,
            "attr": attr,
            "body": file_body,
        })

    # Build outer directory table at offset 0
    table_size = len(entries) * 32
    curr_offset = table_size
    headers = []
    for idx, entry in enumerate(entries):
        body = entry["body"]
        outer_dcrc = jl_crc16(body)
        outer_hdr = make_sdfile_head(outer_dcrc, curr_offset, len(body), entry["attr"], idx, entry["fname"])
        headers.append(outer_hdr)
        curr_offset += len(body)

    ota_bytes = b"".join(headers) + b"".join(e["body"] for e in entries)

    out_file = Path(output_path)
    if out_file.parent and not out_file.parent.exists():
        out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_bytes(ota_bytes)

    return ota_bytes


def edit_ota_image(
    ota_path: str,
    addr: int,
    new_bin_path: str,
    is_comp: bool,
    target: str,
    dict_path: str | None = None,
) -> None:
    """Implements ota_loader.exe --edit mode.

    Replaces the 'ext_norfs.bin' entry inside an existing ota.bin with new_bin_path.
    """
    ota_file = Path(ota_path)
    if not ota_file.exists():
        print(f"ERROR: Failed to read file = {ota_path}", file=sys.stderr)
        return

    data = bytearray(ota_file.read_bytes())
    if len(data) < 32:
        print("ERROR: Invalid otp.bin!!!", file=sys.stderr)
        return

    first_off = struct.unpack("<I", data[4:8])[0]
    if first_off % 32 != 0 or first_off > len(data):
        print("ERROR: Invalid otp.bin!!!", file=sys.stderr)
        return

    n_files = first_off // 32
    entries = []
    norfs_idx = -1

    for idx in range(n_files):
        hdr = data[idx * 32 : (idx + 1) * 32]
        h_crc, d_crc, off, sz, attr, rsv, f_idx, nm = struct.unpack("<HHIIBBH16s", hdr)
        clean_nm = nm.split(b"\x00")[0]
        if clean_nm == b"ext_norfs.bin":
            norfs_idx = idx

        inner_and_payload = bytes(data[off : off + sz])
        entries.append({
            "name": clean_nm,
            "attr": attr,
            "body": inner_and_payload,
        })

    if norfs_idx == -1:
        print("ERROR: Invalid otp.bin!!!", file=sys.stderr)
        return

    new_data = Path(new_bin_path).read_bytes()

    dict_data = None
    if dict_path and os.path.exists(dict_path):
        dict_data = Path(dict_path).read_bytes()
    elif os.path.exists("dict.bin"):
        dict_data = Path("dict.bin").read_bytes()

    if is_comp:
        compressor = LZ4StreamHC(dict_data=dict_data)
        new_payload = compressor.compress_payload(new_data)
    else:
        new_payload = new_data

    new_attr = parse_attr(is_comp, target)
    inner_dcrc = jl_crc16(new_data)
    dict_len = (len(dict_data) & 0xFFFF) if dict_data else 0
    name_field = b"ext_norfs.bin".ljust(16, b"\x00")
    inner_body = struct.pack("<HIIHH16s", inner_dcrc, len(new_data), addr, dict_len, 0, name_field)
    inner_hdr = struct.pack("<H", jl_crc16(inner_body)) + inner_body

    entries[norfs_idx]["attr"] = new_attr
    entries[norfs_idx]["body"] = inner_hdr + new_payload

    # Recompute offsets and rebuild OTA file
    table_size = len(entries) * 32
    curr_offset = table_size
    headers = []
    for idx, entry in enumerate(entries):
        body = entry["body"]
        outer_dcrc = jl_crc16(body)
        outer_hdr = make_sdfile_head(outer_dcrc, curr_offset, len(body), entry["attr"], idx, entry["name"])
        headers.append(outer_hdr)
        curr_offset += len(body)

    new_ota = b"".join(headers) + b"".join(e["body"] for e in entries)
    ota_file.write_bytes(new_ota)
    print("!!!success!!!")


def main(argv: list[str] | None = None) -> int:
    if argv is None:
        argv = sys.argv[1:]

    if not argv or "-h" in argv or "--help" in argv:
        print("usage: ota_loader.py 0x10200 uboot.bin nocompression sfc 0x10300 spp.bin compression ram")
        print("       ota_loader.py --edit ota.bin 0x10300 your.bin nocompression sfc")
        print("       ota_loader.py -o ota.bin 0x10200 uboot.bin compression ram")
        return 0

    output_path = "ota.bin"
    filtered_args = []
    i = 0
    is_edit_mode = False

    while i < len(argv):
        arg = argv[i]
        if arg in ("-o", "--output"):
            if i + 1 >= len(argv):
                print("Error: -o/--output requires a path argument.", file=sys.stderr)
                return 1
            output_path = argv[i + 1]
            i += 2
        elif arg == "--edit":
            is_edit_mode = True
            i += 1
        else:
            filtered_args.append(arg)
            i += 1

    if is_edit_mode:
        # Edit syntax: <ota_path> <addr> <bin_path> <compression|nocompression> <ram|sfc>
        if len(filtered_args) < 5:
            print("Error: --edit requires <ota_path> <addr> <bin_path> <compression|nocompression> <ram|sfc>", file=sys.stderr)
            return 1
        ota_path = filtered_args[0]
        addr = int(filtered_args[1], 0)
        bin_path = filtered_args[2]
        comp_str = filtered_args[3].lower()
        target_str = filtered_args[4].lower()
        is_comp = (comp_str == "compression")
        edit_ota_image(ota_path, addr, bin_path, is_comp, target_str)
        return 0

    # Normal syntax: multiples of 4 args: <addr> <file> <comp> <target>
    if len(filtered_args) % 4 != 0:
        print(f"Error: Invalid arguments count ({len(filtered_args)}). Expected groups of 4: <addr> <file> <comp> <target>", file=sys.stderr)
        return 1

    file_specs = []
    for k in range(0, len(filtered_args), 4):
        addr_str, file_str, comp_str, target_str = filtered_args[k : k + 4]
        try:
            addr = int(addr_str, 0)
        except ValueError:
            print(f"Error: Invalid address '{addr_str}'", file=sys.stderr)
            return 1

        is_comp = (comp_str.lower() == "compression")
        file_specs.append((addr, file_str, is_comp, target_str))

    pack_ota_image(file_specs, output_path=output_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
