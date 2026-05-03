#!/bin/bash
# ============================================================
# save_lb_results.sh
# Saves the Load Balancing xApp simulation results into a
# numbered, timestamped folder under ~/lb_sim_results/.
#
# Usage:
#   ./save_lb_results.sh [label]
#   ./save_lb_results.sh lb001_baseline_60s
#
# Files collected (from $HOME/):
#   alyaadone.csv         — full HO log with all paper metrics (Eq.1-14)
#   sinr_xapp.csv         — per-UE SINR snapshots per KPM cycle
#   handover.csv          — ns-3 HO events (START/SUCCESS/FAILURE)
#   lstm_features.csv     — 14-feature KPM log from ns-3
#   kpm_handover_features.csv — 18-feature KPM
#   energyfilecell*.csv   — per-cell energy consumption
# ============================================================

set -e

LABEL="${1:-}"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULTS_DIR="$HOME/lb_sim_results"
mkdir -p "${RESULTS_DIR}"

# Auto-number based on existing folders
NEXT_NUM=$(ls -d "${RESULTS_DIR}"/[0-9]*/ 2>/dev/null | wc -l)
NEXT_NUM=$((NEXT_NUM + 1))
FOLDER_NAME=$(printf "%03d_%s" "${NEXT_NUM}" "${TIMESTAMP}")
[ -n "${LABEL}" ] && FOLDER_NAME="${FOLDER_NAME}_${LABEL}"

DEST="${RESULTS_DIR}/${FOLDER_NAME}"
mkdir -p "${DEST}"

echo "═══════════════════════════════════════════════════════"
echo "  LB Simulation Results Save"
echo "  Destination: ${DEST}"
echo "═══════════════════════════════════════════════════════"

# ── Core CSV files ────────────────────────────────────────────────────
FILES=(
    "$HOME/alyaadone.csv"
    "$HOME/sinr_xapp.csv"
    "$HOME/handover.csv"
    "$HOME/lstm_features.csv"
    "$HOME/kpm_handover_features.csv"
)

for f in "${FILES[@]}"; do
    if [ -f "$f" ] && [ -s "$f" ]; then
        cp "$f" "${DEST}/"
        lines=$(wc -l < "$f")
        echo "  ✓ $(basename $f)  ($lines lines)"
    else
        echo "  ✗ $(basename $f)  (missing or empty)"
    fi
done

# ── Energy files (one per cell, 7 cells) ─────────────────────────────
ENERGY_COUNT=0
for ef in "$HOME"/energyfilecell*.csv; do
    [ -f "$ef" ] || continue
    cp "$ef" "${DEST}/"
    ENERGY_COUNT=$((ENERGY_COUNT + 1))
done
[ "${ENERGY_COUNT}" -gt 0 ] && echo "  ✓ energyfilecell*.csv  (${ENERGY_COUNT} files)"

# ── Paper metrics analysis ────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════"
echo "  Paper Metrics Analysis (Gures et al. ICT Express 2023)"
echo "═══════════════════════════════════════════════════════"

python3 - "${DEST}" >> "${DEST}/metrics_report.txt" 2>/dev/null <<'PYEOF'
import csv, sys, os, math
from collections import defaultdict

dest = sys.argv[1]

def load_csv(name):
    path = os.path.join(dest, name)
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return list(csv.DictReader(f))

# ── alyaadone.csv analysis ────────────────────────────────────────────
rows = load_csv("alyaadone.csv")
ho_events = [r for r in rows if r.get("event") == "COMMAND_SENT"]
completed  = [r for r in rows if r.get("event") == "COMPLETED" and r.get("ho_success") == "1"]

print("=" * 60)
print("  LOAD BALANCING METRICS REPORT")
print(f"  Total HO events logged: {len(rows)}")
print(f"  HO commands issued:     {len(ho_events)}")
print(f"  Successful completions: {len(completed)}")
print("=" * 60)

if len(ho_events) == 0:
    print("\n[!] No handover events found. Simulation may not have run long enough.")
    sys.exit(0)

# ── 1. CDR (Call Drop Ratio) ──────────────────────────────────────────
failure_rows = [r for r in rows if r.get("event") == "COMMAND_SENT"]
total_ho = len(ho_events)
# CDR = failed HOs / total HOs (approximate from ns-3 handover.csv)
handover_rows = load_csv("handover.csv")
ho_start   = [r for r in handover_rows if r.get("event") == "START"]
ho_success = [r for r in handover_rows if r.get("event") == "SUCCESS"]
ho_fail    = [r for r in handover_rows if r.get("event") == "FAILURE"]
cdr = len(ho_fail) / len(ho_start) if len(ho_start) > 0 else 0.0
print(f"\n[1] CDR (Call Drop Ratio)")
print(f"    HO start:   {len(ho_start)}")
print(f"    HO success: {len(ho_success)}")
print(f"    HO failure: {len(ho_fail)}")
print(f"    CDR:        {cdr:.4f}  ({cdr*100:.2f}%)")
print(f"    Paper best: 0.0078 @ 40 km/h")

