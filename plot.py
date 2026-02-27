#!/usr/bin/env python3
"""plot.py – generate charts from tdmm benchmark CSVs.

Produces four PNG files:
  1. utilization_over_time.png   – util% vs event number, one line per strategy
  2. avg_utilization.png         – bar chart of average utilisation per strategy
  3. speed_vs_size.png           – malloc & free ns vs request size (log x-axis)
  4. overhead_over_time.png      – overhead bytes vs event number per strategy

Usage:
    python3 plot.py
"""

import sys
import csv
import math
from pathlib import Path
from collections import defaultdict

try:
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
except ImportError:
    sys.exit("matplotlib not found.  Run:  pip install matplotlib")

STRATEGY_COLORS = {
    "first_fit": "#2196F3",   # blue
    "best_fit":  "#4CAF50",   # green
    "worst_fit": "#F44336",   # red
}
STRATEGY_LABELS = {
    "first_fit": "First Fit",
    "best_fit":  "Best Fit",
    "worst_fit": "Worst Fit",
}

def read_csv(path):
    rows = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            rows.append(row)
    return rows


def group_by(rows, key):
    groups = defaultdict(list)
    for r in rows:
        groups[r[key]].append(r)
    return groups


# ── 1. Utilization over time ─────────────────────────────────────────────────
def plot_utilization_over_time(csv_path="utilization_over_time.csv",
                                out_path="utilization_over_time.png"):
    if not Path(csv_path).exists():
        print(f"  [skip] {csv_path} not found")
        return

    rows   = read_csv(csv_path)
    groups = group_by(rows, "strategy")

    fig, ax = plt.subplots(figsize=(10, 5))

    for strat, data in sorted(groups.items()):
        xs = [int(r["event_num"])       for r in data]
        ys = [float(r["utilization_pct"]) for r in data]
        # downsample to at most 2000 points so the PNG isn't enormous
        step = max(1, len(xs) // 2000)
        ax.plot(xs[::step], ys[::step],
                label=STRATEGY_LABELS.get(strat, strat),
                color=STRATEGY_COLORS.get(strat, None),
                linewidth=1.2)

    ax.set_xlabel("Event number (alloc / free)")
    ax.set_ylabel("Memory utilization (%)")
    ax.set_title("Memory Utilization Over Time")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=0)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"  Saved {out_path}")
    plt.close(fig)


# ── 2. Average utilization bar chart ─────────────────────────────────────────
def plot_avg_utilization(csv_path="avg_utilization.csv",
                          out_path="avg_utilization.png"):
    if not Path(csv_path).exists():
        print(f"  [skip] {csv_path} not found")
        return

    rows = read_csv(csv_path)
    strats = [r["strategy"]              for r in rows]
    avgs   = [float(r["avg_utilization_pct"]) for r in rows]
    colors = [STRATEGY_COLORS.get(s, "#888") for s in strats]
    labels = [STRATEGY_LABELS.get(s, s)      for s in strats]

    fig, ax = plt.subplots(figsize=(7, 5))
    bars = ax.bar(labels, avgs, color=colors, width=0.5, edgecolor="white")

    for bar, val in zip(bars, avgs):
        ax.text(bar.get_x() + bar.get_width() / 2,
                bar.get_height() + 0.3,
                f"{val:.2f}%",
                ha="center", va="bottom", fontsize=11)

    ax.set_ylabel("Average utilization (%)")
    ax.set_title("Average Memory Utilization per Strategy")
    ax.set_ylim(0, max(avgs) * 1.25 + 1)
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"  Saved {out_path}")
    plt.close(fig)


# ── 3. Speed vs size ──────────────────────────────────────────────────────────
def plot_speed_vs_size(csv_path="speed_vs_size.csv",
                        out_path="speed_vs_size.png"):
    if not Path(csv_path).exists():
        print(f"  [skip] {csv_path} not found")
        return

    rows   = read_csv(csv_path)
    groups = group_by(rows, "strategy")

    fig, (ax_malloc, ax_free) = plt.subplots(1, 2, figsize=(13, 5), sharey=False)

    for strat, data in sorted(groups.items()):
        xs  = [int(r["size_bytes"])   for r in data]
        mns = [float(r["malloc_ns"])  for r in data]
        fns = [float(r["free_ns"])    for r in data]
        kw  = dict(label=STRATEGY_LABELS.get(strat, strat),
                   color=STRATEGY_COLORS.get(strat, None),
                   marker="o", markersize=4, linewidth=1.5)
        ax_malloc.plot(xs, mns, **kw)
        ax_free.plot(xs, fns, **kw)

    for ax, title in [(ax_malloc, "t_malloc()"), (ax_free, "t_free()")]:
        ax.set_xscale("log", base=2)
        ax.set_xlabel("Request size (bytes)")
        ax.set_ylabel("Time (ns per call)")
        ax.set_title(f"{title} Speed vs Request Size")
        ax.legend()
        ax.grid(True, which="both", alpha=0.3)
        ax.xaxis.set_major_formatter(
            ticker.FuncFormatter(lambda v, _: _human_bytes(int(v))))

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"  Saved {out_path}")
    plt.close(fig)


def _human_bytes(n):
    for unit, div in [("M", 1 << 20), ("K", 1 << 10)]:
        if n >= div:
            return f"{n // div}{unit}"
    return str(n)


# ── 4. Overhead over time ─────────────────────────────────────────────────────
def plot_overhead_over_time(csv_path="overhead_over_time.csv",
                             out_path="overhead_over_time.png"):
    if not Path(csv_path).exists():
        print(f"  [skip] {csv_path} not found")
        return

    rows   = read_csv(csv_path)
    groups = group_by(rows, "strategy")

    fig, (ax_bytes, ax_pct) = plt.subplots(1, 2, figsize=(13, 5))

    for strat, data in sorted(groups.items()):
        step = max(1, len(data) // 2000)
        xs   = [int(r["event_num"])          for r in data][::step]
        ob   = [int(r["overhead_bytes"])      for r in data][::step]
        pct  = [float(r["overhead_pct"])      for r in data][::step]
        kw   = dict(label=STRATEGY_LABELS.get(strat, strat),
                    color=STRATEGY_COLORS.get(strat, None),
                    linewidth=1.2)
        ax_bytes.plot(xs, ob,  **kw)
        ax_pct.plot  (xs, pct, **kw)

    ax_bytes.set_xlabel("Event number")
    ax_bytes.set_ylabel("Overhead (bytes)")
    ax_bytes.set_title("Node Overhead Bytes Over Time")
    ax_bytes.legend()
    ax_bytes.grid(True, alpha=0.3)
    ax_bytes.set_ylim(bottom=0)

    ax_pct.set_xlabel("Event number")
    ax_pct.set_ylabel("Overhead (% of system memory)")
    ax_pct.set_title("Node Overhead % Over Time")
    ax_pct.legend()
    ax_pct.grid(True, alpha=0.3)
    ax_pct.set_ylim(bottom=0)

    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"  Saved {out_path}")
    plt.close(fig)


# ── entry point ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    print("Generating plots...")
    plot_utilization_over_time()
    plot_avg_utilization()
    plot_speed_vs_size()
    plot_overhead_over_time()
    print("Done.")
