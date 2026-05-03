
  #!/usr/bin/env python3
"""
compare_xapp.py — Baseline vs Improved xApp Full Analysis
Suez Canal University — Inter-gNB Handover Project
Usage:
    python3 compare_xapp.py \
        --baseline_ho   ~/baseline/handover_xapp.csv \
        --baseline_sinr ~/baseline/sinr_xapp.csv \
        --improved_ho   ~/improved/handover_xapp.csv \
        --improved_sinr ~/improved/sinr_xapp.csv
"""

import argparse
import sys
import os
import warnings
warnings.filterwarnings("ignore")

import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.patches import FancyBboxPatch
from matplotlib.lines import Line2D
import matplotlib.ticker as mticker

# ─── Palette ──────────────────────────────────────────────────────────────────
C_BASE   = "#E05A2B"   # orange-red  → baseline
C_IMP    = "#2BBDE0"   # cyan-blue   → improved
C_BG     = "#0D1117"   # dark background
C_PANEL  = "#161B22"   # card background
C_GRID   = "#21262D"   # grid lines
C_TEXT   = "#E6EDF3"   # primary text
C_DIM    = "#8B949E"   # secondary text
C_GREEN  = "#3FB950"
C_YELLOW = "#D29922"
C_RED    = "#F85149"

plt.rcParams.update({
    "figure.facecolor":  C_BG,
    "axes.facecolor":    C_PANEL,
    "axes.edgecolor":    C_GRID,
    "axes.labelcolor":   C_TEXT,
    "axes.titlecolor":   C_TEXT,
    "xtick.color":       C_DIM,
    "ytick.color":       C_DIM,
    "text.color":        C_TEXT,
    "grid.color":        C_GRID,
    "grid.linewidth":    0.6,
    "legend.facecolor":  C_PANEL,
    "legend.edgecolor":  C_GRID,
    "font.family":       "monospace",
    "font.size":         9,
})

# ─── Helpers ──────────────────────────────────────────────────────────────────

def load_csv(path, label):
    """Load CSV, clean duplicate headers, force numeric types."""
    if not os.path.exists(path):
        print(f"  ✗  {label}: ملف مش موجود — {path}")
        return None
    try:
        df = pd.read_csv(path, dtype=str)
        # Drop rows where any value equals a column name (duplicate headers)
        header_mask = df.apply(lambda r: r.astype(str).isin(df.columns).any(), axis=1)
        df = df[~header_mask].copy()
        # Force numeric on all columns except string ones
        str_cols = {"event"} if "event" in df.columns else set()
        for col in df.columns:
            if col not in str_cols:
                df[col] = pd.to_numeric(df[col], errors="coerce")
        df.dropna(how="all", inplace=True)
        print(f"  ✓  {label}: {len(df)} rows  ({path})")
        return df
    except Exception as e:
        print(f"  ✗  {label}: فشل القراءة — {e}")
        return None