# ── 2. Ping-Pong Rate ─────────────────────────────────────────────────
PP_WINDOW = 8.0   # sim-seconds — matches PING_PONG_WINDOW_SIM_SEC in xApp v4
last_ho_per_ue = {}
pp_count = 0
for r in sorted(ho_events, key=lambda x: float(x.get("sim_time_sec", 0))):
    uid  = r.get("ue_id", "0")
    t    = float(r.get("sim_time_sec", 0))
    frm  = r.get("from_cell", "")
    to   = r.get("to_cell", "")
    if uid in last_ho_per_ue:
        lt, lfrm, lto = last_ho_per_ue[uid]
        if to == lfrm and (t - lt) < PP_WINDOW:
            pp_count += 1
    last_ho_per_ue[uid] = (t, frm, to)
pp_rate = 100.0 * pp_count / total_ho if total_ho > 0 else 0.0
print(f"\n[2] Ping-Pong Rate  (window={PP_WINDOW}s)")
print(f"    PP events: {pp_count} / {total_ho}  ({pp_rate:.1f}%)")

# ── 3. Per-Cell Load (NUE per cell) ──────────────────────────────────
sinr_rows = load_csv("sinr_xapp.csv")
if sinr_rows:
    cell_ues = defaultdict(set)
    for r in sinr_rows:
        cell_ues[r.get("cell_id","0")].add(r.get("ue_id","0"))
    loads = [len(v) for v in cell_ues.values()]
    avg_load = sum(loads) / len(loads) if loads else 0
    # Load variance
    mean_l = avg_load
    variance = sum((l - mean_l)**2 for l in loads) / len(loads) if loads else 0
    print(f"\n[3] Cell Load Distribution (from SINR snapshots)")
    for cid, ues in sorted(cell_ues.items(), key=lambda x: int(x[0]) if x[0].isdigit() else 0):
        print(f"    Cell {cid:>3}: {len(ues):2d} UEs seen")
    print(f"    Avg load:     {avg_load:.2f} UEs/cell")
    print(f"    Load variance:{variance:.4f}")
    print(f"    Paper best:   avg~0.38, variance~0.04 @ 40 km/h")

# ── 4. AWF Decision Quality ───────────────────────────────────────────
if ho_events:
    try:
        fwf_vals = [float(r["fWF_Eq1"]) for r in ho_events if r.get("fWF_Eq1","")]
        ncl_pass = sum(1 for r in ho_events if "PASSED" in r.get("NCL_gate_result",""))
        lb_good  = sum(1 for r in ho_events if "LOAD_REDUCED" in r.get("load_balance_result",""))
        if fwf_vals:
            print(f"\n[4] AWF Decision Quality")
            print(f"    Avg fWF score:    {sum(fwf_vals)/len(fwf_vals):+.4f}")
            print(f"    NCL gate passed:  {ncl_pass}/{len(ho_events)}")
            print(f"    Load reduced HOs: {lb_good}/{len(ho_events)}")
    except (KeyError, ValueError):
        pass

# ── 5. Throughput (from alyaadone.csv — approximate) ─────────────────
print(f"\n[5] Note: Throughput and spectral efficiency require full")
print(f"    sinr_xapp.csv + lstm_features.csv analysis.")
print(f"    Run longer simulation (600s) for accurate throughput metrics.")

print("\n" + "=" * 60)
PYEOF

# print the report to terminal too
if [ -f "${DEST}/metrics_report.txt" ]; then
    cat "${DEST}/metrics_report.txt"
fi

# ── info.txt metadata ─────────────────────────────────────────────────
cat > "${DEST}/info.txt" <<EOF
Simulation saved : $(date)
Label            : ${LABEL:-none}
Folder           : ${FOLDER_NAME}
Paper            : Gures et al. ICT Express 2023 — AWF Load Balancing

File sizes:
$(for f in alyaadone.csv sinr_xapp.csv handover.csv lstm_features.csv kpm_handover_features.csv; do
    p="$HOME/$f"
    [ -f "$p" ] && printf "  %-36s %s lines\n" "$f" "$(wc -l < "$p")" || printf "  %-36s MISSING\n" "$f"
done)
EOF

echo ""
echo "  Saved to : ${DEST}"
echo "  List all : ls ~/lb_sim_results/"
echo "═══════════════════════════════════════════════════════"
