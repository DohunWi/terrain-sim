"""Phase 2c architecture-evidence plots: before/after thread-scaling for the
perf experiment series (EXP-003 baseline vs EXP-009 final combined config).

Reads structured JSON from benchmarks/evaluations/ (perf-parallel-envs and
perf-optimal-scaling-check) -- no numbers are hand-edited, everything here is
derived from those two files. Regenerate after any change to the underlying
benchmarks:

    /usr/bin/env python3 benchmarks/plots/generate_perf_plots.py

Outputs PNG (for README embedding) and SVG (line/text-based, so also
generated per docs/evaluation-protocol.md section 16) into benchmarks/plots/.
"""

from __future__ import annotations

import json
import pathlib

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

HERE = pathlib.Path(__file__).resolve().parent
EVALUATIONS = HERE.parent / "evaluations"

# dataviz skill's validated categorical palette, slots 1 (blue) and 2 (orange) --
# first two slots clear every hard gate (CVD / normal-vision separation) in
# both light and dark on the default adjacent pairlist.
COLOR_BEFORE = "#2a78d6"
COLOR_AFTER = "#eb6834"


def load_before_after():
    before = json.loads((EVALUATIONS / "perf-parallel-envs__bench-cpp.json").read_text())
    after = json.loads((EVALUATIONS / "perf-optimal-scaling-check__bench-cpp.json").read_text())

    before_by_tc = {row["thread_count"]: row for row in before["sweep"]}
    after_by_tc = {row["thread_count"]: row for row in after["sweep"]}
    thread_counts = sorted(before_by_tc.keys())
    return thread_counts, before_by_tc, after_by_tc


def plot_metric(thread_counts, before_by_tc, after_by_tc, *, before_key, before_sd_key, after_key,
                 after_sd_key, ylabel, title, out_stem, log_y):
    before_mean = [before_by_tc[tc][before_key] for tc in thread_counts]
    before_sd = [before_by_tc[tc][before_sd_key] for tc in thread_counts]
    after_mean = [after_by_tc[tc][after_key] for tc in thread_counts]
    after_sd = [after_by_tc[tc][after_sd_key] for tc in thread_counts]

    fig, ax = plt.subplots(figsize=(7, 4.5), dpi=150)

    ax.errorbar(thread_counts, before_mean, yerr=before_sd, marker="o", markersize=6, linewidth=2,
                color=COLOR_BEFORE, capsize=3, label="Before (EXP-003 baseline)")
    ax.errorbar(thread_counts, after_mean, yerr=after_sd, marker="o", markersize=6, linewidth=2,
                color=COLOR_AFTER, capsize=3, label="After (EXP-009: aligned EnvSlot + alloc-free thermalErode)")

    if log_y:
        ax.set_yscale("log")
        ax.yaxis.set_major_formatter(mticker.ScalarFormatter())
        ax.yaxis.set_minor_formatter(mticker.NullFormatter())

    ax.set_xlabel("thread count")
    ax.set_ylabel(ylabel)
    ax.set_title(title, fontsize=11)
    ax.set_xticks(thread_counts)
    ax.grid(True, which="major", axis="both", color="#dddddd", linewidth=0.7, zorder=0)
    ax.set_axisbelow(True)
    ax.legend(loc="upper left", fontsize=8, frameon=False)

    fig.text(0.01, 0.02,
              "n=20 repeats/point, error bars = 1 SD. Dev machine: 8 physical/8 logical cores (Apple Silicon, arm64).\n"
              "Data: benchmarks/evaluations/{perf-parallel-envs,perf-optimal-scaling-check}__bench-cpp.json",
              fontsize=6.5, color="#666666")

    fig.tight_layout(rect=(0, 0.08, 1, 1))
    fig.savefig(HERE / f"{out_stem}.png")
    fig.savefig(HERE / f"{out_stem}.svg")
    plt.close(fig)


def main():
    thread_counts, before_by_tc, after_by_tc = load_before_after()

    plot_metric(
        thread_counts, before_by_tc, after_by_tc,
        before_key="resets_per_sec_mean", before_sd_key="resets_per_sec_sd",
        after_key="resets_per_sec_aligned_mean", after_sd_key="resets_per_sec_aligned_sd",
        ylabel="resets/sec (log scale)",
        title="Env reset throughput vs. thread count -- before/after EXP-005+EXP-008",
        out_stem="perf-optimal-scaling-check__metric-resets-per-sec",
        log_y=True,
    )

    plot_metric(
        thread_counts, before_by_tc, after_by_tc,
        before_key="steps_per_sec_mean", before_sd_key="steps_per_sec_sd",
        after_key="steps_per_sec_aligned_mean", after_sd_key="steps_per_sec_aligned_sd",
        ylabel="steps/sec",
        title="Physics-step throughput vs. thread count -- unchanged by the reset-side fixes",
        out_stem="perf-optimal-scaling-check__metric-steps-per-sec",
        log_y=False,
    )

    print("Wrote benchmarks/plots/perf-optimal-scaling-check__metric-resets-per-sec.{png,svg}")
    print("Wrote benchmarks/plots/perf-optimal-scaling-check__metric-steps-per-sec.{png,svg}")


if __name__ == "__main__":
    main()
