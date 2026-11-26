#!/usr/bin/env python3
"""Runs xemu-perf-tests PfifoContention tests, one per xemu launch, with XEMU_STALL_TRACE enabled.

Run this with the Python interpreter of an xemu-perf-tester install, since it uses xemu-perf-tester and
python_xiso_repacker. The xemu build must include hw/xbox/nv2a/stall_trace.c for the trace logs to be written.

Each test is run in sustained mode with every load scaled to a fraction of the ISO's hardware calibration. For each
test, OUT_DIR receives:
  <test>-trace.log    the XEMU_STALL_TRACE output (see stall_trace.h), unless --no-trace is given
  <test>-results/     the xemu-perf-tester results
  <test>-runner.log   the xemu-perf-tester output

Summarize the results with analyze_stall_trace.py.

example:
  ~/xemu-perf-tester/venv/bin/python perf_scripts/run_stall_trace.py \\
      --tester-dir ~/xemu-perf-tester --iso xemu-perf-tests_xiso.iso --xemu dist/xemu.app \\
      --load-fraction 0.01 out Control LongPushbuffer

  To run a development build directly on macOS:
  DYLD_FALLBACK_LIBRARY_PATH=$PWD/dist/xemu.app/Contents/Libraries/arm64 \\
  ~/xemu-perf-tester/venv/bin/python perf_scripts/run_stall_trace.py \\
      --tester-dir ~/xemu-perf-tester --iso xemu-perf-tests_xiso.iso --xemu build/qemu-system-i386 --no-bundle \\
      out Control
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile

from python_xiso_repacker import ensure_extract_xiso, extract_file, replace_file

SUITE = "PfifoContention"
ALL_TESTS = [
    "Control",
    "LongPushbuffer",
    "MethodChurn",
    "TinyKicks",
    "HalfRatePresent",
    "SurfaceReadback",
    "OcclusionSpin",
    "CpuFramebufferWrite",
    "FillBound",
]


def make_iso(source_iso: str, out_dir: str, fraction: float, overrides: dict[str, int]) -> str:
    """Repacks `source_iso` with every calibrated load scaled by `fraction`, then `overrides` applied."""
    extract_xiso = ensure_extract_xiso(None)
    with tempfile.TemporaryDirectory() as tmp:
        calibration_path = os.path.join(tmp, "calibration.json")
        config_path = os.path.join(tmp, "config.json")
        if not extract_file(source_iso, "calibration.json", calibration_path, extract_xiso):
            sys.exit(f"{source_iso} has no calibration.json")
        if not extract_file(source_iso, "xemu_perf_tests_config.json", config_path, extract_xiso):
            sys.exit(f"{source_iso} has no xemu_perf_tests_config.json")
        with open(calibration_path, encoding="utf-8") as f:
            calibration = json.load(f)
        with open(config_path, encoding="utf-8") as f:
            config = json.load(f)

        loads = {name: max(1, round(load * fraction)) for name, load in calibration["loads"].items()}
        loads.update({f"{SUITE}::{test}": load for test, load in overrides.items()})
        config["loads"] = loads
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2)

        iso = os.path.join(out_dir, "stall_trace.iso")
        if not replace_file(source_iso, iso, "xemu_perf_tests_config.json", config_path, extract_xiso):
            sys.exit("Failed to repack the ISO")

    with open(os.path.join(out_dir, "loads.json"), "w", encoding="utf-8") as f:
        json.dump(loads, f, indent=2)
    return iso


def run_test(args: argparse.Namespace, iso: str, test: str) -> int:
    out_dir = args.out_dir
    block_list = os.path.join(out_dir, f"{test}-block_list.json")
    with open(block_list, "w", encoding="utf-8") as f:
        skipped = [f"{SUITE}::{other}" for other in ALL_TESTS if other != test]
        json.dump({"rules": [{"conditions": ["$version >= 0.0.0"], "skipped": skipped}]}, f)

    env = dict(os.environ)
    env.pop("XEMU_STALL_TRACE", None)
    if not args.no_trace:
        env["XEMU_STALL_TRACE"] = os.path.join(out_dir, f"{test}-trace.log")

    cmd = [
        sys.executable,
        "-c",
        "import xemu_perf_tester; xemu_perf_tester.execute()",
        "--verbose",
        "--iso",
        iso,
        "--xemu",
        args.xemu,
        "--run-mode",
        "sustained",
        "--just-suites",
        SUITE,
        "--renderers",
        args.renderer,
        "--vsync",
        args.vsync,
        "--repeat",
        "1",
        "--warmup-frames",
        str(args.warmup_frames),
        "--measure-frames",
        str(args.measure_frames),
        "--timeout-seconds",
        str(args.timeout_seconds),
        "--block-list-file",
        block_list,
        "--results-path",
        os.path.join(out_dir, f"{test}-results"),
        "--temp-path",
        os.path.join(out_dir, "work"),
        # A separate cache keeps --xemu from being copied over the tester's own cached builds.
        "--cache-path",
        args.cache_path or os.path.join(out_dir, "cache"),
    ]
    if args.no_bundle:
        cmd.append("--no-bundle")
    print(f"=== {test}", flush=True)
    with open(os.path.join(out_dir, f"{test}-runner.log"), "w", encoding="utf-8") as log:
        ret = subprocess.run(
            cmd, env=env, cwd=args.tester_dir, stdout=log, stderr=subprocess.STDOUT, check=False
        ).returncode
    print(f"    exit {ret}", flush=True)
    return ret


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out_dir", help="Directory to write the results into.")
    parser.add_argument("tests", nargs="*", default=ALL_TESTS, help=f"{SUITE} tests to run. Defaults to all.")
    parser.add_argument(
        "--tester-dir",
        required=True,
        help="xemu-perf-tester checkout. xemu-perf-tester is run from here so it finds its inputs/ BIOS files.",
    )
    parser.add_argument("--iso", required=True, help="xemu-perf-tests ISO that contains calibration.json.")
    parser.add_argument("--xemu", required=True, help="xemu executable, or xemu.app on macOS.")
    parser.add_argument("--load-fraction", type=float, default=0.01, help="Fraction of each calibrated load to run.")
    parser.add_argument(
        "--load",
        action="append",
        default=[],
        metavar="TEST=LOAD",
        help="Sets one test's load, overriding --load-fraction. May be repeated.",
    )
    parser.add_argument("--warmup-frames", type=int, default=30)
    parser.add_argument("--measure-frames", type=int, default=120)
    parser.add_argument("--timeout-seconds", type=int, default=300)
    parser.add_argument("--renderer", choices=["GL", "VK"], default="GL")
    parser.add_argument("--vsync", choices=["on", "off"], default="on")
    parser.add_argument("--no-trace", action="store_true", help="Run without XEMU_STALL_TRACE, e.g. for profiling.")
    parser.add_argument("--cache-path", help="xemu-perf-tester cache directory. Defaults to OUT_DIR/cache.")
    parser.add_argument(
        "--no-bundle",
        action="store_true",
        help="Run --xemu as given instead of looking for an xemu.app bundle. On macOS, a build/qemu-system-i386 needs "
        "DYLD_FALLBACK_LIBRARY_PATH set to a dist/xemu.app/Contents/Libraries/<arch> directory.",
    )
    args = parser.parse_args()

    unknown = set(args.tests) - set(ALL_TESTS)
    if unknown:
        parser.error(f"Unknown tests: {', '.join(sorted(unknown))}")
    overrides = {}
    for item in args.load:
        test, _, load = item.partition("=")
        if test not in ALL_TESTS or not load.isdigit():
            parser.error(f"Invalid --load '{item}'")
        overrides[test] = int(load)

    args.out_dir = os.path.abspath(args.out_dir)
    args.tester_dir = os.path.abspath(os.path.expanduser(args.tester_dir))
    args.xemu = os.path.abspath(os.path.expanduser(args.xemu))
    os.makedirs(args.out_dir, exist_ok=True)

    iso = make_iso(os.path.abspath(os.path.expanduser(args.iso)), args.out_dir, args.load_fraction, overrides)
    failures = sum(1 for test in args.tests if run_test(args, iso, test))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
