#!/usr/bin/env python3
"""Compare google/benchmark JSON output against a committed baseline.

Usage:
  compare --current current.json --baseline baseline.json [--tolerance 15]
          [--min-ns 500000] [--ignore-regex "(Parallel|Concurrent)"]
  compare --current current.json --save-baseline baseline.json

Benchmarks below --min-ns nanoseconds or matching --ignore-regex are reported
as skipped and never count as regressions: shared CI runners are too noisy
for ns-scale and thread-count-dependent measurements.

Exit codes:
  0  no regressions
  1  at least one benchmark regressed beyond tolerance
  2  baseline missing/empty (run --save-baseline to create one)
"""

import argparse
import json
import re
import sys


def median_map(path):
    data = json.load(open(path, encoding="utf-8"))
    # Accept either a merged list of runs or a single google/benchmark
    # document ({"context": ..., "benchmarks": [...]}) to be robust against
    # differently-shaped merge outputs.
    if isinstance(data, dict):
        data = data.get("benchmarks", [])
    out = {}
    for b in data:
        if not isinstance(b, dict):
            continue
        if b.get("run_type") == "aggregate" and b.get("aggregate_name") == "median":
            out[b["name"]] = b.get("real_time")
    return out


def write_baseline(path, current):
    blob = {
        "generated_by": "benchmark_compare.py --save-baseline",
        "benchmarks": current,
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(blob, f, indent=2, sort_keys=True)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--current", required=True, help="google/benchmark JSON output")
    ap.add_argument("--baseline", help="path to baseline JSON")
    ap.add_argument(
        "--tolerance",
        type=float,
        default=15.0,
        help="allowed relative slowdown in %% (default 15)",
    )
    ap.add_argument(
        "--min-ns",
        type=float,
        default=0.0,
        help="skip benchmarks whose median is below this many ns (noise floor)",
    )
    ap.add_argument(
        "--ignore-regex",
        help="skip benchmarks whose name matches this regex",
    )
    ap.add_argument("--save-baseline", help="write a baseline from --current and exit")
    args = ap.parse_args()

    current = median_map(args.current)
    if not current:
        print("no median samples found in current output", file=sys.stderr)
        return 3

    if args.save_baseline:
        write_baseline(args.save_baseline, current)
        print(f"baseline written: {len(current)} benchmarks -> {args.save_baseline}")
        return 0

    if not args.baseline:
        ap.error("--baseline is required unless --save-baseline is given")

    try:
        baseline = json.load(open(args.baseline, encoding="utf-8")).get("benchmarks", {})
    except FileNotFoundError:
        print(f"baseline file not found: {args.baseline}", file=sys.stderr)
        return 2

    ignore = re.compile(args.ignore_regex) if args.ignore_regex else None

    common = [(n, current[n], baseline[n]) for n in current if n in baseline]
    if not common:
        print(
            "baseline has no matching benchmarks; run --save-baseline",
            file=sys.stderr,
        )
        return 2

    regressions = []
    skipped = []
    noisy = []
    for name, cur, base in sorted(common):
        if ignore and ignore.search(name):
            skipped.append(name)
            continue
        cur = float(cur)
        base = float(base)
        if base < args.min_ns or cur < args.min_ns:
            noisy.append(name)
            continue
        pct = (cur - base) / base * 100.0
        print(
            f"{'REGRESS' if pct >= args.tolerance else 'ok':8s} {pct:+9.2f}%  "
            f"{name}  cur={cur:>12.3f} base={base:>12.3f} ns"
        )
        if pct >= args.tolerance:
            regressions.append((name, cur, base, pct))

    if skipped:
        print(f"ignored (--ignore-regex): {len(skipped)} benchmark(s) skipped")
    if noisy:
        print(f"noise floor (< {args.min_ns:.0f} ns): {len(noisy)} benchmark(s) skipped")

    missing = [
        n for n in sorted(current) if n not in baseline and not (ignore and ignore.search(n))
    ]
    if missing:
        print(f"\nnew benchmarks (no baseline): {', '.join(missing)}")

    compared = len(common) - len(skipped) - len(noisy)
    if regressions:
        print(f"\nFAIL: {len(regressions)} benchmark(s) regressed >{args.tolerance}%")
        return 1
    print(
        f"\nPASS: {compared} benchmark(s) within {args.tolerance}% tolerance "
        f"({len(skipped)} skipped by regex, {len(noisy)} below noise floor)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
