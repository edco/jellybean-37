#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""Jellybean-37 Direct-to-RAM Loader.

Uploads and executes a RAM firmware binary (jellybean37_ram.bin) in internal SRAM
via the JieLi AC791N MaskROM UBOOT SCSI interface (0x01C02000), without touching
or wearing out SPI NOR flash.

Workflow:
  1. Checks if the device is already in MaskROM UBOOT mode.
  2. If not, sends DFU SysEx (F0 22 24 35 7D F7) over USB-MIDI to trigger reboot.
  3. Connects to the JieLi UBOOT1.00 SCSI device (/dev/sg*, Windows SPTD, or pyusb).
  4. Encrypts blocks with the MaskROM CRC cipher and writes to SRAM.
  5. Issues JUMP_TO_MEMORY (0xFB08) to execute immediately.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import re
import struct
import sys
import time
from pathlib import Path


# --------------------------------------------------------------------------
# Protocol Constants
# --------------------------------------------------------------------------
ENTER_DFU_SYSEX = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])  # MaskROM DFU (RAM)

UBOOT_CMD_WRITE_MEMORY = 0xFB06
UBOOT_CMD_JUMP_TO_MEMORY = 0xFB08
DEFAULT_RAM_ADDR = 0x01C02000
DEFAULT_RAM_ARG = 1
UBOOT_BLOCK_SIZE = 512

# Internal SRAM safe bounds (0x01C02000 - 0x01C7FD80)
SRAM_SAFE_START = 0x01C02000
SRAM_SAFE_END = 0x01C7FD80    # Excludes 64-byte retention RAM at top of SRAM


class BoundaryViolationError(RuntimeError):
    pass


def validate_write_boundaries(addr: int, length: int, allow_unsafe: bool = False) -> None:
    """Validate target write address range against SRAM hardware safety boundaries."""
    if allow_unsafe:
        return

    end_addr = addr + length
    if addr < SRAM_SAFE_START:
        raise BoundaryViolationError(
            f"SAFETY VIOLATION: SRAM write start 0x{addr:08X} is below minimum allowed "
            f"RAM application entry (0x{SRAM_SAFE_START:08X})!\n"
            f"Writing below 0x{SRAM_SAFE_START:08X} will corrupt MaskROM low-memory structures.\n"
            f"To bypass this check, explicitly supply '--allow-unsafe-writes'."
        )
    if end_addr > SRAM_SAFE_END:
        raise BoundaryViolationError(
            f"SAFETY VIOLATION: SRAM write end 0x{end_addr:08X} exceeds safe SRAM ceiling "
            f"(0x{SRAM_SAFE_END:08X}) by {end_addr - SRAM_SAFE_END} bytes!\n"
            f"Addresses 0x{SRAM_SAFE_END:08X}..0x01C80000 are reserved for retention boot tags.\n"
            f"To bypass this check, explicitly supply '--allow-unsafe-writes'."
        )


# --------------------------------------------------------------------------
# Pure-Python CRC16 & Cryptographic Ciphers (Zero External Dependencies)
# --------------------------------------------------------------------------
CRC16_TABLE = []
for _i in range(256):
    _curr = _i << 8
    for _ in range(8):
        if _curr & 0x8000:
            _curr = ((_curr << 1) ^ 0x1021) & 0xFFFF
        else:
            _curr = (_curr << 1) & 0xFFFF
    CRC16_TABLE.append(_curr)


def jl_crc16(data: bytes | bytearray, crc: int = 0) -> int:
    """Calculate JieLi CRC16-CCITT across byte sequence."""
    for b in data:
        crc = ((crc << 8) & 0xFFFF) ^ CRC16_TABLE[(crc >> 8) ^ b]
    return crc


# Chinese cipher key: "孟黎我爱你，玉林" in GB2312
MENG_LI_MAGIC = b"\xc3\xcf\xc0\xe8\xce\xd2\xb0\xae\xc4\xe3\xa3\xac\xd3\xf1\xc1\xd6"


