#!/usr/bin/env python3
"""
GRU xApp Post-Run Analysis
===========================
Reads ~/gru_decisions.csv and ~/lstm_features.csv after a simulation run
and computes the paper's key performance metrics:

  - Ping-pong reduction %
  - Total handover reduction %
  - Time-of-Stay gain %
  - Per-UE breakdown

Usage:
    python3 analyze_results.py [--decisions ~/gru_decisions.csv]
"""

import os
import sys
import argparse
from pathlib import Path

try:
    import pandas as pd
    import numpy as np
except ImportError:
    print("[!] Missing: pip install pandas numpy")
    sys.exit(1)

# ── Paper reference thresholds ───────────────────────────────────────────────
TOS_PP_TH   = 1.2     # s  — ToS below this = "short stay"
LEVEL_TH    = -110.0  # dBm — RSRP safety threshold

# ── Paper target numbers (from abstract) ─────────────────────────────────────
PAPER_PP_REDUCTION  = 98.15   # %
PAPER_HO_REDUCTION  = 46.25   # %
PAPER_TOS_GAIN      = 32.53   # %


def load_decisions(path: Path) -> pd.DataFrame:
    try:
        df = pd.read_csv(path)
        df.columns = df.columns.str.strip()
        return df
    except FileNotFoundError:
        print(f"[!] Not found: {path}")
        print("    Run the simulation first (600s), then re-run this script.")
        sys.exit(1)


def load_lstm_features(path: Path) -> pd.DataFrame | None:
    if not path.exists():
        return None
    try:
        df = pd.read_csv(path)
        df.columns = df.columns.str.strip()
        return df
    except Exception as e:
        print(f"[warn] Could not load lstm_features.csv: {e}")
        return None


