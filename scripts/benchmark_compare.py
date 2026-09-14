#!/usr/bin/env python3
"""Compare google/benchmark JSON output against a committed baseline.

Usage:
  compare --current current.json --baseline baseline.json [--tolerance 15]
  compare --current current.json --save-baseline baseline.json

Exit codes:
  0  no regressions
  1  at least one benchmark regressed beyond tolerance
  2  baseline missing/empty (run --save-baseline to create one)
"""
import argparse
import json
import sys


def median_map(path):
    data = json.load(open(path, encoding="utf-8"))
    out = {}
    for b in data:
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
    ap.add_argument("--save-baseline", help="write a baseline from --current and exit")
    args = ap.parse_args()

    current = median_map(args.current)
    if not current:
        print("no median samples found in current output", file=sys.stderr)
        return 3

    if args.save_baseline:
        write_baseline(args.save_baseline, current)
        print(
            f"baseline written: {len(current)} benchmarks -> {args.save_baseline}"
        )
        return 0

    if not args.baseline:
        ap.error("--baseline is required unless --save-baseline is given")

    try:
        baseline = json.load(open(args.baseline, encoding="utf-8")).get(
            "benchmarks", {}
        )
    except FileNotFoundError:
        print(f"baseline file not found: {args.baseline}", file=sys.stderr)
        return 2

    common = [(n, current[n], baseline[n]) for n in current if n in baseline]
    if not common:
        print(
            "baseline has no matching benchmarks; run --save-baseline",
            file=sys.stderr,
        )
        return 2

    regressions = []
    for name, cur, base in sorted(common):
        base = float(base)
        if base <= 0:
            continue
        pct = (cur - base) / base * 100.0
        print(
            f"{'REGRESS' if pct >= args.tolerance else 'ok':8s} {pct:+9.2f}%  "
            f"{name}  cur={cur:>12.3f} base={base:>12.3f} ns"
        )
        if pct >= args.tolerance:
            regressions.append((name, cur, base, pct))

    missing = [n for n in sorted(current) if n not in baseline]
    if missing:
        print(f"\nnew benchmarks (no baseline): {', '.join(missing)}")

    if regressions:
        print(
            f"\nFAIL: {len(regressions)} benchmark(s) regressed >{args.tolerance}%"
        )
        return 1
    print(f"\nPASS: {len(common)} benchmark(s) within {args.tolerance}% tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())