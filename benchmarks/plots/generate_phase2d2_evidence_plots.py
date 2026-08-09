#!/usr/bin/env python3
"""Generate public Phase 2d-2 plots from committed JSON evidence."""

from __future__ import annotations

import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


HERE = Path(__file__).resolve().parent
EVALUATIONS = HERE.parent / "evaluations"
COLOR_BEFORE = "#6f7378"
COLOR_AFTER = "#86a83f"
COLORS = {
    "explicit_euler": "#e36b5b",
    "semi_implicit_euler": "#86a83f",
    "velocity_verlet": "#3978b8",
    "rk4": "#8c6bb1",
}
LABELS = {
    "explicit_euler": "Explicit Euler",
    "semi_implicit_euler": "Semi-implicit Euler",
    "velocity_verlet": "Velocity Verlet",
    "rk4": "RK4",
}


def save(fig: plt.Figure, stem: str) -> None:
    fig.savefig(HERE / f"{stem}.png", dpi=180, facecolor="white")
    fig.savefig(HERE / f"{stem}.svg", facecolor="white")
    plt.close(fig)


def plot_inline_accessor() -> None:
    data = json.loads((EVALUATIONS / "perf-heightmap-at-inline__bench-cpp.json").read_text())
    summary = next(row for row in data["summary"] if row["stage"] == "thermal_erode_only_approx")
    before = np.array(summary["before_run_means_us"])
    after = np.array(summary["after_run_means_us"])

    fig, ax = plt.subplots(figsize=(7.6, 4.8))
    rng = np.random.default_rng(7)
    ax.scatter(np.zeros_like(before) + rng.normal(0, 0.025, len(before)), before,
               color=COLOR_BEFORE, alpha=0.72, s=32, label="Before · commit 6624551")
    ax.scatter(np.ones_like(after) + rng.normal(0, 0.025, len(after)), after,
               color=COLOR_AFTER, alpha=0.8, s=32, label="After · commit 0beed39")
    ax.errorbar([0, 1], [before.mean(), after.mean()],
                yerr=[before.std(ddof=1), after.std(ddof=1)], fmt="_", markersize=28,
                linewidth=2.2, capsize=5, color="#151515", label="Mean ± 1 SD")
    ax.set_xticks([0, 1], ["Out-of-line accessor", "Inline accessor"])
    ax.set_ylabel("thermal erosion runtime (µs, approximate)")
    ax.set_title("Heightmap accessor placement changes thermal-erosion runtime", loc="left", weight="bold")
    ax.text(0.98, 0.96, f"{summary['improvement_pct']:.1f}% lower mean runtime",
            transform=ax.transAxes, ha="right", va="top", color=COLOR_AFTER, weight="bold")
    ax.grid(axis="y", color="#e7e7e3", linewidth=0.8)
    ax.spines[["top", "right", "left"]].set_visible(False)
    ax.legend(loc="upper right", bbox_to_anchor=(1, 0.88), frameon=False, fontsize=8)
    fig.text(0.02, 0.045,
             "n=12 runs/variant, 200 samples/run, 10 warmups. Apple M2, Apple Clang 17, -O3. "
             "Metric subtracts each run's fBm mean; native C++ only.\n"
             "Data: benchmarks/evaluations/perf-heightmap-at-inline__bench-cpp.json",
             fontsize=6.6, color="#666666")
    fig.subplots_adjust(left=0.12, right=0.98, top=0.84, bottom=0.22)
    save(fig, "perf-heightmap-at-inline__metric-thermal-runtime")


def plot_integrators() -> None:
    data = json.loads((EVALUATIONS / "physics-integrator-energy-drift__bench-cpp.json").read_text())
    operating_dt = min({row["dt"] for row in data["rows"]}, key=lambda value: abs(value - 1 / 60))
    rows = [row for row in data["rows"] if row["dt"] == operating_dt]
    rows.sort(key=lambda row: ["explicit_euler", "semi_implicit_euler", "velocity_verlet", "rk4"].index(row["integrator"]))
    values = [max(abs(row["max_drift_pct"]), abs(row["min_drift_pct"])) for row in rows]
    plotted = [max(value, 0.0001) for value in values]

    fig, ax = plt.subplots(figsize=(9.2, 5.2))
    y = np.arange(len(rows))
    ax.barh(y, plotted, color=[COLORS[row["integrator"]] for row in rows], height=0.58)
    ax.set_yticks(y, [f"{LABELS[row['integrator']]} · {row['force_evals_per_step']} force eval/step"
                      if row["force_evals_per_step"] == 1 else
                      f"{LABELS[row['integrator']]} · {row['force_evals_per_step']} force evals/step"
                      for row in rows])
    ax.invert_yaxis()
    ax.set_xscale("log")
    ax.set_xlabel("maximum absolute energy drift over 20 periods (%) · log scale")
    ax.set_title("Integrator behavior at the operating dt (1/60 s)", loc="left", weight="bold")
    for index, value in enumerate(values):
        label = f"{value:.4g}%" if value else "< 0.0001%"
        ax.text(plotted[index] * 1.18, index, label, va="center", fontsize=8)
    ax.grid(axis="x", which="major", color="#e7e7e3", linewidth=0.8)
    ax.spines[["top", "right", "left"]].set_visible(False)
    fig.text(0.02, 0.045,
             "Isolated 1D harmonic oscillator (k=1, m=1), not terrain-contact dynamics. "
             "This compares numerical behavior, not wall-clock speed.\n"
             "Data: benchmarks/evaluations/physics-integrator-energy-drift__bench-cpp.json",
             fontsize=6.6, color="#666666")
    fig.subplots_adjust(left=0.36, right=0.97, top=0.84, bottom=0.22)
    save(fig, "physics-integrator-energy-drift__metric-operating-dt")


if __name__ == "__main__":
    plot_inline_accessor()
    plot_integrators()