def calc_metrics(ho: pd.DataFrame, sinr: pd.DataFrame, name: str) -> dict:
    """Compute all KPIs from HO and SINR dataframes."""
    m = {"name": name}

    # ── HO counts ──
    completed   = ho[ho["event"].astype(str).str.strip() == "COMPLETED"].copy()
    cmd_sent    = ho[ho["event"].astype(str).str.strip() == "COMMAND_SENT"].copy()

    m["total_ho_commands"] = len(cmd_sent)
    m["total_ho_completed"]= len(completed)
    m["success_rate"]      = (100.0 * len(completed) / len(cmd_sent)
                               if len(cmd_sent) > 0 else 0.0)

    # ── Ping-pong ──
    pp = completed[completed["is_ping_pong"].astype(float) == 1]
    m["ping_pong_count"] = len(pp)
    m["ping_pong_rate"]  = (100.0 * len(pp) / len(completed)
                            if len(completed) > 0 else 0.0)

    # ── Latency ──
    lat = completed["latency_sec"].astype(float)
    valid_lat = lat[lat > 0]
    m["latency_mean"]   = valid_lat.mean()   if len(valid_lat) > 0 else 0.0
    m["latency_median"] = valid_lat.median() if len(valid_lat) > 0 else 0.0
    m["latency_p95"]    = valid_lat.quantile(0.95) if len(valid_lat) > 0 else 0.0
    m["latency_values"] = valid_lat.values

    # ── SINR ──
    sinr_vals = sinr["sinr_db"].astype(float).dropna()
    m["sinr_mean"]   = sinr_vals.mean()
    m["sinr_median"] = sinr_vals.median()
    m["sinr_std"]    = sinr_vals.std()
    m["sinr_values"] = sinr_vals.values

    # ── SINR per UE (mean) ──
    if "ue_id" in sinr.columns:
        ue_mean = sinr.groupby("ue_id")["sinr_db"].apply(
            lambda x: x.astype(float).mean()
        )
        m["sinr_per_ue"] = ue_mean
    else:
        m["sinr_per_ue"] = pd.Series(dtype=float)

    # ── HO timeline ──
    if "time_sec" in cmd_sent.columns:
        t0 = ho["time_sec"].astype(float).min()
        m["ho_times"] = (cmd_sent["time_sec"].astype(float) - t0).values
    else:
        m["ho_times"] = np.array([])

    # ── SINR timeline ──
    if "time_sec" in sinr.columns:
        t0s = sinr["time_sec"].astype(float).min()
        m["sinr_times"]  = (sinr["time_sec"].astype(float) - t0s).values
        m["sinr_series"] = sinr["sinr_db"].astype(float).values
    else:
        m["sinr_times"]  = np.array([])
        m["sinr_series"] = np.array([])

    # ── HO per UE ──
    if "ue_id" in completed.columns:
        m["ho_per_ue"] = completed.groupby("ue_id").size()
    else:
        m["ho_per_ue"] = pd.Series(dtype=int)

    # ── SINR before HO ──
    sinr_before = completed["serving_sinr_before"].astype(float).dropna()
    sinr_before = sinr_before[sinr_before != 0]
    m["sinr_before_ho"]  = sinr_before.values
    m["sinr_before_mean"]= sinr_before.mean() if len(sinr_before) > 0 else 0.0

    return m


def delta_str(base_val, imp_val, lower_is_better=False, pct=False):
    """Return colored delta string."""
    if base_val == 0:
        return ""
    diff = imp_val - base_val
    pct_val = 100.0 * diff / abs(base_val) if base_val != 0 else 0
    better = (diff < 0) if lower_is_better else (diff > 0)
    color = C_GREEN if better else C_RED
    sign = "+" if diff >= 0 else ""
    if pct:
        return f"{sign}{pct_val:.1f}%", color
    return f"{sign}{diff:.2f}", color


# ─── Plotting ─────────────────────────────────────────────────────────────────

def draw_kpi_card(ax, title, b_val, i_val, unit="", lower_better=False, fmt=".2f"):
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")

    ax.text(0.5, 0.88, title, ha="center", va="top",
            fontsize=8, color=C_DIM, fontweight="bold")

    b_str = f"{b_val:{fmt}}{unit}"
    i_str = f"{i_val:{fmt}}{unit}"

    ax.text(0.25, 0.52, b_str, ha="center", va="center",
            fontsize=13, color=C_BASE, fontweight="bold")
    ax.text(0.75, 0.52, i_str, ha="center", va="center",
            fontsize=13, color=C_IMP, fontweight="bold")

    ax.text(0.25, 0.22, "Baseline", ha="center", va="center",
            fontsize=7, color=C_DIM)
    ax.text(0.75, 0.22, "Improved", ha="center", va="center",
            fontsize=7, color=C_DIM)

    # delta arrow
    diff = i_val - b_val
    better = (diff < 0) if lower_better else (diff > 0)
    clr = C_GREEN if better else (C_RED if diff != 0 else C_DIM)
    sign = "+" if diff >= 0 else ""
    pct  = 100.0 * diff / abs(b_val) if b_val != 0 else 0
    ax.text(0.5, 0.52, f"{sign}{pct:.0f}%", ha="center", va="center",
            fontsize=9, color=clr, fontweight="bold")

    # bottom border line
    ax.add_line(Line2D([0.05, 0.95], [0.08, 0.08],
                       color=C_GRID, linewidth=0.8, transform=ax.transAxes))


