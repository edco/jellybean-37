#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""
Flash a benchmark build to the SMK-37 Pro and collect CPU load statistics.

The device is flashed to SRAM with tools/jellybean_loadram.py, which makes it
re-enumerate as a USB device. This script then waits for the CDC ACM interface
to come back and parses the BENCHCPU records the firmware emits over it.

Typical use::

    python3 tools/mcu_bench.py                    # build + flash + measure
    python3 tools/mcu_bench.py --no-build         # reuse the existing build
    python3 tools/mcu_bench.py --label before     # tag the saved result
    python3 tools/mcu_bench.py --compare baseline.json

Build products land in a separate build directory from the normal firmware so a
benchmark run cannot overwrite a playable image.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BENCH_BUILD_DIR = REPO_ROOT / "build-bench"
RAM_IMAGE = BENCH_BUILD_DIR / "bin" / "jellybean37_ram.bin"
FLASHER = REPO_ROOT / "tools" / "jellybean_loadram.py"
RESULT_DIR = REPO_ROOT / "bench-results"

# "BENCHCPU n=750 avg=94.31 p99=98.11 max=99.90 over=0 | win_n=..."
BENCH_RE = re.compile(
    r"BENCHCPU\s+"
    r"v=(?P<voices>\d+)\s+"
    r"n=(?P<n>\d+)\s+"
    r"avg=(?P<avg>\d+\.\d+)\s+"
    r"p99=(?P<p99>\d+\.\d+)\s+"
    r"max=(?P<mx>\d+\.\d+)\s+"
    r"over=(?P<over>\d+)"
    r"(?:\s+fo=(?P<fo>\d+))?"
    r"(?:\s*\|\s*warm_n=(?P<warm_n>\d+)\s+warm_over=(?P<warm_over>\d+)"
    r"(?:\s*\|\s*win_n=(?P<win_n>\d+)\s+win_avg=(?P<win_avg>\d+\.\d+)"
    r"\s+win_max=(?P<win_max>\d+\.\d+)\s+win_over=(?P<win_over>\d+)))?"
)
# Emitted once at boot. Optional: every BENCHCPU record repeats the voice count,
# so the parser does not depend on catching this line.
READY_RE = re.compile(r"BENCHLOAD\s+ready\s+voices=(\d+)")


@dataclass
class BenchResult:
    """Cumulative whole-run CPU load, in percent of the 64,000-tick block budget."""

    label: str = ""
    blocks: int = 0
    avg: float = 0.0
    p99: float = 0.0
    max: float = 0.0
    over: int = 0
    voices: int = 0
    # Averages from the final reporting window only, useful for spotting a
    # build whose cost is drifting between the start and the end of the run.
    win_avg: float = 0.0
    win_max: float = 0.0
    # Load is bucketed into whole percents before the percentile is taken, so
    # p99 carries +/- 1% quantisation. Recorded so results are not mistaken for
    # exact timings when comparing builds.
    resolution_pct: float = 1.0
    blocks_per_second: float = 375.0
    seconds: float = 0.0
    raw: dict = field(default_factory=dict)


def log(msg: str) -> None:
    print(f"[bench] {msg}", flush=True)


def device_port(serial_hint: str | None) -> str | None:
    """
    Resolve the CDC port, preferring a previously seen serial number.

    ttyACM numbering is not stable across re-enumeration, so matching on
    ID_SERIAL_SHORT is what makes this robust; the glob is only a fallback for
    the first run or for a device that changed serial.
    """
    candidates = sorted(glob.glob("/dev/ttyACM*"))
    if not candidates:
        return None
    if not serial_hint:
        return candidates[0]
    for port in candidates:
        try:
            out = subprocess.run(
                ["udevadm", "info", "-q", "property", "-n", port],
                capture_output=True, text=True, timeout=5,
            ).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        for line in out.splitlines():
            if line.startswith("ID_SERIAL_SHORT=") and line.split("=", 1)[1] == serial_hint:
                return port
    return None


