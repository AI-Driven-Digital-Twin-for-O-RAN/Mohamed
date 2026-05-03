#!/bin/bash
# ============================================================
# save_sim_results.sh
# Saves the current simulation's CSV output files into a
# timestamped folder under ~/sim_results/.
#
# Usage:
#   ./save_sim_results.sh [optional_label]
#
# Files collected (from $HOME/):
#   handover.csv              — per-handover event log
#   lstm_features.csv         — 12-feature GRU input per UE per 0.25s
#   kpm_handover_features.csv — 18-feature KPM log
# ============================================================

set -e

LABEL="${1:-}"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
if [ -n "$LABEL" ]; then
    FOLDER_NAME="${TIMESTAMP}_${LABEL}"
else
    FOLDER_NAME="${TIMESTAMP}"
fi

# Auto-number: count existing sim folders and prefix with next number
NEXT_NUM=$(ls -d "$HOME/sim_results"/[0-9]*/ 2>/dev/null | wc -l)
NEXT_NUM=$((NEXT_NUM + 1))
FOLDER_NAME="${NEXT_NUM}_${FOLDER_NAME}"

DEST="$HOME/sim_results/${FOLDER_NAME}"
mkdir -p "${DEST}"

FILES=(
    "$HOME/handover.csv"
    "$HOME/lstm_features.csv"
    "$HOME/kpm_handover_features.csv"
)

echo "Saving simulation results to: ${DEST}"
for f in "${FILES[@]}"; do
    if [ -f "$f" ] && [ -s "$f" ]; then
        cp "$f" "${DEST}/"
        lines=$(wc -l < "$f")
        echo "  ✓ $(basename $f)  ($lines lines)"
    else
        echo "  ✗ $(basename $f)  (not found or empty)"
    fi
done

# Write a short metadata file
cat > "${DEST}/info.txt" <<EOF
Simulation saved: $(date)
Label: ${LABEL:-none}
Folder: ${FOLDER_NAME}

handover.csv lines:              $(wc -l < "$HOME/handover.csv" 2>/dev/null || echo 0)
lstm_features.csv lines:         $(wc -l < "$HOME/lstm_features.csv" 2>/dev/null || echo 0)
kpm_handover_features.csv lines: $(wc -l < "$HOME/kpm_handover_features.csv" 2>/dev/null || echo 0)
EOF

# Quick ping-pong analysis on handover.csv if it has data
HO_FILE="${DEST}/handover.csv"
if [ -f "$HO_FILE" ] && [ "$(wc -l < "$HO_FILE")" -gt 1 ]; then
    total=$(tail -n +2 "$HO_FILE" | wc -l)
    echo "" >> "${DEST}/info.txt"
    echo "=== Handover Summary ===" >> "${DEST}/info.txt"
    echo "Total handover events: $total" >> "${DEST}/info.txt"
    python3 - "$HO_FILE" >> "${DEST}/info.txt" 2>/dev/null <<'PYEOF'
import csv, sys
from collections import defaultdict

file = sys.argv[1]
rows = []
with open(file) as f:
    for r in csv.DictReader(f):
        rows.append(r)

if not rows:
    print("No data")
    sys.exit(0)

# Ping-pong: same UE goes A->B->A within 5s
# Track last HO time and previous cell per UE
last_ho = {}    # ue_id -> (time_sec, from_cell, to_cell)
pp_count = 0
for r in rows:
    uid = r['ue_id']
    t   = float(r['time_sec'])
    frm = r['from_cell']
    to  = r['to_cell']
    if uid in last_ho:
        lt, lf, lt_cell = last_ho[uid]
        if to == lf and (t - lt) < 5.0:
            pp_count += 1
    last_ho[uid] = (t, frm, to)

total = len(rows)
pp_rate = 100.0 * pp_count / total if total > 0 else 0.0
print(f"Ping-pong handovers: {pp_count} / {total}  ({pp_rate:.1f}%)")
print(f"Successful handovers: {sum(1 for r in rows if r.get('executed_ok','')=='1')}")
PYEOF
fi

echo ""
echo "Done. Results in: ${DEST}"
echo "List all sims: ls ~/sim_results/"