def jl_crc_cipher(buff: bytearray, off: int, size: int, key: int = 0xFFFFFFFF) -> None:
    """JieLi 'CrcDecode' cipher for MaskROM UBOOT memory transfers."""
    crc = jl_crc16(int.to_bytes(key >> 16, 2, "little"), key & 0xFFFF)
    magic_len = len(MENG_LI_MAGIC)
    for i in range(size):
        mi = i % magic_len
        crc = jl_crc16(MENG_LI_MAGIC[mi:mi+1], crc)
        buff[off + i] ^= (crc & 0xFF)


# --------------------------------------------------------------------------
# MIDI Transport Engine (for DFU Reboot Trigger)
# --------------------------------------------------------------------------
class MidiTransport:
    def send_sysex(self, data: bytes) -> None:
        raise NotImplementedError

    def close(self) -> None:
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


class AlsaRawBackend(MidiTransport):
    def __init__(self, port_path: str):
        self.fd = os.open(port_path, os.O_RDWR | os.O_NONBLOCK)
        self.path = port_path

    def send_sysex(self, data: bytes) -> None:
        total = 0
        while total < len(data):
            n = os.write(self.fd, data[total:])
            if n <= 0:
                break
            total += n

    def close(self) -> None:
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = None


class AlsaSeqBackend(MidiTransport):
    def __init__(self, client_name: str = "jellybean_loadram", card: int | None = None):
        asound = ctypes.CDLL("libasound.so.2")
        self.asound = asound
        self.handle = ctypes.c_void_p()
        SND_SEQ_OPEN_DUPLEX = 3
        SND_SEQ_NONBLOCK = 1
        err = asound.snd_seq_open(ctypes.byref(self.handle), b"default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK)
        if err < 0:
            raise RuntimeError(f"snd_seq_open failed: {err}")
        asound.snd_seq_set_client_name(self.handle, client_name.encode("latin1"))
        SND_SEQ_PORT_CAP_WRITE = 1 << 1
        SND_SEQ_PORT_CAP_SUBS_WRITE = 1 << 6
        SND_SEQ_PORT_TYPE_APPLICATION = 1 << 20
        self.port = asound.snd_seq_create_simple_port(
            self.handle,
            b"port0",
            SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION,
        )
        dest_client, dest_port = self._find_dest(card)
        err = asound.snd_seq_connect_to(self.handle, self.port, dest_client, dest_port)
        if err < 0:
            raise RuntimeError(f"snd_seq_connect_to failed: {err}")

    def _find_dest(self, card: int | None) -> tuple[int, int]:
        cinfo = ctypes.create_string_buffer(512)
        pinfo = ctypes.create_string_buffer(512)
        asound = self.asound
        asound.snd_seq_client_info_set_client(cinfo, -1)
        while asound.snd_seq_query_next_client(self.handle, cinfo) == 0:
            c = asound.snd_seq_client_info_get_client(cinfo)
            if c < 16:
                continue
            asound.snd_seq_port_info_set_client(pinfo, c)
            asound.snd_seq_port_info_set_port(pinfo, -1)
            while asound.snd_seq_query_next_port(self.handle, pinfo) == 0:
                caps = asound.snd_seq_port_info_get_capability(pinfo)
                p = asound.snd_seq_port_info_get_port(pinfo)
                if (caps & (1 << 1)):
                    return c, p
        raise RuntimeError("No destination MIDI port found via ALSA sequencer")

    def send_sysex(self, data: bytes) -> None:
        ev = ctypes.create_string_buffer(128)
        self.asound.snd_seq_ev_clear(ev)
        ev_p = ctypes.cast(ev, ctypes.c_void_p)
        self.asound.snd_seq_ev_set_source(ev_p, self.port)
        self.asound.snd_seq_ev_set_subs(ev_p)
        self.asound.snd_seq_ev_set_direct(ev_p)
        c_data = ctypes.create_string_buffer(data)
        self.asound.snd_seq_ev_set_sysex(ev_p, len(data), c_data)
        self.asound.snd_seq_event_output_direct(self.handle, ev_p)

    def close(self) -> None:
        if self.handle:
            try:
                self.asound.snd_seq_close(self.handle)
            except Exception:
                pass
            self.handle = None


class WinMmBackend(MidiTransport):
    def __init__(self, dev_idx: int = 0):
        winmm = ctypes.windll.winmm
        self.winmm = winmm
        self.handle = ctypes.c_void_p()
        res = winmm.midiOutOpen(ctypes.byref(self.handle), dev_idx, 0, 0, 0)
        if res != 0:
            raise RuntimeError(f"midiOutOpen failed with error code {res}")

    def send_sysex(self, data: bytes) -> None:
        class MIDIHDR(ctypes.Structure):
            _fields_ = [
                ("lpData", ctypes.c_char_p),
                ("dwBufferLength", ctypes.c_uint),
                ("dwBytesRecorded", ctypes.c_uint),
                ("dwUser", ctypes.c_void_p),
                ("dwFlags", ctypes.c_uint),
                ("lpNext", ctypes.c_void_p),
                ("reserved", ctypes.c_void_p),
                ("dwOffset", ctypes.c_uint),
                ("dwReserved", ctypes.c_uint * 8),
            ]

        c_buf = ctypes.create_string_buffer(data)
        hdr = MIDIHDR()
        hdr.lpData = ctypes.cast(c_buf, ctypes.c_char_p)
        hdr.dwBufferLength = len(data)
        hdr.dwBytesRecorded = len(data)
        self.winmm.midiOutPrepareHeader(self.handle, ctypes.byref(hdr), ctypes.sizeof(hdr))
        self.winmm.midiOutLongMsg(self.handle, ctypes.byref(hdr), ctypes.sizeof(hdr))
        while not (hdr.dwFlags & 0x01):
            time.sleep(0.005)
        self.winmm.midiOutUnprepareHeader(self.handle, ctypes.byref(hdr), ctypes.sizeof(hdr))

    def close(self) -> None:
        if self.handle:
            self.winmm.midiOutClose(self.handle)
            self.handle = None


class RtMidiBackend(MidiTransport):
    def __init__(self, port_idx: int = 0):
        import rtmidi
        self._out = rtmidi.MidiOut()
        self._out.open_port(port_idx)

    def send_sysex(self, data: bytes) -> None:
        self._out.send_message(list(data))

    def close(self) -> None:
        try:
            self._out.close_port()
        except Exception:
            pass


def find_default_midi_port() -> str | int | None:
    """Auto-detect the SMK-37 Pro USB-MIDI device."""
    if sys.platform == "linux" and os.path.exists("/proc/asound/cards"):
        try:
            with open("/proc/asound/cards", "r") as f:
                content = f.read()
            card_idx = None
            for line in content.splitlines():
                m = re.match(r"\s*(\d+)\s*\[([^\]]+)\]:\s*(.*)", line)
                if m:
                    c_idx, c_id, c_desc = m.groups()
                    desc = (c_id + " " + c_desc).lower()
                    if any(k in desc for k in ["smk-37", "smk37", "m-vave", "pro", "jellybean"]):
                        card_idx = c_idx
                        break
            if card_idx is not None:
                p = f"/dev/snd/midiC{card_idx}D0"
                if os.path.exists(p):
                    return p
            if os.path.exists("/dev/snd"):
                for f in sorted(os.listdir("/dev/snd")):
                    if f.startswith("midi"):
                        return os.path.join("/dev/snd", f)
        except Exception:
            pass

    try:
        import rtmidi
        midi_out = rtmidi.MidiOut()
        for idx, port_name in enumerate(midi_out.get_ports()):
            name = port_name.lower()
            if any(k in name for k in ["smk", "pro", "m-vave", "jellybean"]):
                return idx
        if midi_out.get_ports():
            return 0
    except ImportError:
        pass

    return 0 if sys.platform == "win32" else None


def open_midi_transport(port: str | int | None) -> MidiTransport:
    """Open appropriate MIDI transport for target platform."""
    if port is None:
        port = find_default_midi_port()
    if port is None:
        raise RuntimeError("No USB-MIDI ports found. Plug in the SMK-37 Pro or specify --port.")

    if isinstance(port, str) and port.startswith("/dev/snd/midi"):
        try:
            return AlsaRawBackend(port)
        except OSError as e:
            import errno
            if e.errno != errno.EBUSY:
                raise
            m = re.search(r"midiC(\d+)D", port)
            print(f"[*] {port} is busy (held by another MIDI client); falling back to ALSA sequencer")
            return AlsaSeqBackend(card=int(m.group(1)) if m else None)

    try:
        return RtMidiBackend(int(port))
    except (ImportError, ValueError):
        pass

    if sys.platform == "win32":
        return WinMmBackend(int(port) if isinstance(port, (int, str)) and str(port).isdigit() else 0)

    if isinstance(port, str) and os.path.exists(port):
        return AlsaRawBackend(port)

    raise RuntimeError(f"Unable to open MIDI port '{port}'. Install python-rtmidi or check device path.")


# --------------------------------------------------------------------------
# SCSI & UBOOT Transports (Linux SG_IO, Windows SPTD, macOS / pyusb)
# --------------------------------------------------------------------------
class ScsiTransport:
    def execute(self, cdb: bytes, data_out: bytes | None = None, data_in_len: int = 0) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


class LinuxSgioTransport(ScsiTransport):
    """Linux SCSI Generic (SG_IO ioctl on /dev/sg*) transport."""

    class _sg_io_hdr(ctypes.Structure):
        _fields_ = [
            ("interface_id",    ctypes.c_int),
            ("dxfer_direction", ctypes.c_int),
            ("cmd_len",         ctypes.c_ubyte),
            ("mx_sb_len",       ctypes.c_ubyte),
            ("iovec_count",     ctypes.c_short),
            ("dxfer_len",       ctypes.c_uint),
            ("dxferp",          ctypes.c_void_p),
            ("cmdp",            ctypes.c_void_p),
            ("sbp",             ctypes.c_void_p),
            ("timeout",         ctypes.c_uint),
            ("flags",           ctypes.c_uint),
            ("pack_id",         ctypes.c_int),
            ("usr_ptr",         ctypes.c_void_p),
            ("status",          ctypes.c_ubyte),
            ("masked_status",   ctypes.c_ubyte),
            ("msg_status",      ctypes.c_ubyte),
            ("sb_len_wr",       ctypes.c_ubyte),
            ("host_status",     ctypes.c_ushort),
            ("driver_status",   ctypes.c_ushort),
            ("resid",           ctypes.c_int),
            ("duration",        ctypes.c_uint),
            ("info",            ctypes.c_uint),
        ]

    SG_IO = 0x2285
    SG_DXFER_NONE = -1
    SG_DXFER_TO_DEV = -2
    SG_DXFER_FROM_DEV = -3

    def __init__(self, path: str):
        import fcntl
        self.fcntl = fcntl
        self.fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
        self.path = path

    def execute(self, cdb: bytes, data_out: bytes | None = None, data_in_len: int = 0) -> bytes:
        import fcntl
        sgio = self._sg_io_hdr()
        sgio.interface_id = ord("S")
        sgio.timeout = getattr(self, "timeout_ms", 5000)
        sgio.cmd_len = len(cdb)
        c_cdb = ctypes.create_string_buffer(cdb)
        sgio.cmdp = ctypes.cast(c_cdb, ctypes.c_void_p)

        sb = ctypes.create_string_buffer(32)
        sgio.sbp = ctypes.cast(sb, ctypes.c_void_p)
        sgio.mx_sb_len = 32

        c_data_in = None
        if data_out is not None:
            sgio.dxfer_direction = self.SG_DXFER_TO_DEV
            sgio.dxfer_len = len(data_out)
            c_data_out = ctypes.create_string_buffer(data_out)
            sgio.dxferp = ctypes.cast(c_data_out, ctypes.c_void_p)
        elif data_in_len > 0:
            sgio.dxfer_direction = self.SG_DXFER_FROM_DEV
            sgio.dxfer_len = data_in_len
            c_data_in = ctypes.create_string_buffer(data_in_len)
            sgio.dxferp = ctypes.cast(c_data_in, ctypes.c_void_p)
        else:
            sgio.dxfer_direction = self.SG_DXFER_NONE
            sgio.dxfer_len = 0

        res = fcntl.ioctl(self.fd, self.SG_IO, sgio)
        if res < 0:
            raise OSError(res, f"SG_IO ioctl failed on {self.path}")

        if c_data_in is not None:
            return bytes(c_data_in.raw[:data_in_len])
        return b""

    def close(self) -> None:
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = None


class WindowsSptdTransport(ScsiTransport):
    """Windows SCSI Pass-Through Direct transport."""

    class _SCSI_PASS_THROUGH_DIRECT(ctypes.Structure):
        _fields_ = [
            ("len", ctypes.c_ushort),
            ("status", ctypes.c_ubyte),
            ("path_id", ctypes.c_ubyte),
            ("target_id", ctypes.c_ubyte),
            ("lun", ctypes.c_ubyte),
            ("cdb_len", ctypes.c_ubyte),
            ("sense_len", ctypes.c_ubyte),
            ("dir", ctypes.c_ubyte),
            ("data_len", ctypes.c_ulong),
            ("timeout", ctypes.c_ulong),
            ("data_ptr", ctypes.c_void_p),
            ("sense_off", ctypes.c_ulong),
            ("cdb", ctypes.c_ubyte * 16),
        ]

    IOCTL_SCSI_PASS_THROUGH_DIRECT = 0x4D014
    SCSI_IOCTL_DATA_OUT = 0
    SCSI_IOCTL_DATA_IN = 1
    SCSI_IOCTL_DATA_UNSPECIFIED = 2

    def __init__(self, path: str):
        kernel32 = ctypes.windll.kernel32
        self.kernel32 = kernel32
        GENERIC_READ = 0x80000000
        GENERIC_WRITE = 0x40000000
        FILE_SHARE_READ = 1
        FILE_SHARE_WRITE = 2
        OPEN_EXISTING = 3
        self.handle = kernel32.CreateFileW(
            path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            None,
            OPEN_EXISTING,
            0,
            None,
        )
        if self.handle == ctypes.c_void_p(-1).value or self.handle == -1:
            raise ctypes.WinError()
        self.path = path

    def execute(self, cdb: bytes, data_out: bytes | None = None, data_in_len: int = 0) -> bytes:
        sptd = self._SCSI_PASS_THROUGH_DIRECT()
        sptd.len = ctypes.sizeof(sptd)
        sptd.target_id = 1
        sptd.timeout = 5
        sptd.cdb_len = len(cdb)
        for i in range(min(16, len(cdb))):
            sptd.cdb[i] = cdb[i]

        c_buf = None
        if data_out is not None:
            sptd.dir = self.SCSI_IOCTL_DATA_OUT
            sptd.data_len = len(data_out)
            c_buf = ctypes.create_string_buffer(data_out)
            sptd.data_ptr = ctypes.cast(c_buf, ctypes.c_void_p)
        elif data_in_len > 0:
            sptd.dir = self.SCSI_IOCTL_DATA_IN
            sptd.data_len = data_in_len
            c_buf = ctypes.create_string_buffer(data_in_len)
            sptd.data_ptr = ctypes.cast(c_buf, ctypes.c_void_p)
        else:
            sptd.dir = self.SCSI_IOCTL_DATA_UNSPECIFIED
            sptd.data_len = 0

        returned = ctypes.c_ulong()
        res = self.kernel32.DeviceIoControl(
            self.handle,
            self.IOCTL_SCSI_PASS_THROUGH_DIRECT,
            ctypes.byref(sptd),
            sptd.len,
            ctypes.byref(sptd),
            sptd.len,
            ctypes.byref(returned),
            None,
        )
        if not res:
            raise ctypes.WinError()

        if c_buf is not None and data_in_len > 0:
            return bytes(c_buf.raw[:data_in_len])
        return b""

    def close(self) -> None:
        if self.handle:
            self.kernel32.CloseHandle(self.handle)
            self.handle = None


class PyUsbBotTransport(ScsiTransport):
    """macOS / cross-platform pyusb USB Mass Storage Bulk-Only Transport (BOT)."""

    def __init__(self, dev):
        self.dev = dev
        try:
            if dev.is_kernel_driver_active(0):
                dev.detach_kernel_driver(0)
        except Exception:
            pass
        dev.set_configuration()
        cfg = dev.get_active_configuration()
        intf = cfg[(0, 0)]
        self.ep_out = None
        self.ep_in = None
        for ep in intf:
            import usb.util
            if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT:
                self.ep_out = ep.bEndpointAddress
            else:
                self.ep_in = ep.bEndpointAddress
        self.tag = 1

    def execute(self, cdb: bytes, data_out: bytes | None = None, data_in_len: int = 0) -> bytes:
        dlen = len(data_out) if data_out is not None else data_in_len
        flags = 0x00 if data_out is not None else (0x80 if data_in_len > 0 else 0x00)
        cbw = bytearray(b"USBC")
        cbw += struct.pack("<IIBB", self.tag, dlen, flags, 0)
        cbw += bytes([len(cdb)])
        cbw += cdb[:16]
        cbw += b"\x00" * (31 - len(cbw))
        self.dev.write(self.ep_out, cbw, timeout=5000)

        if data_out is not None and len(data_out) > 0:
            self.dev.write(self.ep_out, data_out, timeout=5000)

        res_data = b""
        if data_in_len > 0:
            res_data = bytes(self.dev.read(self.ep_in, data_in_len, timeout=5000))

        csw = bytes(self.dev.read(self.ep_in, 13, timeout=5000))
        if len(csw) >= 13 and csw[:4] != b"USBS":
            raise RuntimeError("Invalid CSW signature from USB Mass Storage device")
        self.tag += 1
        return res_data

    def close(self) -> None:
        pass


def find_uboot_device(device_hint: str | None = None, timeout: float = 10.0) -> ScsiTransport | None:
    """Locate and connect to the JieLi UBOOT1.00 SCSI device."""
    end = time.monotonic() + timeout

    if device_hint:
        if sys.platform == "linux":
            return LinuxSgioTransport(device_hint)
        if sys.platform == "win32":
            return WindowsSptdTransport(device_hint)

    while time.monotonic() < end:
        if sys.platform == "linux" and os.path.exists("/dev"):
            for fname in sorted(os.listdir("/dev")):
                if fname.startswith("sg"):
                    path = os.path.join("/dev", fname)
                    try:
                        t = LinuxSgioTransport(path)
                        cdb = b"\x12\x00\x00\x00\x24\x00" + b"\xff" * 10
                        inq = t.execute(cdb, data_in_len=36)
                        if len(inq) >= 32:
                            vendor = inq[8:16].decode("latin1", errors="ignore").strip()
                            product = inq[16:32].decode("latin1", errors="ignore").strip()
                            if product.upper().startswith(("UBOOT", "UDISK", "DEVICE")) or "JIELI" in vendor.upper():
                                return t
                        t.close()
                    except Exception:
                        pass

        if sys.platform == "win32":
            for idx in range(16):
                path = rf"\\.\PhysicalDrive{idx}"
                try:
                    t = WindowsSptdTransport(path)
                    cdb = b"\x12\x00\x00\x00\x24\x00" + b"\xff" * 10
                    inq = t.execute(cdb, data_in_len=36)
                    if len(inq) >= 32:
                        vendor = inq[8:16].decode("latin1", errors="ignore").strip()
                        product = inq[16:32].decode("latin1", errors="ignore").strip()
                        if product.upper().startswith(("UBOOT", "UDISK", "DEVICE")) or "JIELI" in vendor.upper():
                            return t
                    t.close()
                except Exception:
                    pass

        try:
            import usb.core
            devices = usb.core.find(find_all=True)
            for d in devices:
                try:
                    for cfg in d:
                        for intf in cfg:
                            if (intf.bInterfaceClass == 0x08 and intf.bInterfaceSubClass == 0x06):
                                bot = PyUsbBotTransport(d)
                                cdb = b"\x12\x00\x00\x00\x24\x00" + b"\xff" * 10
                                inq = bot.execute(cdb, data_in_len=36)
                                if len(inq) >= 32:
                                    vendor = inq[8:16].decode("latin1", errors="ignore").strip()
                                    product = inq[16:32].decode("latin1", errors="ignore").strip()
                                    if product.upper().startswith(("UBOOT", "UDISK", "DEVICE")) or "JIELI" in vendor.upper():
                                        return bot
                except Exception:
                    pass
        except ImportError:
            pass

        time.sleep(0.5)

    return None


# --------------------------------------------------------------------------
# Direct-to-RAM Execution Engine (MaskROM UBOOT1.00)
# --------------------------------------------------------------------------
def load_ram_image(
    scsi: ScsiTransport,
    ram_data: bytes,
    addr: int = DEFAULT_RAM_ADDR,
    arg: int = DEFAULT_RAM_ARG,
    allow_unsafe_writes: bool = False,
    verbose: bool = False,
) -> None:
    """Upload binary directly to SRAM and jump to execution."""
    validate_write_boundaries(addr, len(ram_data), allow_unsafe=allow_unsafe_writes)

    # Ensure binary is padded to 512-byte blocks
    if len(ram_data) % UBOOT_BLOCK_SIZE != 0:
        pad_len = UBOOT_BLOCK_SIZE - (len(ram_data) % UBOOT_BLOCK_SIZE)
        ram_data += b"\x00" * pad_len

    total_bytes = len(ram_data)
    curr_addr = addr
    uploaded = 0

    print(f"[*] Uploading {total_bytes} bytes to SRAM @ 0x{addr:08X}...")

    for offset in range(0, total_bytes, UBOOT_BLOCK_SIZE):
        block = bytearray(ram_data[offset : offset + UBOOT_BLOCK_SIZE])
        jl_crc_cipher(block, 0, len(block), 0xFFFFFFFF)
        block_crc = jl_crc16(block)

        # Build WRITE_MEMORY (0xFB06) CDB
        cdb = bytearray()
        cdb += struct.pack(">H", UBOOT_CMD_WRITE_MEMORY)
        cdb += struct.pack(">I", curr_addr)
        cdb += struct.pack(">H", len(block))
        cdb += b"\x00"
        cdb += struct.pack("<H", block_crc)
        cdb += b"\xff" * (16 - len(cdb))

        scsi.execute(bytes(cdb), data_out=bytes(block))
        curr_addr += len(block)
        uploaded += len(block)

        if verbose or uploaded == total_bytes or uploaded % (UBOOT_BLOCK_SIZE * 32) == 0:
            pct = 100 * uploaded // total_bytes
            print(f"  [+] SRAM: {uploaded}/{total_bytes} bytes ({pct}%)", end="\r", flush=True)

    print(f"\n[+] Memory upload complete ({uploaded} bytes).")
    print(f"[*] Issuing jump to 0x{addr:08X} (arg=0x{arg:04X})...")

    # Build JUMP_TO_MEMORY (0xFB08) CDB
    cdb_jump = bytearray()
    cdb_jump += struct.pack(">H", UBOOT_CMD_JUMP_TO_MEMORY)
    cdb_jump += struct.pack(">I", addr)
    cdb_jump += struct.pack(">H", arg)
    cdb_jump += b"\xff" * (16 - len(cdb_jump))

    import threading
    scsi.timeout_ms = 1000

    def _jump() -> None:
        try:
            scsi.execute(bytes(cdb_jump), data_in_len=16)
        except Exception:
            pass

    th = threading.Thread(target=_jump, daemon=True)
    th.start()
    th.join(2.0)
    if th.is_alive():
        scsi.close = lambda: None

    print("=== Firmware successfully loaded and running in RAM ===")


# --------------------------------------------------------------------------
# Main CLI & Workflow Controller
# --------------------------------------------------------------------------
def main() -> int:
    default_img = Path(__file__).resolve().parent.parent / "build" / "bin" / "jellybean37_ram.bin"
    ap = argparse.ArgumentParser(
        description="Jellybean-37 Direct-to-RAM Loader (MaskROM UBOOT)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument(
        "image",
        nargs="?",
        default=str(default_img),
        help="Path to RAM binary (e.g. jellybean37_ram.bin)",
    )
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=DEFAULT_RAM_ADDR, help="SRAM load destination address")
    ap.add_argument("--arg", type=lambda x: int(x, 0), default=DEFAULT_RAM_ARG, help="Execution argument for jump command")
    ap.add_argument("--device", help="Direct SCSI device path (/dev/sg* on Linux, PhysicalDriveN on Win)")
    ap.add_argument("--port", help="MIDI device name or index for DFU trigger")
    ap.add_argument("--no-reboot", action="store_true", help="Do not trigger DFU reboot via MIDI SysEx")
    ap.add_argument("--allow-unsafe-writes", action="store_true", help="Bypass SRAM safety boundary validation")
    ap.add_argument("--verbose", "-v", action="store_true", help="Verbose transfer output")
    args = ap.parse_args()

    img_path = Path(args.image)
    if not img_path.is_file():
        print(f"Error: RAM image file not found: {img_path}", file=sys.stderr)
        return 1

    img_data = img_path.read_bytes()
    if len(img_data) < 512:
        print(f"Error: {img_path} is too small to be a valid RAM binary ({len(img_data)} bytes)", file=sys.stderr)
        return 1

    try:
        validate_write_boundaries(args.addr, len(img_data), allow_unsafe=args.allow_unsafe_writes)
    except BoundaryViolationError as e:
        print(f"\n[!] {e}", file=sys.stderr)
        return 1

    print("\n=== Jellybean-37 Direct-to-RAM Execution (Zero NOR Flash Writes) ===")

    # Step 1: Check if UBOOT SCSI device is already available
    scsi = find_uboot_device(args.device, timeout=1.0)

    # Step 2: If not found and reboot enabled, trigger reboot via MIDI SysEx
    if not scsi and not args.no_reboot:
        print("[*] Checking for SMK-37 Pro in application mode to trigger DFU reboot...")
        try:
            t = open_midi_transport(args.port)
            t.send_sysex(ENTER_DFU_SYSEX)
            t.close()
            print("[*] DFU SysEx (0x7D) sent. Waiting for MaskROM UBOOT SCSI device to enumerate...")
        except Exception as e:
            print(f"[!] Warning: Could not send DFU SysEx over MIDI: {e}")
            print("[*] Assuming keyboard is already in DFU mode, checking SCSI bus...")

        scsi = find_uboot_device(args.device, timeout=12.0)

    if not scsi:
        print(
            "\n[!] Error: Unable to detect JieLi MaskROM UBOOT SCSI device.\n"
            "Troubleshooting Steps:\n"
            "  1. Ensure the USB cable is securely connected.\n"
            "  2. To force MaskROM mode manually, hold down the DFU button while plugging in USB.\n"
            "  3. On Linux, ensure current user has read/write permissions on /dev/sg* (e.g. sudo or 'dialout'/'disk' group).",
            file=sys.stderr,
        )
        return 1

    try:
        load_ram_image(
            scsi,
            img_data,
            addr=args.addr,
            arg=args.arg,
            allow_unsafe_writes=args.allow_unsafe_writes,
            verbose=args.verbose,
        )
    finally:
        scsi.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())