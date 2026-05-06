#!/usr/bin/env python3
"""
generate_plots.py — Standalone post-simulation plot & report generator.

Usage:
    python3 generate_plots.py                        # auto-detects latest sim dir
    python3 generate_plots.py <path-to-sim-dir>      # explicit sim dir

Generates inside the sim dir:
    plots/decision_quality.png
    plots/handovers_over_time.png
    plots/ho_per_ue.png
    plots/ho_activity.png
    decision_log.csv
    decision_summary.json
    summary.txt
"""

import csv
import json
import os
import sys
import uuid
from collections import Counter, defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESULTS_DIR = "/home/omar_farouk/open-ran-clean/3D_GUI_Sim_Results"


def _latest_sim_dir():
    dirs = sorted(
        [d for d in os.listdir(RESULTS_DIR) if d.startswith("sim") and len(d) > 5 and d[3:6].isdigit()],
        key=lambda d: os.path.getmtime(os.path.join(RESULTS_DIR, d)),
        reverse=True,
    )
    if not dirs:
        raise RuntimeError("No sim directories found in " + RESULTS_DIR)
    return os.path.join(RESULTS_DIR, dirs[0])


def _build_decisions(csv_path):
    rows = []
    with open(csv_path) as f:
        for r in csv.DictReader(f):
            if r.get("executed_ok", "0").strip() == "1":
                rows.append(r)
    rows.sort(key=lambda r: float(r["time_sec"]))

    ue_rows = defaultdict(list)
    for idx, r in enumerate(rows):
        ue_rows[r["ue_id"]].append((idx, r))

    pp_indices = set()
    for ue_list in ue_rows.values():
        for i in range(1, len(ue_list)):
            pi, a = ue_list[i - 1]
            ci, b = ue_list[i]
            if (a["to_cell"] == b["from_cell"] and a["from_cell"] == b["to_cell"]
                    and float(b["time_sec"]) - float(a["time_sec"]) <= 5.0):
                pp_indices.add(pi)

    sim_label = os.path.basename(os.path.dirname(csv_path))[:6]  # e.g. sim010
    decisions = []
    for idx, r in enumerate(rows):
        decisions.append({
            "uuid":       str(uuid.uuid4()),
            "sim":        sim_label,
            "time_sec":   float(r["time_sec"]),
            "ue_id":      int(r["ue_id"]),
            "from_cell":  int(r["from_cell"]),
            "to_cell":    int(r["to_cell"]),
            "is_correct": idx not in pp_indices,
        })
    return decisions, pp_indices


