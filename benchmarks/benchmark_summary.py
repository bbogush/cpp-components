#!/usr/bin/env python3
"""Render Google Benchmark JSON results as a Markdown summary.

Usage: benchmark_summary.py RESULT.json [RESULT.json ...]
"""

import json
import sys
from pathlib import Path

# Keys Google Benchmark always emits; anything else numeric is a user counter.
STANDARD_KEYS = {
    "name", "family_index", "per_family_instance_index", "run_name", "run_type",
    "repetitions", "repetition_index", "threads", "iterations", "real_time",
    "cpu_time", "time_unit", "aggregate_name", "aggregate_unit", "label",
    "error_occurred", "error_message",
}


def format_number(value):
    for limit, suffix in ((1e9, "G"), (1e6, "M"), (1e3, "k")):
        if abs(value) >= limit:
            return f"{value / limit:.2f}{suffix}"
    return f"{value:.2f}"


NS_PER_UNIT = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}


def format_time(value, unit):
    ns = value * NS_PER_UNIT.get(unit, 1)
    for limit, suffix in ((1e9, "s"), (1e6, "ms"), (1e3, "us")):
        if ns >= limit:
            return f"{ns / limit:.2f} {suffix}"
    return f"{ns:.2f} ns"


def format_counter(key, value):
    if key.endswith("_per_second"):
        return f"{key}: {format_number(value)}/s"
    return f"{key}: {format_number(value)}"


def render(path):
    data = json.loads(Path(path).read_text())
    context = data.get("context", {})
    lines = [
        f"### {Path(path).stem}",
        "",
        f"{context.get('num_cpus', '?')} CPUs @ {context.get('mhz_per_cpu', '?')} MHz, "
        f"CPU scaling {'on' if context.get('cpu_scaling_enabled') else 'off'}",
        "",
        "| Benchmark | Real time | CPU time | Iterations | Counters |",
        "|-----------|----------:|---------:|-----------:|----------|",
    ]
    for bench in data.get("benchmarks", []):
        if bench.get("error_occurred"):
            lines.append(f"| {bench['name']} | error: {bench.get('error_message', '')} | | | |")
            continue
        unit = bench.get("time_unit", "ns")
        counters = ", ".join(
            format_counter(key, value)
            for key, value in bench.items()
            if key not in STANDARD_KEYS and isinstance(value, (int, float))
        )
        lines.append(
            f"| {bench['name']} "
            f"| {format_time(bench['real_time'], unit)} "
            f"| {format_time(bench['cpu_time'], unit)} "
            f"| {bench.get('iterations', '')} "
            f"| {counters} |"
        )
    return "\n".join(lines)


def main(paths):
    if not paths:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    print("\n\n".join(render(path) for path in sorted(paths)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
