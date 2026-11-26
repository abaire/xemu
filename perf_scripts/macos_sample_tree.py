#!/usr/bin/env python3
"""Breaks down one xemu thread from a macOS `sample` report, such as one written by macos_sample_test.sh.

The thread is found by a function on its stack, `pfifo_thread` by default. `sample` splits a thread's call tree by the
dispatch queue it was on, so every part of the report that contains that function is combined.

  summary  Prints the thread's idle and busy samples, the share of busy samples spent in mutex operations and in
           xemu's own pgraph/pfifo functions, and the functions with the most self time.
  paths    Prints every call path that holds at least --min-share of the thread's samples.

A note on inlining: in optimized builds, functions such as pfifo_run_pusher are inlined into their callers, so their
time shows up as the caller's self time.
"""

from __future__ import annotations

import argparse
import re
from collections import defaultdict

NODE_RE = re.compile(r"^(?P<indent>[ +!:|]*)(?P<count>\d+) (?P<name>.+)$")
IDLE_FUNCTIONS = {"__psynch_cvwait"}


class Node:
    def __init__(self, name: str, count: int, depth: int):
        self.name = name
        self.count = count
        self.depth = depth
        self.children: list[Node] = []


def short_name(name: str) -> str:
    name = re.sub(r"\s+\(in [^)]*\).*$", "", name)
    name = re.sub(r"\s+\+ \d+.*$", "", name)
    return name.strip()


def parse_threads(path: str) -> list[Node]:
    threads: list[Node] = []
    stack: list[Node] = []
    in_tree = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line.startswith("Call graph:"):
                in_tree = True
                continue
            if in_tree and (line.startswith("Total number in stack") or line.startswith("Sort by top")):
                break
            if not in_tree or not line.strip():
                continue
            m = NODE_RE.match(line)
            if not m:
                continue
            depth = len(m.group("indent"))
            node = Node(short_name(m.group("name")), int(m.group("count")), depth)
            if node.name.startswith("Thread_"):
                threads.append(node)
                stack = [node]
                continue
            while stack and stack[-1].depth >= depth:
                stack.pop()
            if stack:
                stack[-1].children.append(node)
            stack.append(node)
    return threads


def find(node: Node, name: str, out: list[Node]) -> None:
    """Collects the outermost nodes named `name` under `node`."""
    if node.name == name:
        out.append(node)
        return
    for child in node.children:
        find(child, name, out)


def thread_roots(path: str, root_name: str) -> list[Node]:
    roots: list[Node] = []
    for thread in parse_threads(path):
        find(thread, root_name, roots)
    return roots


def self_times(node: Node, acc: dict[str, int]) -> None:
    acc[node.name] += node.count - sum(c.count for c in node.children)
    for child in node.children:
        self_times(child, acc)


def summary(path: str, root_name: str, top: int) -> None:
    roots = thread_roots(path, root_name)
    if not roots:
        print(f"{root_name} not found")
        return
    acc: dict[str, int] = defaultdict(int)
    for root in roots:
        self_times(root, acc)
    total = sum(r.count for r in roots)
    idle = sum(acc[name] for name in IDLE_FUNCTIONS)
    busy = total - idle

    def share(predicate) -> int:
        return sum(count for name, count in acc.items() if predicate(name))

    locks = share(lambda n: "mutex" in n.lower() or "unfair_lock" in n)
    xemu = share(lambda n: n.startswith(("pgraph_", "pfifo_", "ramht_")))
    print(f"{root_name}: {total} samples, {idle} idle, {busy} busy")
    if busy:
        print(f"  mutex operations: {locks} ({100.0 * locks / busy:.1f}% of busy)")
        print(f"  xemu pgraph/pfifo self time: {xemu} ({100.0 * xemu / busy:.1f}% of busy)")
    print("  top self time (% of busy):")
    for name, count in sorted(acc.items(), key=lambda kv: -kv[1])[:top]:
        if name in IDLE_FUNCTIONS:
            continue
        print(f"    {count:6d} ({100.0 * count / max(busy, 1):5.1f}%) {name}")


def paths(path: str, root_name: str, min_share: float) -> None:
    roots = thread_roots(path, root_name)
    total = sum(r.count for r in roots)
    if not total:
        print(f"{root_name} not found")
        return
    threshold = min_share * total

    def walk(node: Node, chain: list[str]) -> None:
        chain = [*chain, node.name]
        big = [c for c in node.children if c.count >= threshold]
        if not big:
            print(f"  {node.count:6d} ({100.0 * node.count / total:5.1f}%) " + " > ".join(chain))
            return
        rest = node.count - sum(c.count for c in big)
        if rest >= threshold:
            print(f"  {rest:6d} ({100.0 * rest / total:5.1f}%) " + " > ".join(chain) + " [rest]")
        for child in big:
            walk(child, chain)

    print(f"{root_name}: {total} samples")
    for root in roots:
        for child in root.children:
            if child.count >= threshold:
                walk(child, [])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["summary", "paths"])
    parser.add_argument("sample_file", help="Output of `sample <pid> <seconds> -file <path>`.")
    parser.add_argument("--root", default="pfifo_thread", help="Function that identifies the thread.")
    parser.add_argument("--top", type=int, default=15, help="summary: number of self-time functions to print.")
    parser.add_argument("--min-share", type=float, default=0.02, help="paths: smallest share of samples to print.")
    args = parser.parse_args()

    if args.command == "summary":
        summary(args.sample_file, args.root, args.top)
    else:
        paths(args.sample_file, args.root, args.min_share)


if __name__ == "__main__":
    main()