def device_serial() -> str | None:
    port = device_port(None)
    if port is None:
        return None
    try:
        out = subprocess.run(
            ["udevadm", "info", "-q", "property", "-n", port],
            capture_output=True, text=True, timeout=5,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for line in out.splitlines():
        if line.startswith("ID_SERIAL_SHORT="):
            return line.split("=", 1)[1]
    return None


def build() -> None:
    # Configure the ALL superbuild, not a bare MCU sub-build, so the generated
    # ninja file exposes the `mcu` ExternalProject target. BIN_OUTPUT_DIR is
    # redirected into the bench build tree: it defaults to build/bin,
    # and overwriting jellybean37_ram.bin there would leave the normal build tree
    # holding a benchmark image that cannot play.
    log("configuring benchmark build (MCUNO_BENCH_MODE=ON)")
    subprocess.run(
        ["cmake", "-G", "Ninja",
         "-S", str(REPO_ROOT), "-B", str(BENCH_BUILD_DIR),
         "-DMCUNO_BENCH_MODE=ON",
         f"-DBIN_OUTPUT_DIR={BENCH_BUILD_DIR / 'bin'}"],
        check=True, cwd=REPO_ROOT,
    )
    log("building jellybean37_ram.bin")
    subprocess.run(
        ["ninja", "-C", str(BENCH_BUILD_DIR), "mcu"],
        check=True, cwd=REPO_ROOT,
    )


def flash() -> None:
    log(f"flashing {RAM_IMAGE.name} to SRAM (RAM only, no NOR writes)")
    proc = subprocess.run(
        [sys.executable, str(FLASHER), str(RAM_IMAGE)],
        cwd=REPO_ROOT,
    )
    if proc.returncode != 0:
        raise SystemExit(f"flasher failed with exit code {proc.returncode}")


def wait_for_device(serial_hint: str | None, timeout: float) -> str:
    """
    Block until the CDC port reappears, then open it.

    The device is gone for a while mid-flash, so a port that exists is not on
    its own proof of readiness. Opening it and seeing the BENCHLOAD banner is;
    that is handled by the caller.
    """
    log(f"waiting for device (timeout {timeout:.0f}s)")
    deadline = time.time() + timeout
    while time.time() < deadline:
        port = device_port(serial_hint)
        if port is not None:
            log(f"device present at {port}")
            return port
        time.sleep(0.5)
    raise SystemExit(
        f"device did not re-enumerate within {timeout:.0f}s. "
        "Is it connected, and is it in a mode the flasher can reach?"
    )


def iter_lines(port: str, baud: int, deadline: float):
    """
    Yield decoded lines from the CDC port, reopening it as needed.

    The port node appears before the device behind it is ready to answer, so the
    first open/read after a re-enumeration can fail even though nothing is
    wrong. Treating that as fatal would make every run a coin flip on how fast
    the host races the device, so a dropped port is simply reopened until the
    deadline. Real failures still surface: the caller sees no records at all.
    """
    import serial

    reopened = False
    while time.time() < deadline:
        try:
            with serial.Serial(port, baud, timeout=0.5) as ser:
                ser.reset_input_buffer()
                while time.time() < deadline:
                    raw = ser.readline()
                    if raw:
                        yield raw.decode("utf8", "replace").strip()
        except (serial.SerialException, OSError) as e:
            if time.time() >= deadline:
                return
            reopened = True
            log(f"  {port} not ready ({type(e).__name__}); reopening")
            time.sleep(0.75)
    if reopened:
        log("  port was reopened during the run")


def collect(port: str, duration: float, baud: int) -> BenchResult:
    log(f"opening {port} at {baud} baud, sampling for {duration:.0f}s")
    result = BenchResult(label="")
    seen_data = False
    start = None

    deadline = time.time() + duration
    for text in iter_lines(port, baud, deadline):
        m = READY_RE.search(text)
        if m:
            result.voices = int(m.group(1))
            log(f"benchmark running: {m.group(1)} voices held")
            continue

        m = BENCH_RE.search(text)
        if not m:
            continue

        # Records are cumulative, so the last one seen carries the whole run.
        # Attaching late simply means fewer windows; it does not invalidate the
        # figures, because the device has been measuring since boot either way.
        seen_data = True
        if start is None:
            start = time.time()
            result.voices = int(m.group("voices"))
        result.blocks = int(m.group("n"))
        result.avg = float(m.group("avg"))
        result.p99 = float(m.group("p99"))
        result.max = float(m.group("mx"))
        result.over = int(m.group("over"))
        if m.group("fo"):
            result.first_over_block = int(m.group("fo"))
        result.raw = m.groupdict()
        if m.groupdict().get("warm_n"):
            result.warmup_blocks = int(m.group("warm_n"))
            result.warmup_over = int(m.group("warm_over"))
        if m.group("win_avg"):
            result.win_avg = float(m.group("win_avg"))
            result.win_max = float(m.group("win_max"))
        result.seconds = time.time() - start

    if not seen_data:
        raise SystemExit(
            f"connected to {port} but read no BENCHCPU records in {duration:.0f}s.\n"
            "Either this is not a benchmark build (rebuild with -DMCUNO_BENCH_MODE=ON, "
            "or drop --no-build so the script does it), or the device needs more than "
            f"{duration:.0f}s to produce its first report -- try a longer --duration."
        )
    return result


def report(result: BenchResult) -> None:
    headroom = 100.0 - result.avg
    print()
    print("=" * 66)
    print(f"  MCUno-60 sustained load: {result.voices} voices, Bench Heavy")
    if result.label:
        print(f"  build: {result.label}")
    print("=" * 66)
    print(f"  blocks measured : {result.blocks}  ({result.seconds:.1f}s)")
    print(f"  avg             : {result.avg:6.2f} %   headroom {headroom:5.2f} %")
    print(f"  p99             : {result.p99:6.2f} %")
    print(f"  max             : {result.max:6.2f} %")
    print(f"  block overruns  : {result.over}")
    if result.first_over_block:
        print(
            f"  first overrun   : block {result.first_over_block}"
            f" of {result.blocks} ({result.first_over_block / result.blocks * 100:.1f}% in)"
        )
    if result.warmup_blocks:
        print(
            f"  warmup excluded : {result.warmup_blocks} blocks"
            f" ({result.warmup_over} overran, cold start)"
        )
    if result.win_avg:
        print(f"  final window    : avg {result.win_avg:.2f} %, max {result.win_max:.2f} %")
    print(f"  p99 resolution   : +/- {result.resolution_pct:.0f} % (whole-percent histogram)")
    print("=" * 66)
    if result.over:
        print("  WARNING: blocks reached the deadline; audio is at risk of underrun.")
    print()


def compare(baseline_path: Path, result: BenchResult) -> None:
    try:
        base = json.loads(baseline_path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        raise SystemExit(f"cannot read baseline {baseline_path}: {e}") from e

    print()
    print(f"  vs baseline {baseline_path.name} ({base.get('label', 'unnamed')}):")
    print(f"  {'metric':<12} {'baseline':>10} {'now':>10} {'delta':>10}")
    print("  " + "-" * 44)
    for key, unit in (("avg", "%"), ("p99", "%"), ("max", "%")):
        b, n = float(base.get(key, 0.0)), float(getattr(result, key))
        delta = n - b
        arrow = "better" if delta < 0 else ("worse" if delta > 0 else "same")
        print(f"  {key:<12} {b:>9.2f}{unit} {n:>9.2f}{unit} {delta:>+9.2f} {arrow}")
    print(f"  {'headroom':<12} {100.0 - float(base.get('avg', 0.0)):>9.2f}% "
          f"{100.0 - result.avg:>9.2f}% {result.avg - float(base.get('avg', 0.0)):>+9.2f}")
    print()
    print("  Note: p99 and max carry +/-1% histogram quantisation, so differences")
    print("  smaller than that are not meaningful. avg is exact to 0.01%.")
    print()


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Flash a benchmark build and measure MCU audio CPU load.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument("--duration", type=float, default=20.0,
                    help="seconds to collect statistics after the banner")
    ap.add_argument("--no-build", action="store_true",
                    help="reuse the existing benchmark build instead of rebuilding")
    ap.add_argument("--no-flash", action="store_true",
                    help="skip flashing and read from an already-running device")
    ap.add_argument("--baud", type=int, default=115200, help="CDC baud rate")
    ap.add_argument("--timeout", type=float, default=90.0,
                    help="seconds to wait for the device to re-enumerate")
    ap.add_argument("--label", default="", help="name recorded with the result")
    ap.add_argument("--save", action="store_true", help="write bench-results/<label>.json")
    ap.add_argument("--compare", type=Path, default=None,
                    help="compare against a previously saved result JSON")
    args = ap.parse_args()

    try:
        import serial  # noqa: F401
    except ImportError:
        raise SystemExit("pyserial is required: pip install pyserial")

    serial_hint = device_serial()

    if not args.no_build:
        build()
    if not RAM_IMAGE.exists():
        raise SystemExit(f"missing {RAM_IMAGE}; build without --no-build first")

    if not args.no_flash:
        flash()

    port = wait_for_device(serial_hint, args.timeout)
    result = collect(port, args.duration, args.baud)
    result.label = args.label or time.strftime("%Y%m%d-%H%M%S")

    report(result)

    if args.save or args.compare:
        RESULT_DIR.mkdir(exist_ok=True)
    if args.save:
        out = RESULT_DIR / f"{result.label}.json"
        out.write_text(json.dumps(asdict(result), indent=2) + "\n")
        log(f"saved {out.relative_to(REPO_ROOT)}")
    if args.compare:
        compare(args.compare, result)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        sys.exit(130)