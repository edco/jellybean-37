#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""Install a Jellybean-37 package (.fwsc) on an SMK-37 Pro over USB-MIDI.

Implements the official M-VAVE two-stage pull-based OTA update protocol:
  Stage 1: Running firmware checks package and boots into the RAM update loader.
  Stage 2: Update loader (running in SRAM with interrupts off) pulls flash.bin,
           verifies whole-image CRC, programs the application partition,
           confirms with host, and reboots into the new firmware.

Usage:
  jellybean_install.py PACKAGE.fwsc [--port NAME] [--yes] [--force]
  jellybean_install.py --info [--port NAME]

If the SMK-37 Pro is already in update mode (e.g. from an interrupted flash),
the installer detects it and proceeds straight to Stage 2.
Supports both mido (cross-platform) and native Linux ALSA rawmidi devices.
"""

import argparse
import glob
import hashlib
import os
import queue
import re
import select
import sys
import time

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
UPGRADE = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
MAXDATA = 512
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F

PORT_RE = re.compile(r"smk|pro|m-vave|ota|jellybean|usb-midi|composite|sinco|speed", re.I)

DELAY = {
    "open": 0.3,
    "start": 2.0,
    "reply": 0.01,
    "loader": 3.0,
    "reboot": 3.0,
    "retry": 1.0,
    "poll": 1.0,
    "hs": 1.0,
    "idle_check": 10.0,
    "idle_write": 180.0,
    "wait_loader": 30.0,
    "wait_reboot": 40.0,
}

EXIT = {
    "usage": 2,
    "badpkg": 2,
    "notfound": 3,
    "model": 6,
    "lost": 4,
    "stopped": 4,
    "badreq": 4,
    "noloader": 5,
    "noreturn": 5,
    "mismatch": 6,
}


class InstallError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------ wire format ---

def pack7(data: bytes) -> bytes:
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s: bytes) -> bytes:
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def response(addr: int, data: bytes, fl: int = 0) -> bytes:
    n = len(data)
    body = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl])
    body += addr.to_bytes(4, "little") + bytes([n & 0xFF, (n >> 8) & 0xFF, 0]) + bytes(data)
    body.append(~sum(body[6:]) & 0xFF)
    return b"\xF0" + pack7(body) + b"\xF7"


def parse_request(pkt: bytes):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 15 or u[:3] != b"\x00\x59\x30" or (~sum(u[6:14]) & 0xFF) != u[14]:
        return None
    return u[6], int.from_bytes(u[7:11], "little"), int.from_bytes(u[11:14], "little")


class Identity:
    def __init__(self, text: str, model: str, version: int):
        self.text, self.model, self.version = text, model, version

    @property
    def loader(self) -> bool:
        return self.model.lower().startswith("ota-")


def printable(b: bytes) -> str:
    return "".join(chr(c) if 32 <= c < 127 else "?" for c in b)


def parse_identity(pkt: bytes):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) != 34 or d[:3] != b"\x00\x59\x11":
        return None
    txt = printable(d[6:33].rstrip(b"\0"))
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


# --------------------------------------------------------------- package ---

def product_of(raw: bytes) -> str:
    if len(raw) < BLOCKS * BLK:
        raise InstallError("badpkg", "not an SMK-37 / M-VAVE package (too short)")
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def logical_image(raw: bytes) -> bytes:
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK : i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK :])


def model_of(text: str) -> str:
    m = text.split("_")[0].removeprefix("ota-")
    if m.lower() in ("jellybean37", "jellybean-37", "smk-37 pro", "smk-37", "smk37"):
        return "SMK-37 Pro"
    return m


# ------------------------------------------------------------------ MIDI ---

class MidoLink:
    def __init__(self, backend, in_name, out_name):
        import mido
        self.backend, self.name, self.mido = backend, in_name, mido
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.out = mido.open_output(out_name)
        except Exception:
            self.inp.close()
            raise
        time.sleep(DELAY["open"])

    def _rx(self, msg):
        if msg.type == "sysex":
            self.q.put(bytes([0xF0, *msg.data, 0xF7]))

    @property
    def lost(self):
        return self.name not in self.backend.input_names()

    def send(self, pkt):
        try:
            self.out.send(self.mido.Message("sysex", data=pkt[1:-1]))
            return True
        except Exception:
            return False

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        for p in (self.inp, self.out):
            try:
                p.close()
            except Exception:
                pass


class MidoBackend:
    def __init__(self):
        import mido
        self.mido = mido

    def input_names(self):
        return list(dict.fromkeys(self.mido.get_input_names()))

    def output_names(self):
        return list(dict.fromkeys(self.mido.get_output_names()))

    def open(self, in_name, out_name):
        return MidoLink(self, in_name, out_name)


class AlsaRawMidiLink:
    def __init__(self, path: str, name: str):
        self.path = path
        self.name = name
        self.fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
        self.buf = bytearray()
        time.sleep(DELAY["open"])

    @property
    def lost(self):
        return not os.path.exists(self.path)

    def send(self, pkt: bytes) -> bool:
        try:
            total = 0
            while total < len(pkt):
                n = os.write(self.fd, pkt[total:])
                if n <= 0:
                    break
                total += n
            return total == len(pkt)
        except OSError:
            return False

    def read(self, timeout: float):
        end = time.monotonic() + timeout
        while True:
            # Check accumulated buffer for SysEx frame F0 .. F7
            if 0xF0 in self.buf:
                s = self.buf.index(0xF0)
                if 0xF7 in self.buf[s:]:
                    e = s + self.buf[s:].index(0xF7) + 1
                    frame = bytes(self.buf[s:e])
                    self.buf = self.buf[e:]
                    return frame

            left = end - time.monotonic()
            if left <= 0:
                return None

            r, _, _ = select.select([self.fd], [], [], max(0, min(left, 0.05)))
            if r:
                try:
                    chunk = os.read(self.fd, 1024)
                    if chunk:
                        self.buf.extend(chunk)
                except OSError:
                    return None

    def drain(self):
        self.buf.clear()
        while True:
            r, _, _ = select.select([self.fd], [], [], 0)
            if not r:
                break
            try:
                os.read(self.fd, 1024)
            except OSError:
                break

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass


class AlsaRawMidiBackend:
    def input_names(self):
        card_desc = {}
        if os.path.exists("/proc/asound/cards"):
            try:
                content = open("/proc/asound/cards").read()
                for m in re.finditer(r"^\s*(\d+)\s+\[([^\]]+)\]:\s*.*?\n\s*(.*?)$", content, re.M):
                    c_num = m.group(1)
                    c_id = m.group(2).strip()
                    c_info = m.group(3).strip()
                    card_desc[c_num] = f"{c_id} - {c_info}"
            except OSError:
                pass

        names = []
        for dev in sorted(glob.glob("/dev/snd/midiC*D*")):
            card_m = re.search(r"midiC(\d+)D(\d+)", dev)
            if card_m:
                c_num = card_m.group(1)
                desc = card_desc.get(c_num)
                if not desc:
                    id_path = f"/sys/class/sound/card{c_num}/id"
                    if os.path.exists(id_path):
                        try:
                            desc = open(id_path).read().strip()
                        except OSError:
                            pass
                names.append(f"{dev} ({desc})" if desc else dev)
        return names

    def output_names(self):
        return self.input_names()

    def open(self, in_name, out_name):
        dev_path = in_name.split()[0]
        return AlsaRawMidiLink(dev_path, in_name)


def pair_output(name, outs):
    if name in outs:
        return name
    base = re.sub(r"\s+\d+$", "", name)
    same = [o for o in outs if re.sub(r"\s+\d+$", "", o) == base]
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


# --------------------------------------------------------------- updater ---

class Device:
    def __init__(self, link, ident, name):
        self.link, self.id, self.name = link, ident, name


def handshake(link, tries=3):
    link.drain()
    for _ in range(tries):
        if not link.send(HS_QUERY):
            return None
        end = time.monotonic() + DELAY["hs"]
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                break
            ident = parse_identity(p)
            if ident:
                return ident
    return None


class Updater:
    def __init__(self, backend, port=None):
        self.backend = backend
        self.port = port

    def candidates(self, only_port):
        names = self.backend.input_names()
        if self.port:
            mine = [n for n in names if self.port.lower() in n.lower()]
            if only_port:
                return mine
            return mine + [n for n in names if n not in mine and PORT_RE.search(n)]
        return [n for n in names if PORT_RE.search(n)]

    def find(self, want=None, only_port=True):
        outs = self.backend.output_names()
        for name in self.candidates(only_port):
            out = pair_output(name, outs)
            if not out:
                continue
            try:
                link = self.backend.open(name, out)
            except Exception:
                continue
            ident = handshake(link, 2)
            if ident and (want is None or want(ident)):
                return Device(link, ident, name)
            link.close()
        return None

    def wait_for(self, want, secs):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want, only_port=False)
            if dev:
                return dev
            time.sleep(DELAY["retry"])
        return None

    def serve(self, link, image, finish, idle, progress):
        served, last = 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None:
                if link.lost:
                    return served, False, True
                if time.monotonic() - last > idle:
                    return served, False, False
                continue
            r = parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last = time.monotonic()
            if addr in (FINISH_CHECK, FINISH_WRITE):
                link.send(response(addr, b"success\0"))
                if addr == finish:
                    return served, True, False
                continue
            if n > MAXDATA or addr + n > len(image):
                raise InstallError("badreq", f"device requested {addr:#x}+{n}, outside package boundary")
            time.sleep(DELAY["reply"])
            if not link.send(response(addr, image[addr : addr + n], fl)):
                return served, False, True
            served += 1
            progress(served, addr + n)

    def check(self, dev, image, step):
        step("start", dev.id.text)
        dev.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(
            dev.link, image, FINISH_CHECK, DELAY["idle_check"], lambda k, end: step("check", k)
        )
        dev.link.close()
        if not finished:
            raise InstallError(
                "lost" if lost else "stopped",
                f"SMK-37 Pro {'disconnected' if lost else 'stopped answering'} after {served} requests",
            )
        step("loader")
        time.sleep(DELAY["loader"])
        ota = self.wait_for(lambda i: i.loader, DELAY["wait_loader"])
        if not ota:
            raise InstallError(
                "noloader",
                "Update loader did not appear. Replug USB and rerun installer: keyboard stays in update mode.",
            )
        return ota

    def write(self, ota, image, step):
        step("write", 0)
        ota.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(
            ota.link,
            image,
            FINISH_WRITE,
            DELAY["idle_write"],
            lambda k, end: step("write", min(99, end * 100 // len(image))),
        )
        ota.link.close()
        if not finished:
            raise InstallError(
                "lost" if lost else "stopped",
                f"Update loader {'disconnected' if lost else 'stopped answering'} after {served} requests",
            )
        step("write", 100)

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        back = self.wait_for(lambda i: not i.loader, DELAY["wait_reboot"])
        if not back:
            raise InstallError("noreturn", "SMK-37 Pro did not return after reboot: please power-cycle")
        back.link.close()
        if back.id.text != product and model_of(back.id.text) != model_of(product):
            raise InstallError("mismatch", f"Flashed, but device reports {back.id.text} (expected {product})")
        step("done", back.id.text)
        return back.id.text

    def install(self, dev, image, product, step=lambda *a: None):
        if model_of(dev.id.text) != model_of(product):
            dev.link.close()
            raise InstallError("model", f"Device is {dev.id.text}, but package is for {product}")
        ota = dev if dev.id.loader else self.check(dev, image, step)
        self.write(ota, image, step)
        return self.verify(product, step)


# ------------------------------------------------------------------- CLI ---

def load_package(path: str, force: bool):
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        raise InstallError("badpkg", f"Cannot read {path}: {e.strerror}")
    product = product_of(raw)
    if not re.fullmatch(r"[^_]+_\d+", product):
        raise InstallError("badpkg", f"{path}: not a valid SMK-37 / M-VAVE package (identity {product!r})")
    return product, logical_image(raw)


def not_found(up: Updater):
    names = up.backend.input_names()
    where = f"No MIDI port matching {up.port!r} answered" if up.port else "SMK-37 Pro not found"
    return InstallError(
        "notfound",
        f"{where} (Check USB data cable and close any other DAW / MIDI applications)"
        f"\n  Available MIDI ports: {', '.join(names) if names else '(none)'}",
    )


class Progress:
    def __init__(self, out):
        self.out, self.line = out, False
        self.last_write_pct = -1

    def __call__(self, k, a=None):
        if k == "check":
            self.put(f"Stage 1: staging loader ({a} requests)")
        elif k == "write":
            pct = 100 if a >= 100 else (a // 10) * 10
            if pct != self.last_write_pct:
                self.last_write_pct = pct
                self.put(f"Stage 2: writing firmware {pct:3d}%")
        else:
            self.end()
            msg = {
                "start": f"Starting OTA session on {a}...",
                "loader": "Rebooting into dedicated RAM update loader...",
                "reboot": "Flashing complete. Rebooting into new firmware...",
                "done": f"Success! SMK-37 Pro is now running {a}.",
            }[k]
            print(msg, file=self.out, flush=True)

    def put(self, s):
        print(f"\r{s}\033[K", end="", file=self.out, flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(file=self.out, flush=True)
            self.line = False


def run(a, backend, out, ask):
    up = Updater(backend, a.port)
    if a.info:
        dev = up.find()
        if not dev:
            raise not_found(up)
        dev.link.close()
        mode = "UPDATE LOADER (recovery mode)" if dev.id.loader else "Running Firmware"
        print(f"Device: {dev.id.text}  [{mode}]  Port: {dev.name}", file=out)
        return 0

    product, image = load_package(a.package, a.force)
    print(f"Package: {product}  ({a.package})", file=out)
    dev = up.find()
    if not dev:
        raise not_found(up)
    mode_info = "  (already in update mode: resuming flash write)" if dev.id.loader else ""
    print(f"Device:  {dev.id.text}  ({dev.name}){mode_info}", file=out)

    if not a.yes and not ask("Proceed with firmware flash? Do not disconnect USB cable during flash. [y/N] "):
        dev.link.close()
        print("Cancelled.", file=out)
        return 1

    prog = Progress(out)
    try:
        up.install(dev, image, product, prog)
    finally:
        prog.end()
    return 0


def ask_tty(prompt: str) -> bool:
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False


def main(argv=None, backend=None, out=sys.stdout, ask=ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", help="SMK-37 Pro package canister (.fwsc)")
    ap.add_argument("--info", action="store_true", help="Print identity of connected SMK-37 Pro")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (substring match)")
    ap.add_argument("--yes", action="store_true", help="Bypass interactive confirmation")
    ap.add_argument("--force", action="store_true", help="Bypass loader marker validation")
    a = ap.parse_args(argv)

    if bool(a.info) == bool(a.package):
        ap.error("Provide a PACKAGE.fwsc or use --info")

    try:
        if backend is None:
            # Try mido first, fallback to native Linux ALSA rawmidi
            try:
                backend = MidoBackend()
            except ImportError:
                if sys.platform.startswith("linux"):
                    backend = AlsaRawMidiBackend()
                else:
                    raise InstallError("usage", "Requires mido with python-rtmidi (pip install mido python-rtmidi)")
        return run(a, backend, out, ask)
    except InstallError as e:
        print(f"Error: {e}", file=sys.stderr)
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\nInterrupted. If keyboard is in update mode, run jellybean_install.py again to finish.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
