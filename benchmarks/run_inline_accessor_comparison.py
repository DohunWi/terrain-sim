#!/usr/bin/env python3
"""Run the pre/post Heightmap::at() benchmark in an alternating order.

The two benchmark executables must already be compiled from the requested
commits with identical compiler flags. This wrapper preserves every run and
adds the run-level statistics used by the public evidence document.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path


TRACKED_STAGES = (
    "thermal_erode_only_approx",
    "fbm_plus_thermal_erode_combined",
)


def run_once(binary: Path, output: Path) -> dict:
    subprocess.run([str(binary), str(output)], check=True, capture_output=True, text=True)
    return json.loads(output.read_text())


def stage_mean(result: dict, name: str) -> float:
    return next(stage["mean_us"] for stage in result["stages"] if stage["name"] == name)


def summarize(before: list[dict], after: list[dict], stage: str) -> dict:
    before_values = [stage_mean(result, stage) for result in before]
    after_values = [stage_mean(result, stage) for result in after]
    before_mean = statistics.mean(before_values)
    after_mean = statistics.mean(after_values)
    before_sd = statistics.stdev(before_values)
    after_sd = statistics.stdev(after_values)
    se_diff = math.sqrt(before_sd**2 / len(before_values) + after_sd**2 / len(after_values))
    t_approx = (before_mean - after_mean) / se_diff if se_diff else math.inf
    improvement_pct = (before_mean - after_mean) / before_mean * 100.0
    accepted = t_approx >= 2.5 and improvement_pct >= 10.0
    return {
        "stage": stage,
        "unit": "microseconds",
        "before_run_means_us": before_values,
        "after_run_means_us": after_values,
        "before_mean_us": before_mean,
        "before_sd_us": before_sd,
        "after_mean_us": after_mean,
        "after_sd_us": after_sd,
        "improvement_pct": improvement_pct,
        "t_approx": t_approx,
        "decision": "Accepted" if accepted else "Inconclusive",
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=12)
    parser.add_argument("--before-commit", required=True)
    parser.add_argument("--after-commit", required=True)
    args = parser.parse_args()

    before_results: list[dict] = []
    after_results: list[dict] = []
    execution_order: list[str] = []

    with tempfile.TemporaryDirectory(prefix="terrain-inline-accessor-") as temp_dir:
        temp = Path(temp_dir)
        for repeat in range(args.repeats):
            order = ("before", "after") if repeat % 2 == 0 else ("after", "before")
            execution_order.extend(order)
            for variant in order:
                binary = args.before if variant == "before" else args.after
                result = run_once(binary, temp / f"{repeat:02d}-{variant}.json")
                (before_results if variant == "before" else after_results).append(result)

    document = {
        "experiment_id": "perf-heightmap-at-inline",
        "measured_at_utc": datetime.now(timezone.utc).isoformat(),
        "metric_scope": "64x64 fBm generation plus 10 thermal-erosion iterations; native C++ only",
        "before_commit": args.before_commit,
        "after_commit": args.after_commit,
        "compiler": "Apple Clang 17.0.0",
        "compiler_flags": "-std=c++20 -O3 -DNDEBUG -Wall -Wextra",
        "hardware": {
            "model": "MacBook Air",
            "chip": "Apple M2",
            "cpu_cores": "8 (4 performance + 4 efficiency)",
            "memory_gb": 8,
            "architecture": "arm64",
        },
        "operating_system": "macOS 26.5.1 (25F80)",
        "repeats": args.repeats,
        "samples_per_run": 200,
        "warmup_samples_per_run": 10,
        "execution_order": execution_order,
        "summary": [summarize(before_results, after_results, stage) for stage in TRACKED_STAGES],
        "raw_runs": {"before": before_results, "after": after_results},
        "limitations": [
            "thermal_erode_only_approx subtracts each run's fBm mean from the combined timing",
            "single-machine local measurement; it is not a cross-platform performance claim",
            "the benchmark excludes Python, pybind11, networking, and Unity",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")


if __name__ == "__main__":
    main()