def generate(sim_dir):
    csv_path = os.path.join(sim_dir, "handover.csv")
    if not os.path.exists(csv_path):
        print(f"ERROR: handover.csv not found in {sim_dir}")
        sys.exit(1)

    sim_label = os.path.basename(sim_dir)[:6]
    print(f"Generating plots for {sim_label} → {sim_dir}")

    decisions, pp_indices = _build_decisions(csv_path)
    total     = len(decisions)
    pp_count  = len(pp_indices)
    correct   = total - pp_count
    accuracy  = round(100 * correct / total, 2) if total else 0.0
    pp_rate   = round(pp_count / total * 100, 2) if total else 0.0

    # ── decision_log.csv ──────────────────────────────────────────────────────
    log_path = os.path.join(sim_dir, "decision_log.csv")
    with open(log_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=decisions[0].keys())
        w.writeheader()
        w.writerows(decisions)
    print(f"  ✓ decision_log.csv  ({total} entries)")

    # ── Plots ─────────────────────────────────────────────────────────────────
    plots_dir = os.path.join(sim_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)

    times  = [d["time_sec"]   for d in decisions]
    flags  = [d["is_correct"] for d in decisions]

    # 1. Decision quality scatter
    fig, ax = plt.subplots(figsize=(12, 4))
    ax.scatter([t for t, c in zip(times, flags) if c],
               [1] * sum(flags), color="green", label="Correct", alpha=0.7, s=20)
    ax.scatter([t for t, c in zip(times, flags) if not c],
               [0] * sum(1 for c in flags if not c),
               color="red", label="Ping-pong", alpha=0.9, s=50, marker="x", linewidths=2)
    ax.set_xlabel("Simulation Time (s)")
    ax.set_yticks([0, 1])
    ax.set_yticklabels(["Ping-pong", "Correct"])
    ax.set_title(f"{sim_label} — GRU Decision Quality  (PP rate: {pp_rate}%,  Accuracy: {accuracy}%)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(plots_dir, "decision_quality.png"), dpi=150)
    plt.close(fig)
    print("  ✓ plots/decision_quality.png")

    # 2. Cumulative handovers
    fig, ax = plt.subplots(figsize=(12, 4))
    ax.plot(times, range(1, len(times) + 1), color="steelblue", linewidth=2)
    ax.set_xlabel("Simulation Time (s)")
    ax.set_ylabel("Cumulative Handovers")
    ax.set_title(f"{sim_label} — Cumulative Handovers Over Time  (total: {total})")
    fig.tight_layout()
    fig.savefig(os.path.join(plots_dir, "handovers_over_time.png"), dpi=150)
    plt.close(fig)
    print("  ✓ plots/handovers_over_time.png")

    # 3. Handovers per UE
    ue_counts = Counter(d["ue_id"] for d in decisions)
    ue_ids    = sorted(ue_counts.keys())
    fig, ax   = plt.subplots(figsize=(12, 4))
    ax.bar([str(u) for u in ue_ids], [ue_counts[u] for u in ue_ids],
           color="steelblue", edgecolor="black")
    ax.set_xlabel("UE ID")
    ax.set_ylabel("Handovers")
    ax.set_title(f"{sim_label} — Handovers per UE")
    fig.tight_layout()
    fig.savefig(os.path.join(plots_dir, "ho_per_ue.png"), dpi=150)
    plt.close(fig)
    print("  ✓ plots/ho_per_ue.png")

    # 4. HO activity (5s bins)
    import numpy as np
    bin_edges = np.arange(0, 61, 5)
    counts, _ = np.histogram(times, bins=bin_edges)
    fig, ax   = plt.subplots(figsize=(12, 4))
    ax.bar(bin_edges[:-1], counts, width=5, align="edge", color="coral", edgecolor="black")
    ax.set_xlabel("Simulation Time (s)")
    ax.set_ylabel("HOs in 5s window")
    ax.set_title(f"{sim_label} — Handover Activity Over Time")
    fig.tight_layout()
    fig.savefig(os.path.join(plots_dir, "ho_activity.png"), dpi=150)
    plt.close(fig)
    print("  ✓ plots/ho_activity.png")

    # ── decision_summary.json ─────────────────────────────────────────────────
    ts = os.path.basename(sim_dir)[7:22]  # extract timestamp from folder name
    summary_data = {
        "sim":               sim_label,
        "tag":               "gru_scenario",
        "timestamp":         ts,
        "total_handovers":   total,
        "pingpong_events":   pp_count,
        "pingpong_rate_pct": pp_rate,
        "correct_decisions": correct,
        "total_decisions":   total,
        "accuracy_pct":      accuracy,
    }
    with open(os.path.join(sim_dir, "decision_summary.json"), "w") as f:
        json.dump(summary_data, f, indent=2)
    print("  ✓ decision_summary.json")

    # ── summary.txt ───────────────────────────────────────────────────────────
    summary_txt = (
        f"Simulation Results — {sim_label} — {ts}\n"
        f"{'='*44}\n"
        f"Total handovers   : {total}\n"
        f"Ping-pong events  : {pp_count}\n"
        f"Ping-pong rate    : {pp_rate}%\n"
        f"GRU accuracy      : {accuracy}% ({correct}/{total} correct decisions)\n"
    )
    with open(os.path.join(sim_dir, "summary.txt"), "w") as f:
        f.write(summary_txt)
    print("  ✓ summary.txt")

    print(f"\n  DONE — {total} HOs | {pp_count} PPs | {pp_rate}% PP rate | {accuracy}% accuracy")
    return summary_data


if __name__ == "__main__":
    if len(sys.argv) > 1:
        sim_dir = sys.argv[1].rstrip("/")
    else:
        sim_dir = _latest_sim_dir()
        print(f"Auto-detected: {sim_dir}")

    generate(sim_dir)