def make_report(bm, im, out_path):
    fig = plt.figure(figsize=(20, 26), facecolor=C_BG)
    fig.suptitle(
        "Inter-gNB Handover xApp  ·  Baseline vs Improved  ·  Full Analysis",
        fontsize=16, fontweight="bold", color=C_TEXT, y=0.985
    )

    gs_main = gridspec.GridSpec(
        7, 1, figure=fig,
        hspace=0.55,
        top=0.97, bottom=0.03,
        left=0.06, right=0.97
    )

    # ══════════════════════════════════════════════════════
    # ROW 0 — KPI Cards (6 cards)
    # ══════════════════════════════════════════════════════
    gs0 = gridspec.GridSpecFromSubplotSpec(1, 6, subplot_spec=gs_main[0], wspace=0.05)
    kpis = [
        ("Total HO\nCommands",   bm["total_ho_commands"],  im["total_ho_commands"],  "",   False, ".0f"),
        ("HO Success\nRate (%)", bm["success_rate"],        im["success_rate"],        "%",  True,  ".1f"),
        ("Ping-Pong\nCount",     bm["ping_pong_count"],     im["ping_pong_count"],     "",   True,  ".0f"),
        ("Ping-Pong\nRate (%)",  bm["ping_pong_rate"],      im["ping_pong_rate"],      "%",  True,  ".1f"),
        ("Avg HO\nLatency (s)",  bm["latency_mean"],        im["latency_mean"],        "s",  True,  ".2f"),
        ("Avg SINR\n(dB)",       bm["sinr_mean"],           im["sinr_mean"],           "dB", False, ".2f"),
    ]
    for k, (title, bv, iv, unit, lb, fmt) in enumerate(kpis):
        ax = fig.add_subplot(gs0[k])
        ax.set_facecolor(C_PANEL)
        for spine in ax.spines.values():
            spine.set_edgecolor(C_GRID)
        draw_kpi_card(ax, title, bv, iv, unit, lb, fmt)

    # ══════════════════════════════════════════════════════
    # ROW 1 — HO Timeline  +  SINR Distribution
    # ══════════════════════════════════════════════════════
    gs1 = gridspec.GridSpecFromSubplotSpec(1, 2, subplot_spec=gs_main[1], wspace=0.35)

    # HO Timeline (cumulative)
    ax_hot = fig.add_subplot(gs1[0])
    for m_, color, lbl in [(bm, C_BASE, "Baseline"), (im, C_IMP, "Improved")]:
        t = np.sort(m_["ho_times"])
        if len(t):
            ax_hot.step(t, np.arange(1, len(t)+1), where="post",
                        color=color, lw=2, label=lbl)
    ax_hot.set_title("Cumulative HO Commands over Time", fontsize=10, pad=8)
    ax_hot.set_xlabel("Time (s)")
    ax_hot.set_ylabel("Cumulative HOs")
    ax_hot.legend(fontsize=8)
    ax_hot.grid(True, alpha=0.4)

    # SINR Distribution
    ax_sd = fig.add_subplot(gs1[1])
    bins = np.linspace(-50, 70, 60)
    for m_, color, lbl in [(bm, C_BASE, "Baseline"), (im, C_IMP, "Improved")]:
        ax_sd.hist(m_["sinr_values"], bins=bins, alpha=0.55,
                   color=color, label=f"{lbl} μ={m_['sinr_mean']:.1f}dB",
                   edgecolor="none", density=True)
        ax_sd.axvline(m_["sinr_mean"], color=color, lw=1.5, ls="--", alpha=0.85)
    ax_sd.set_title("SINR Distribution (all UEs, all time)", fontsize=10, pad=8)
    ax_sd.set_xlabel("SINR (dB)")
    ax_sd.set_ylabel("Density")
    ax_sd.legend(fontsize=8)
    ax_sd.grid(True, alpha=0.4)

    # ══════════════════════════════════════════════════════
    # ROW 2 — Latency CDF  +  SINR before HO
    # ══════════════════════════════════════════════════════
    gs2 = gridspec.GridSpecFromSubplotSpec(1, 2, subplot_spec=gs_main[2], wspace=0.35)

    # Latency CDF
    ax_lat = fig.add_subplot(gs2[0])
    for m_, color, lbl in [(bm, C_BASE, "Baseline"), (im, C_IMP, "Improved")]:
        v = np.sort(m_["latency_values"])
        if len(v):
            cdf = np.arange(1, len(v)+1) / len(v)
            ax_lat.plot(v, cdf, color=color, lw=2, label=lbl)
            ax_lat.axvline(np.median(v), color=color, lw=1, ls=":", alpha=0.7)
    ax_lat.set_title("HO Latency CDF  (COMMAND_SENT → COMPLETED)", fontsize=10, pad=8)
    ax_lat.set_xlabel("Latency (s)")
    ax_lat.set_ylabel("CDF")
    ax_lat.set_ylim(0, 1.05)
    ax_lat.legend(fontsize=8)
    ax_lat.grid(True, alpha=0.4)

    # SINR before HO decision
    ax_sbh = fig.add_subplot(gs2[1])
    bins2 = np.linspace(-40, 60, 50)
    for m_, color, lbl in [(bm, C_BASE, "Baseline"), (im, C_IMP, "Improved")]:
        v = m_["sinr_before_ho"]
        if len(v):
            ax_sbh.hist(v, bins=bins2, alpha=0.55, color=color,
                        label=f"{lbl} μ={m_['sinr_before_mean']:.1f}dB",
                        edgecolor="none", density=True)
            ax_sbh.axvline(np.mean(v), color=color, lw=1.5, ls="--")
    ax_sbh.set_title("Serving SINR at HO Decision Moment", fontsize=10, pad=8)
    ax_sbh.set_xlabel("SINR (dB)")
    ax_sbh.set_ylabel("Density")
    ax_sbh.legend(fontsize=8)
    ax_sbh.grid(True, alpha=0.4)

    # ══════════════════════════════════════════════════════
    # ROW 3 — SINR Timeline (smoothed avg)
    # ══════════════════════════════════════════════════════
    ax_st = fig.add_subplot(gs_main[3])
    for m_, color, lbl in [(bm, C_BASE, "Baseline"), (im, C_IMP, "Improved")]:
        t = m_["sinr_times"]
        s = m_["sinr_series"]
        if len(t) and len(s):
            # sort by time then rolling average
            idx = np.argsort(t)
            t_s, s_s = t[idx], s[idx]
            win = max(1, len(s_s) // 80)
            s_smooth = np.convolve(s_s, np.ones(win)/win, mode="same")
            ax_st.plot(t_s, s_s, color=color, alpha=0.12, lw=0.6)
            ax_st.plot(t_s, s_smooth, color=color, lw=2,
                       label=f"{lbl} (smoothed)")
    ax_st.set_title("SINR Timeline — All UEs (smoothed rolling average)", fontsize=10, pad=8)
    ax_st.set_xlabel("Time (s)")
    ax_st.set_ylabel("SINR (dB)")
    ax_st.axhline(0, color=C_DIM, lw=0.8, ls="--", alpha=0.5)
    ax_st.legend(fontsize=8)
    ax_st.grid(True, alpha=0.4)

    # ══════════════════════════════════════════════════════
    # ROW 4 — HO per UE (bar)  +  Avg SINR per UE (bar)
    # ══════════════════════════════════════════════════════
    gs4 = gridspec.GridSpecFromSubplotSpec(1, 2, subplot_spec=gs_main[4], wspace=0.35)

    # HO per UE
    ax_hu = fig.add_subplot(gs4[0])
    all_ues = sorted(set(bm["ho_per_ue"].index) | set(im["ho_per_ue"].index))
    x = np.arange(len(all_ues))
    w = 0.38
    b_vals = [bm["ho_per_ue"].get(u, 0) for u in all_ues]
    i_vals = [im["ho_per_ue"].get(u, 0) for u in all_ues]
    ax_hu.bar(x - w/2, b_vals, width=w, color=C_BASE, alpha=0.85, label="Baseline")
    ax_hu.bar(x + w/2, i_vals, width=w, color=C_IMP,  alpha=0.85, label="Improved")
    ax_hu.set_xticks(x)
    ax_hu.set_xticklabels([str(u) for u in all_ues], fontsize=7, rotation=45)
    ax_hu.set_title("Completed HOs per UE", fontsize=10, pad=8)
    ax_hu.set_xlabel("UE ID")
    ax_hu.set_ylabel("HO Count")
    ax_hu.legend(fontsize=8)
    ax_hu.grid(True, alpha=0.4, axis="y")

    # Avg SINR per UE
    ax_su = fig.add_subplot(gs4[1])
    all_ues2 = sorted(set(bm["sinr_per_ue"].index) | set(im["sinr_per_ue"].index))
    x2 = np.arange(len(all_ues2))
    b_sv = [bm["sinr_per_ue"].get(u, np.nan) for u in all_ues2]
    i_sv = [im["sinr_per_ue"].get(u, np.nan) for u in all_ues2]
    ax_su.bar(x2 - w/2, b_sv, width=w, color=C_BASE, alpha=0.85, label="Baseline")
    ax_su.bar(x2 + w/2, i_sv, width=w, color=C_IMP,  alpha=0.85, label="Improved")
    ax_su.set_xticks(x2)
    ax_su.set_xticklabels([str(u) for u in all_ues2], fontsize=7, rotation=45)
    ax_su.set_title("Average SINR per UE (dB)", fontsize=10, pad=8)
    ax_su.set_xlabel("UE ID")
    ax_su.set_ylabel("Avg SINR (dB)")
    ax_su.axhline(0, color=C_DIM, lw=0.8, ls="--", alpha=0.5)
    ax_su.legend(fontsize=8)
    ax_su.grid(True, alpha=0.4, axis="y")

    # ══════════════════════════════════════════════════════
    # ROW 5 — Latency Box  +  SINR Box
    # ══════════════════════════════════════════════════════
    gs5 = gridspec.GridSpecFromSubplotSpec(1, 2, subplot_spec=gs_main[5], wspace=0.35)

    ax_lb = fig.add_subplot(gs5[0])
    data_lat = [bm["latency_values"], im["latency_values"]]
    bp = ax_lb.boxplot(data_lat, patch_artist=True,
                       medianprops=dict(color=C_TEXT, lw=2),
                       whiskerprops=dict(color=C_DIM),
                       capprops=dict(color=C_DIM),
                       flierprops=dict(marker=".", color=C_DIM, alpha=0.4, ms=3))
    bp["boxes"][0].set_facecolor(C_BASE + "88")
    if len(bp["boxes"]) > 1:
        bp["boxes"][1].set_facecolor(C_IMP + "88")
    ax_lb.set_xticks([1, 2])
    ax_lb.set_xticklabels(["Baseline", "Improved"])
    ax_lb.set_title("HO Latency Distribution", fontsize=10, pad=8)
    ax_lb.set_ylabel("Latency (s)")
    ax_lb.grid(True, alpha=0.4, axis="y")

    ax_sb2 = fig.add_subplot(gs5[1])
    # sample to keep plot fast
    sv_b = bm["sinr_values"]
    sv_i = im["sinr_values"]
    if len(sv_b) > 3000: sv_b = np.random.choice(sv_b, 3000, replace=False)
    if len(sv_i) > 3000: sv_i = np.random.choice(sv_i, 3000, replace=False)
    bp2 = ax_sb2.boxplot([sv_b, sv_i], patch_artist=True,
                          medianprops=dict(color=C_TEXT, lw=2),
                          whiskerprops=dict(color=C_DIM),
                          capprops=dict(color=C_DIM),
                          flierprops=dict(marker=".", color=C_DIM, alpha=0.2, ms=2))
    bp2["boxes"][0].set_facecolor(C_BASE + "88")
    if len(bp2["boxes"]) > 1:
        bp2["boxes"][1].set_facecolor(C_IMP + "88")
    ax_sb2.set_xticks([1, 2])
    ax_sb2.set_xticklabels(["Baseline", "Improved"])
    ax_sb2.set_title("Overall SINR Distribution", fontsize=10, pad=8)
    ax_sb2.set_ylabel("SINR (dB)")
    ax_sb2.grid(True, alpha=0.4, axis="y")

    # ══════════════════════════════════════════════════════
    # ROW 6 — Summary Text Table
    # ══════════════════════════════════════════════════════
    ax_tbl = fig.add_subplot(gs_main[6])
    ax_tbl.axis("off")

    rows = [
        ("Metric", "Baseline", "Improved", "Delta", "Better?"),
        ("─"*28, "─"*12, "─"*12, "─"*10, "─"*8),
        ("Total HO Commands",
         f"{bm['total_ho_commands']}",
         f"{im['total_ho_commands']}",
         f"{im['total_ho_commands']-bm['total_ho_commands']:+d}",
         "—"),
        ("HO Success Rate",
         f"{bm['success_rate']:.1f}%",
         f"{im['success_rate']:.1f}%",
         f"{im['success_rate']-bm['success_rate']:+.1f}%",
         "✓ Improved" if im['success_rate'] >= bm['success_rate'] else "✗ Worse"),
        ("Ping-Pong Count",
         f"{bm['ping_pong_count']}",
         f"{im['ping_pong_count']}",
         f"{im['ping_pong_count']-bm['ping_pong_count']:+d}",
         "✓ Improved" if im['ping_pong_count'] <= bm['ping_pong_count'] else "✗ Worse"),
        ("Ping-Pong Rate",
         f"{bm['ping_pong_rate']:.1f}%",
         f"{im['ping_pong_rate']:.1f}%",
         f"{im['ping_pong_rate']-bm['ping_pong_rate']:+.1f}%",
         "✓ Improved" if im['ping_pong_rate'] <= bm['ping_pong_rate'] else "✗ Worse"),
        ("Avg HO Latency",
         f"{bm['latency_mean']:.3f}s",
         f"{im['latency_mean']:.3f}s",
         f"{im['latency_mean']-bm['latency_mean']:+.3f}s",
         "✓ Improved" if im['latency_mean'] <= bm['latency_mean'] else "✗ Worse"),
        ("Median HO Latency",
         f"{bm['latency_median']:.3f}s",
         f"{im['latency_median']:.3f}s",
         f"{im['latency_median']-bm['latency_median']:+.3f}s",
         "✓ Improved" if im['latency_median'] <= bm['latency_median'] else "✗ Worse"),
        ("P95 HO Latency",
         f"{bm['latency_p95']:.3f}s",
         f"{im['latency_p95']:.3f}s",
         f"{im['latency_p95']-bm['latency_p95']:+.3f}s",
         "✓ Improved" if im['latency_p95'] <= bm['latency_p95'] else "✗ Worse"),
        ("Avg SINR (all UEs)",
         f"{bm['sinr_mean']:.2f}dB",
         f"{im['sinr_mean']:.2f}dB",
         f"{im['sinr_mean']-bm['sinr_mean']:+.2f}dB",
         "✓ Improved" if im['sinr_mean'] >= bm['sinr_mean'] else "✗ Worse"),
        ("SINR Std Dev",
         f"{bm['sinr_std']:.2f}dB",
         f"{im['sinr_std']:.2f}dB",
         f"{im['sinr_std']-bm['sinr_std']:+.2f}dB",
         "✓ Improved" if im['sinr_std'] <= bm['sinr_std'] else "✗ Worse"),
        ("Avg SINR before HO",
         f"{bm['sinr_before_mean']:.2f}dB",
         f"{im['sinr_before_mean']:.2f}dB",
         f"{im['sinr_before_mean']-bm['sinr_before_mean']:+.2f}dB",
         "✓ Improved" if im['sinr_before_mean'] >= bm['sinr_before_mean'] else "✗ Worse"),
    ]

    col_x = [0.01, 0.32, 0.52, 0.72, 0.87]
    y_start = 0.97
    row_h   = 0.085

    for ri, row in enumerate(rows):
        y = y_start - ri * row_h
        is_header = ri == 0
        is_sep    = ri == 1
        for ci, (cell, cx) in enumerate(zip(row, col_x)):
            if is_sep:
                color = C_GRID
                fs = 7
            elif is_header:
                color = C_TEXT
                fs = 9
            else:
                if ci == 4:
                    color = C_GREEN if "✓" in cell else (C_RED if "✗" in cell else C_DIM)
                elif ci == 3:
                    try:
                        v = float(cell.replace("%","").replace("s","").replace("dB","").replace("+",""))
                        color = C_GREEN if v <= 0 else C_RED
                    except:
                        color = C_DIM
                elif ci == 1:
                    color = C_BASE
                elif ci == 2:
                    color = C_IMP
                else:
                    color = C_TEXT
                fs = 8
            ax_tbl.text(cx, y, cell, transform=ax_tbl.transAxes,
                        fontsize=fs if not is_header else 9,
                        color=color,
                        fontweight="bold" if is_header else "normal",
                        va="top", fontfamily="monospace")

    ax_tbl.set_title("Summary Table", fontsize=10, pad=8, loc="left")

    # Legend patches top-right
    from matplotlib.patches import Patch
    legend_els = [Patch(facecolor=C_BASE, label="Baseline"),
                  Patch(facecolor=C_IMP,  label="Improved")]
    fig.legend(handles=legend_els, loc="upper right",
               bbox_to_anchor=(0.97, 0.985), fontsize=9,
               framealpha=0.3, edgecolor=C_GRID)

    plt.savefig(out_path, dpi=150, bbox_inches="tight",
                facecolor=C_BG, edgecolor="none")
    print(f"\n  ✓  Report saved → {out_path}")
    return fig


# ─── Terminal Summary ─────────────────────────────────────────────────────────

def print_summary(bm, im):
    W = 70
    print("\n" + "═"*W)
    print(f"{'COMPARISON SUMMARY':^{W}}")
    print("═"*W)
    print(f"{'Metric':<30} {'Baseline':>12} {'Improved':>12} {'Delta':>10}")
    print("─"*W)

    rows = [
        ("Total HO Commands",    bm['total_ho_commands'],  im['total_ho_commands'],  False, ".0f", ""),
        ("HO Success Rate",      bm['success_rate'],        im['success_rate'],        True,  ".1f", "%"),
        ("Ping-Pong Count",      bm['ping_pong_count'],     im['ping_pong_count'],     True,  ".0f", ""),
        ("Ping-Pong Rate",       bm['ping_pong_rate'],      im['ping_pong_rate'],      True,  ".1f", "%"),
        ("Avg Latency",          bm['latency_mean'],        im['latency_mean'],        True,  ".3f", "s"),
        ("Median Latency",       bm['latency_median'],      im['latency_median'],      True,  ".3f", "s"),
        ("P95 Latency",          bm['latency_p95'],         im['latency_p95'],         True,  ".3f", "s"),
        ("Avg SINR",             bm['sinr_mean'],           im['sinr_mean'],           False, ".2f", "dB"),
        ("SINR Std Dev",         bm['sinr_std'],            im['sinr_std'],            True,  ".2f", "dB"),
        ("Avg SINR before HO",   bm['sinr_before_mean'],   im['sinr_before_mean'],    False, ".2f", "dB"),
    ]

    GREEN = "\033[92m"
    RED   = "\033[91m"
    RESET = "\033[0m"
    DIM   = "\033[2m"

    for label, bv, iv, lower_better, fmt, unit in rows:
        diff = iv - bv
        better = (diff < 0) if lower_better else (diff > 0)
        clr = GREEN if better else (RED if diff != 0 else DIM)
        sign = "+" if diff >= 0 else ""
        print(f"  {label:<28} {bv:{fmt}}{unit:>3}  {iv:{fmt}}{unit:>3}  "
              f"{clr}{sign}{diff:{fmt}}{unit}{RESET}")

    print("═"*W)


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="xApp Baseline vs Improved Comparison")
    ap.add_argument("--baseline_ho",   required=True)
    ap.add_argument("--baseline_sinr", required=True)
    ap.add_argument("--improved_ho",   required=True)
    ap.add_argument("--improved_sinr", required=True)
    ap.add_argument("--out", default=os.path.expanduser("~/xapp_comparison.png"),
                    help="Output image path (default: ~/xapp_comparison.png)")
    args = ap.parse_args()

    print("\n─── تحميل الملفات ───────────────────────────────────────")
    bho   = load_csv(args.baseline_ho,   "Baseline HO")
    bsinr = load_csv(args.baseline_sinr, "Baseline SINR")
    iho   = load_csv(args.improved_ho,   "Improved HO")
    isinr = load_csv(args.improved_sinr, "Improved SINR")

    if any(x is None for x in [bho, bsinr, iho, isinr]):
        print("\n  ✗  ملف ناقص — تأكد من الـ paths.")
        sys.exit(1)

    print("\n─── حساب الـ Metrics ────────────────────────────────────")
    bm = calc_metrics(bho, bsinr, "Baseline")
    im = calc_metrics(iho, isinr, "Improved")

    print_summary(bm, im)

    print("\n─── رسم الـ Report ──────────────────────────────────────")
    make_report(bm, im, args.out)
    print(f"\n  شغّل:  eog {args.out}  أو  xdg-open {args.out}\n")


if __name__ == "__main__":
    main()
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    python3 ~/compare_xapp.py \
  --baseline_ho   ~/baseline/handover_xapp_clean.csv \
  --baseline_sinr ~/baseline/sinr_xapp.csv \
  --improved_ho   ~/improved/handover_xapp.csv \
  --improved_sinr ~/improved/sinr_xapp.csv && xdg-open ~/xapp_comparison.png
