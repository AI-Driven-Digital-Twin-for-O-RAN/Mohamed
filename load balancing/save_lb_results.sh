#!/bin/bash
# save_lb_results.sh — saves Load Balancing simulation results
# Usage: ./save_lb_results.sh [label]

set -e

LABEL="${1:-}"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
BASE="/home/omar_farouk/open-ran-clean/load balancing/sim_results"

NEXT_NUM=$(ls -d "$BASE"/[0-9]*/ 2>/dev/null | wc -l)
NEXT_NUM=$((NEXT_NUM + 1))
FOLDER_NAME="${NEXT_NUM}_${TIMESTAMP}${LABEL:+_$LABEL}"
DEST="$BASE/$FOLDER_NAME"
mkdir -p "$DEST"

FILES=(
    "$HOME/load_balance_kpis.csv"
    "$HOME/lb_handover_xapp.csv"
    "$HOME/handover.csv"
    "$HOME/kpm_handover_features.csv"
)

echo "Saving LB simulation results to: $DEST"
for f in "${FILES[@]}"; do
    if [ -f "$f" ] && [ -s "$f" ]; then
        cp "$f" "$DEST/"
        lines=$(wc -l < "$f")
        echo "  ✓ $(basename $f)  ($lines lines)"
    else
        echo "  ✗ $(basename $f)  (not found or empty)"
    fi
done

# Copy xApp log
[ -f /tmp/xapp_lb.log ] && cp /tmp/xapp_lb.log "$DEST/xapp_lb.log" && echo "  ✓ xapp_lb.log"

# Metadata + analysis
cat > "$DEST/info.txt" <<EOF
Simulation saved: $(date)
Label: ${LABEL:-none}
Folder: $FOLDER_NAME

load_balance_kpis.csv lines:  $(wc -l < "$HOME/load_balance_kpis.csv" 2>/dev/null || echo 0)
lb_handover_xapp.csv lines:   $(wc -l < "$HOME/lb_handover_xapp.csv" 2>/dev/null || echo 0)
handover.csv lines:           $(wc -l < "$HOME/handover.csv" 2>/dev/null || echo 0)
EOF

# Load balancing analysis
python3 - "$DEST" >> "$DEST/info.txt" 2>/dev/null <<'PYEOF'
import csv, sys, os
from collections import defaultdict

d = sys.argv[1]

# Handover analysis
ho_file = os.path.join(d, "handover.csv")
if os.path.exists(ho_file):
    rows = list(csv.DictReader(open(ho_file)))
    total = len(rows)
    ok = sum(1 for r in rows if r.get('executed_ok','') == '1')
    # Ping-pong: UE goes A→B→A within 30s (slow pedestrians)
    last = {}
    pp = 0
    for r in rows:
        uid, t, frm, to = r['ue_id'], float(r['time_sec']), r['from_cell'], r['to_cell']
        if uid in last:
            lt, lf, _ = last[uid]
            if to == lf and (t - lt) < 30.0:
                pp += 1
        last[uid] = (t, frm, to)
    pp_rate = 100.0 * pp / total if total else 0
    print(f"\n=== Handover Summary ===")
    print(f"Total HOs: {total}   Successful: {ok}   Ping-pong: {pp} ({pp_rate:.1f}%)")

# Cell load analysis from KPI CSV
kpi_file = os.path.join(d, "load_balance_kpis.csv")
if os.path.exists(kpi_file):
    cell_ues = defaultdict(list)
    last_time = 0
    for row in csv.DictReader(open(kpi_file)):
        try:
            cell_ues[int(row['cell_id'])].append(int(row['ue_count']))
            last_time = float(row['time_sec'])
        except:
            pass
    print(f"\n=== Cell Load Distribution (avg UE count over {last_time:.0f}s) ===")
    for cid in sorted(cell_ues):
        avg = sum(cell_ues[cid]) / len(cell_ues[cid])
        final = cell_ues[cid][-1] if cell_ues[cid] else 0
        print(f"  Cell {cid}: avg={avg:.1f} UEs  final={final} UEs")
PYEOF

echo ""
echo "Done → $DEST"
echo "List all: ls \"$BASE\""
