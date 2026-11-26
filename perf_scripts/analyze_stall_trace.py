#!/usr/bin/env python3
"""Summarizes the XEMU_STALL_TRACE logs written by run_stall_trace.py.

For each test, sums the trace intervals that fall inside the sustained run, then scales the totals to one guest frame
using the run's mean frame time. The window is the longest stretch of intervals that each saw a flip, minus the first
--warmup-flips flips and the last, partial interval.

Each site is printed as the time spent there per frame, the number of hits per frame and the longest single hit. The
"draws" and "flip_stalls" sites are counts only. Compare "draws" per frame against the test's load to check the
window: a mismatch means the frame time scaling is off.

Busy times are inflated by the tracing itself, by about 2.5% for draw-bound tests and much more for tests that
submit many tiny method batches. Rerun those with run_stall_trace.py --no-trace for their busy times.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re

LINE_RE = re.compile(r"(\w+)=(\d+)(?:/(\d+)/(\d+))?")


def parse_trace(path: str) -> list[dict]:
    lines = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            entry: dict = {}
            for name, total, count, max_us in LINE_RE.findall(raw):
                if name == "t_ms":
                    entry["t_ms"] = int(total)
                else:
                    entry[name] = (int(total), int(count), int(max_us))
            if "t_ms" in entry:
                lines.append(entry)
    return lines


def window(lines: list[dict], warmup_flips: int) -> tuple[list[dict], float]:
    """Returns the intervals of the measured run and their total duration in milliseconds."""
    flips = [line.get("flip_stalls", (0, 0, 0))[1] for line in lines]
    best = (0, 0)
    run_start = None
    for i, n in enumerate([*flips, 0]):
        if n and run_start is None:
            run_start = i
        elif not n and run_start is not None:
            if i - run_start > best[1] - best[0]:
                best = (run_start, i)
            run_start = None

    start, end = best[0], best[1] - 1
    seen = 0
    while start < end and seen < warmup_flips:
        seen += flips[start]
        start += 1
    selected = lines[start:end]
    if not selected or start == 0:
        return [], 0.0
    return selected, float(selected[-1]["t_ms"] - lines[start - 1]["t_ms"])


def summarize(out_dir: str, test: str, warmup_flips: int) -> dict | None:
    trace_path = os.path.join(out_dir, f"{test}-trace.log")
    results = glob.glob(os.path.join(out_dir, f"{test}-results", "*", "*.json"))
    if not os.path.isfile(trace_path) or not results:
        return None
    with open(results[0], encoding="utf-8") as f:
        cell = json.load(f)["cells"][0]
    if not cell["results"]:
        return {"test": test, "error": cell.get("error") or "no results"}
    record = cell["results"][0]

    selected, duration_ms = window(parse_trace(trace_path), warmup_flips)
    frame_ms = record["frame_us"]["mean"] / 1000.0
    frames = duration_ms / frame_ms if frame_ms else 0.0
    if not frames:
        return {"test": test, "error": "no measured intervals in the trace"}

    totals: dict[str, list[int]] = {}
    for line in selected:
        for name, value in line.items():
            if name == "t_ms":
                continue
            total, count, max_us = value
            acc = totals.setdefault(name, [0, 0, 0])
            acc[0] += total
            acc[1] += count
            acc[2] = max(acc[2], max_us)

    per_frame = {
        name: {"ms": round(total / 1000.0 / frames, 3), "count": round(count / frames, 1), "max_ms": max_us / 1000.0}
        for name, (total, count, max_us) in sorted(totals.items())
    }
    draws = totals.get("draws", [0, 0, 0])[1]
    method_us = totals.get("puller_method", [0, 0, 0])[0]
    return {
        "test": test,
        "load": record["load"],
        "busy_p50_ms": record["busy_us"]["p50"] / 1000.0,
        "busy_p95_ms": record["busy_us"]["p95"] / 1000.0,
        "frame_mean_ms": frame_ms,
        "missed_vblanks": record["missed_vblanks"],
        "window_ms": duration_ms,
        "window_frames": round(frames, 1),
        "method_us_per_draw": round(method_us / draws, 2) if draws else None,
        "per_frame": per_frame,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out_dir", help="Directory written by run_stall_trace.py.")
    parser.add_argument("--json", action="store_true", help="Print the summaries as JSON.")
    parser.add_argument(
        "--warmup-flips", type=int, default=30, help="Flips to skip at the start of each run. Defaults to 30."
    )
    parser.add_argument(
        "--min-ms", type=float, default=0.0, help="Hide timed sites that take less than this many ms per frame."
    )
    args = parser.parse_args()

    traces = glob.glob(os.path.join(args.out_dir, "*-trace.log"))
    tests = sorted(os.path.basename(p)[: -len("-trace.log")] for p in traces)
    summaries = [s for s in (summarize(args.out_dir, t, args.warmup_flips) for t in tests) if s]
    if args.json:
        print(json.dumps(summaries, indent=2))
        return

    for s in summaries:
        if "error" in s:
            print(f"## {s['test']}: {s['error']}\n")
            continue
        print(
            f"## {s['test']} load={s['load']} busy p50/p95={s['busy_p50_ms']}/{s['busy_p95_ms']} ms "
            f"frame mean={s['frame_mean_ms']} ms missed vblanks={s['missed_vblanks']} "
            f"window={s['window_frames']} frames method/draw={s['method_us_per_draw']} us"
        )
        for name, v in sorted(s["per_frame"].items(), key=lambda kv: -kv[1]["ms"]):
            if v["ms"] < args.min_ms and v["ms"] > 0:
                continue
            print(f"  {name:24s} {v['ms']:9.3f} ms/frame  {v['count']:9.1f} hits/frame  max {v['max_ms']:.3f} ms")
        print()


if __name__ == "__main__":
    main()
