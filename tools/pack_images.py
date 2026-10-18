#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""
Firmware Packaging Automation Script for Jellybean-37 (SMK-37 Pro hardware)
Produces:
  1. Assembled SPI Flash image (jellybean37_flash_image.bin)
  2. Native JieLi update containers (jellybean37.ufw, jellybean37.fw)
  3. M-VAVE update canister with 20-byte interleaved watermark (.fwsc)
"""

import argparse
import os
import struct
import sys
from pathlib import Path

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
APP_SLOT = 0x8DFBC
FLASH_SIZE = 0x93000
FLASH_1M_SIZE = 0x100000
KEY = 0x980F

# Flash header defaults (burner_size 544, VID "0.01", flash_size 0xFF000, fs_ver 0x10, PID "AC791N_STORY")
FLASH_HDR = bytes.fromhex("20022f104cc900f00f001010a4ff4e5d0b41e0cd781dc856417fb367cf9f")
UFW_CHIP = b"AC791N"


def _crc_table():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if (c & 0x8000) else (c << 1)
        t.append(c & 0xFFFF)
    return t


_T = _crc_table()


def crc16(data: bytes, crc: int = 0) -> int:
    for b in data:
        crc = ((crc << 8) & 0xFFFF) ^ _T[((crc >> 8) ^ b) & 0xFF]
    return crc


def enc(buf: bytearray, off: int, size: int, key: int = 0xFFFF):
    """JieLi ENC stream cipher (in place)."""
    for i in range(size):
        buf[off + i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if (key & 0x8000) else 0)) & 0xFFFF


def sfc(buf: bytearray, off: int, size: int, base: int, key: int):
    """SFC cipher: ENC per 32-byte block, key mixed with block address."""
    for i in range(0, size, 32):
        enc(buf, off + i, min(size - i, 32), key ^ ((off + i - base) >> 2))


def chipkey_decode(d: bytes) -> int:
    thesum = sum(d[:16]) & 0xFF
    thesum = 0xAA if thesum >= 0xE0 else 0x55 if thesum <= 0x10 else thesum
    return sum(1 << i for i in range(16) if (d[16 + i] ^ d[15 - i]) < thesum)


def entry(dcrc: int, offset: int, size: int, flags: int, resvd: int, index: int, name, fill: int = 0xFF) -> bytes:
    if isinstance(name, bytes):
        nm = name
    else:
        nm = name.encode("ascii") + b"\0"
        nm = nm + bytes([fill]) * (16 - len(nm))
    body = struct.pack("<HIIBBH16s", dcrc, offset & 0xFFFFFFFF, size & 0xFFFFFFFF, flags, resvd, index, nm)
    return struct.pack("<H", crc16(body)) + body


def key_blob(key: int) -> bytes:
    """32 bytes that chipkey_decode() turns into `key` (+ CRC16)."""
    d = bytearray([0x10] * 16) + bytearray(16)
    for i in range(16):
        d[16 + i] = d[15 - i] ^ (0x00 if ((key >> i) & 1) else 0xFF)
    assert chipkey_decode(bytes(d)) == key
    return bytes(d) + struct.pack("<H", crc16(d))


def sdk_file(rel: str) -> bytes:
    path = os.path.join(PROJECT_ROOT, "vendor", "jieli-sdk", "cpu", "wl82", "tools", rel)
    with open(path, "rb") as f:
        return f.read()


def flash_image(app: bytes, key: int) -> bytes:
    spl = sdk_file("uboot.boot")
    cfg_tool = sdk_file("cfg_tool.bin")
    eq = sdk_file("cfg/eq_cfg_hw.bin")
    isd = key_blob(key) + b"[JELLYBEAN]\r\n"
    f = bytearray(b"\xFF" * FLASH_SIZE)

    # Head [0, 0x4000)
    spl_off = 0xA0
    isd_off = spl_off + len(spl)
    assert isd_off + len(isd) <= 0x4000
    f[spl_off : spl_off + len(spl)] = spl
    f[isd_off : isd_off + len(isd)] = isd
    top = (
        entry(crc16(spl), spl_off, len(spl), 0x00, 0x00, 0, "uboot.boot")
        + entry(crc16(isd), isd_off, len(isd), 0x02, 0x80, 0, "isd_config.ini")
        + entry(0xFFFF, 0x4000, 0xFFFFFFFF, 0x81, 0xFF, 0, "app_dir_head")
        + entry(0xFFFF, 0xFF000, 0x1000, 0x12, 0x01, 0xFFFF, "key_mac")
    )
    hdr = struct.pack("<H", crc16(FLASH_HDR)) + FLASH_HDR
    head = bytearray(hdr + top)
    for o in range(0, len(head), 32):
        enc(head, o, 32)
    f[0 : len(head)] = head

    # App area 0x4000..
    if len(app) > APP_SLOT:
        raise SystemExit(f"app is {len(app)} B, max slot size is {APP_SLOT} B")
    app_padded = app + b"\xFF" * (APP_SLOT - len(app))
    blk = 0x120 + APP_SLOT + len(cfg_tool)
    area = bytearray(b"\xFF" * blk)
    area[0x120 : 0x120 + APP_SLOT] = app_padded
    area[0x120 + APP_SLOT : blk] = cfg_tool
    ents = [
        entry(crc16(app_padded), 0x120, APP_SLOT, 0x82, 0xFF, 0, "app.bin"),
        entry(crc16(cfg_tool), 0x120 + APP_SLOT, len(cfg_tool), 0x82, 0xFF, 0, "cfg_tool.bin"),
        entry(0xFFFF, 0x93000, 352256, 0x12, 0x80, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),
        entry(0xFFFF, 0, FLASH_SIZE, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),
        entry(0xFFFF, 0xE9000, 0x1000, 0x92, 0x80, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")),
        entry(0xFFFF, 0xEA000, 0x12000, 0x92, 0x81, 1, "USR"),
    ]
    area[32 : 32 + len(b"".join(ents))] = b"".join(ents)
    area[0:32] = entry(crc16(area[32:blk]), 0x02000120, blk, 0x83, 0xFF, 0, "app_area_head")

    eq_e = entry(crc16(eq), 0x40, len(eq), 0x82, 0xFF, 1, "eq_cfg_hw.bin")
    cfg_body = eq_e + eq
    cfg = entry(crc16(cfg_body), 0x20, 0x20 + len(cfg_body), 0x83, 0xFF, 1, "cfg") + cfg_body
    region = bytearray(area + cfg)
    sfc(region, 0, len(region), 0, key)
    f[0x4000 : 0x4000 + len(region)] = region
    return bytes(f)


def build_ufw_and_fwsc(flash: bytes, ota: bytes, product: str) -> tuple[bytes, bytes]:
    files = [(0, "flash.bin", flash), (100, "ota.bin", ota)]
    off, ents = 0x400, []
    for i, (typ, name, d) in enumerate(files):
        off = (off + 0xFF) & ~0xFF
        e = bytearray(0x50)
        struct.pack_into("<HHHHIII", e, 0, typ, i, crc16(d), 0, off, len(d), (len(d) + 31) & ~31)
        e[0x40 : 0x40 + len(name)] = name.encode("ascii")
        ents.append((e, off, d))
        off += len(d)
    total = off

    hdr = bytearray(0x40)
    struct.pack_into("<IH", hdr, 4, total, len(files))
    struct.pack_into("<HHH", hdr, 10, 0x0004, 0x0200, 0)
    hdr[16 : 16 + len(UFW_CHIP)] = UFW_CHIP
    lst = bytearray()
    for e, _, _ in ents:
        x = bytearray(e)
        enc(x, 0, 0x50)
        lst += x
    struct.pack_into("<H", hdr, 2, crc16(lst))
    struct.pack_into("<H", hdr, 0, crc16(hdr[2:0x40]))
    enc(hdr, 0, 0x40)

    logical = bytearray(b"\xFF" * total)
    logical[0:0x40] = hdr
    logical[0x40 : 0x40 + len(lst)] = lst
    for _, o, d in ents:
        logical[o : o + len(d)] = d

    # Interleave 20-byte watermark for .fwsc
    raw = bytearray()
    for i in range(20):
        ch = product[i] if i < len(product) else None
        m = (ord(ch) + i + 1) & 0xFF if ch else 0x7D
        raw += logical[i * 0x2F : (i + 1) * 0x2F] + bytes([m])
    raw += logical[20 * 0x2F :]

    return bytes(logical), bytes(raw)


def main():
    parser = argparse.ArgumentParser(description="Jellybean-37 Firmware Packager")
    parser.add_argument("app_bin", help="Path to compiled jellybean37_flash.bin")
    parser.add_argument("out_dir", help="Output directory for generated firmware images")
    parser.add_argument("--ini", default=None, help="Path to isd_config.ini (optional)")
    parser.add_argument("--cpu-freq", choices=["320", "360"], default="360", help="Target CPU frequency in MHz")
    parser.add_argument("--version-str", default="jellybean37_001", help="Firmware version string")
    parser.add_argument("--post-build-dir", default=None, help="Ignored")
    parser.add_argument("--ota-bin", default=None, help="Path to ota.bin")
    args = parser.parse_args()

    app_path = Path(args.app_bin)
    if not app_path.is_file():
        sys.exit(f"Error: app_bin not found: {app_path}")

    ota_path = Path(args.ota_bin) if args.ota_bin else Path(args.out_dir) / "ota.bin"
    if not ota_path.is_file():
        sys.exit(f"Error: ota_bin not found: {ota_path}")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"=== Generating Flash Image for {args.version_str} (Key: {KEY:#06x}) ===")
    app_bytes = app_path.read_bytes()
    flash_bytes = flash_image(app_bytes, KEY)

    print(f"=== Packaging UFW Container and .fwsc Canister ===")
    ota_bytes = ota_path.read_bytes()
    ufw_bytes, fwsc_bytes = build_ufw_and_fwsc(flash_bytes, ota_bytes, args.version_str)

    flash_image_path = out_dir / "jellybean37_flash_image.bin"
    ufw_path = out_dir / "jellybean37.ufw"
    fw_path = out_dir / "jellybean37.fw"
    fwsc_path = out_dir / f"{args.version_str}.fwsc"

    flash_image_path.write_bytes(flash_bytes)
    ufw_path.write_bytes(ufw_bytes)
    fw_path.write_bytes(ufw_bytes)
    fwsc_path.write_bytes(fwsc_bytes)

    print("==========================================================")
    print(" Firmware packaging complete:")
    print(f" 1. Full SPI Flash Image: {flash_image_path} ({len(flash_bytes)} bytes)")
    print(f" 2. Native JieLi Images:  {ufw_path} ({len(ufw_bytes)} bytes)")
    print(f"                          {fw_path} ({len(ufw_bytes)} bytes)")
    print(f" 3. M-VAVE .fwsc Canister:{fwsc_path} ({len(fwsc_bytes)} bytes)")
    print("==========================================================")


if __name__ == "__main__":
    main()