def print_section(title: str):
    w = 60
    print("\n" + "═" * w)
    print(f"  {title}")
    print("═" * w)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--decisions",    default=str(Path.home() / "gru_decisions.csv"))
    parser.add_argument("--lstm",         default=str(Path.home() / "lstm_features.csv"))
    parser.add_argument("--no-reference", action="store_true",
                        help="Skip paper reference comparison")
    args = parser.parse_args()

    dec = load_decisions(Path(args.decisions))
    feat = load_lstm_features(Path(args.lstm))

    print("\n╔══════════════════════════════════════════════════════════╗")
    print("║     GRU xApp — Post-Run Performance Analysis             ║")
    print("╚══════════════════════════════════════════════════════════╝")
    print(f"\n  Decision log: {args.decisions}  ({len(dec)} rows)")

    # ── Normalise column names ────────────────────────────────────────────────
    dec["final_action"]  = dec["final_action"].str.strip()
    dec["gru_decision"]  = dec["gru_decision"].str.strip()

    # ── Basic counts ─────────────────────────────────────────────────────────
    total_a3      = len(dec)
    n_executed    = (dec["final_action"] == "EXECUTED").sum()
    n_blocked     = (dec["final_action"] == "BLOCKED").sum()
    n_no_pred     = (dec["gru_decision"] == "NO_PRED").sum()

    print_section("HANDOVER COUNTS")
    print(f"  A3 events evaluated      : {total_a3}")
    print(f"  Handovers EXECUTED       : {n_executed}")
    print(f"  Handovers BLOCKED (GRU)  : {n_blocked}")
    print(f"  (No prediction — SINR)   : {n_no_pred}")

    if total_a3 == 0:
        print("\n[!] No A3 events recorded. Is the simulation still running?")
        return

    # ── Block rate / HO reduction ─────────────────────────────────────────────
    ho_reduction_pct  = 100.0 * n_blocked / total_a3

    print_section("HANDOVER REDUCTION (Paper Algorithm 1 + 2)")
    print(f"  Handover reduction       : {ho_reduction_pct:.2f} %")
    if not args.no_reference:
        diff = ho_reduction_pct - PAPER_HO_REDUCTION
        sign = "+" if diff >= 0 else ""
        print(f"  Paper target             : {PAPER_HO_REDUCTION:.2f} %  "
              f"(Δ = {sign}{diff:.2f} pp)")

    # ── Ping-pong detection ───────────────────────────────────────────────────
    # Rows where GRU blocked AND predicted_tos < TOS_PP_TH
    pp_blocked = dec[(dec["final_action"] == "BLOCKED") &
                     (dec["predicted_tos"] < TOS_PP_TH)]
    n_pp_blocked = len(pp_blocked)

    # Baseline: how many A3 events would have been ping-pong WITHOUT GRU
    # (predicted_tos < threshold regardless of action)
    n_pp_candidates = (dec["predicted_tos"] < TOS_PP_TH).sum()
    pp_reduction_pct = (100.0 * n_pp_blocked / n_pp_candidates
                        if n_pp_candidates > 0 else 0.0)

    print_section("PING-PONG REDUCTION (Algorithm 1)")
    print(f"  PP candidates (ToS<{TOS_PP_TH}s) : {n_pp_candidates}")
    print(f"  PP handovers BLOCKED      : {n_pp_blocked}")
    print(f"  Ping-pong reduction       : {pp_reduction_pct:.2f} %")
    if not args.no_reference:
        diff = pp_reduction_pct - PAPER_PP_REDUCTION
        sign = "+" if diff >= 0 else ""
        print(f"  Paper target              : {PAPER_PP_REDUCTION:.2f} %  "
              f"(Δ = {sign}{diff:.2f} pp)")

    # ── Time-of-Stay analysis ─────────────────────────────────────────────────
    executed_dec = dec[dec["final_action"] == "EXECUTED"]
    blocked_dec  = dec[dec["final_action"] == "BLOCKED"]

    if len(executed_dec) > 0 and len(blocked_dec) > 0:
        mean_tos_executed = executed_dec["predicted_tos"].mean()
        mean_tos_blocked  = blocked_dec["predicted_tos"].mean()
        mean_tos_all      = dec["predicted_tos"].mean()
        # ToS gain: with GRU we skip short-stay handovers so avg ToS is higher
        tos_gain_pct = (100.0 * (mean_tos_executed - mean_tos_all) / mean_tos_all
                        if mean_tos_all > 0 else 0.0)
    else:
        mean_tos_executed = dec["predicted_tos"].mean()
        mean_tos_blocked  = 0.0
        tos_gain_pct = 0.0

    print_section("TIME-OF-STAY (ToS) ANALYSIS")
    print(f"  Mean predicted ToS (all)  : {dec['predicted_tos'].mean():.3f} s")
    print(f"  Mean ToS (executed HOs)   : {mean_tos_executed:.3f} s")
    print(f"  Mean ToS (blocked HOs)    : {mean_tos_blocked:.3f} s")
    if not args.no_reference:
        diff = tos_gain_pct - PAPER_TOS_GAIN
        sign = "+" if diff >= 0 else ""
        print(f"  Paper ToS gain target     : {PAPER_TOS_GAIN:.2f} %  "
              f"(Δ = {sign}{diff:.2f} pp)")

    # ── GRU confidence distribution ───────────────────────────────────────────
    print_section("GRU CONFIDENCE STATISTICS")
    pred_rows = dec[dec["gru_decision"] != "NO_PRED"]
    if len(pred_rows) > 0:
        print(f"  Predictions made          : {len(pred_rows)}")
        print(f"  Mean confidence           : {pred_rows['gru_confidence'].mean():.3f}")
        print(f"  Min / Max confidence      : "
              f"{pred_rows['gru_confidence'].min():.3f} / "
              f"{pred_rows['gru_confidence'].max():.3f}")
        print(f"  EXECUTE confidence (mean) : "
              f"{pred_rows[pred_rows['gru_decision']=='EXECUTE']['gru_confidence'].mean():.3f}")
        print(f"  AVOID confidence (mean)   : "
              f"{pred_rows[pred_rows['gru_decision']=='AVOID']['gru_confidence'].mean():.3f}")

    # ── Per-UE breakdown ─────────────────────────────────────────────────────
    print_section("PER-UE BREAKDOWN")
    print(f"  {'UE':>4}  {'A3':>4}  {'Exec':>5}  {'Block':>6}  "
          f"{'Block%':>7}  {'MeanToS':>8}  {'MeanSINR':>9}")
    print("  " + "-" * 52)
    for ue_id, g in dec.groupby("ue_id"):
        n      = len(g)
        ex     = (g["final_action"] == "EXECUTED").sum()
        bl     = (g["final_action"] == "BLOCKED").sum()
        bl_pct = 100.0 * bl / n if n > 0 else 0.0
        mtos   = g["predicted_tos"].mean()
        msinr  = g["serving_sinr"].mean()
        print(f"  {ue_id:>4}  {n:>4}  {ex:>5}  {bl:>6}  "
              f"{bl_pct:>6.1f}%  {mtos:>8.3f}s  {msinr:>8.2f}dB")

    # ── lstm_features analysis ────────────────────────────────────────────────
    if feat is not None:
        print_section("NS-3 FEATURE LOG SUMMARY")
        n_ues   = feat["IMSI"].nunique() if "IMSI" in feat.columns else "?"
        n_rows  = len(feat)
        t_start = feat["ElapsedTime"].min() if "ElapsedTime" in feat.columns else "?"
        t_end   = feat["ElapsedTime"].max() if "ElapsedTime" in feat.columns else "?"
        print(f"  Rows in lstm_features.csv : {n_rows}")
        print(f"  Unique UEs (IMSIs)        : {n_ues}")
        print(f"  Elapsed time range        : {t_start} → {t_end} s")
        if "Level" in feat.columns:
            print(f"  RSRP (Level) range        : "
                  f"{feat['Level'].min():.1f} → {feat['Level'].max():.1f} dBm")
        if "SNR" in feat.columns:
            print(f"  SNR range                 : "
                  f"{feat['SNR'].min():.1f} → {feat['SNR'].max():.1f} dB")

    # ── Summary ───────────────────────────────────────────────────────────────
    print_section("SUMMARY vs PAPER TARGETS")
    metrics = [
        ("Handover reduction", ho_reduction_pct, PAPER_HO_REDUCTION, "%"),
        ("Ping-pong reduction", pp_reduction_pct, PAPER_PP_REDUCTION, "%"),
    ]
    all_good = True
    for name, achieved, target, unit in metrics:
        diff = achieved - target
        status = "✓" if diff >= -5 else "↓ below target"
        if diff < -5:
            all_good = False
        sign = "+" if diff >= 0 else ""
        print(f"  {name:<24}: {achieved:6.2f}{unit}  "
              f"(target={target}{unit}, Δ={sign}{diff:.2f}pp) {status}")
    if all_good:
        print("\n  ✅ Results are consistent with the paper.")
    else:
        print("\n  ℹ  Results are below paper targets.")
        print("     This is expected for shorter simulations (<600s).")
        print("     Run the full 600s scenario for best results.")

    print("\n" + "═" * 60 + "\n")


if __name__ == "__main__":
    main()
