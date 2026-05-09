#!/bin/bash
# ─────────────────────────────────────────────────────────────────────
#  run-multi-scenario.sh — launch several /k8s/sim/launch runs in
#  sequence, then watch all the Jobs to completion. Useful for the
#  defense demo to populate the Grafana dashboard with multiple data
#  points across all three xApps.
#
#  Usage:
#      bash tools/run-multi-scenario.sh                 # default matrix
#      bash tools/run-multi-scenario.sh --parallel      # all at once
#      CTRL=http://localhost:8001 bash tools/...        # override URL
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

CTRL="${CTRL:-http://localhost:8001}"
PARALLEL=0
[ "${1:-}" = "--parallel" ] && PARALLEL=1

# Each line: scenario, xapp_id, sim_time, n_ues, n_mmwave
RUNS=(
    'gru_scenario             gru-handover   60   20   7'
    'gru_scenario             rl-handover    60   20   7'
    'gru_scenario             gru-handover   60   30   7'
    'load_balancing_scenario  lb-awf         60   20   8'
    'gru_scenario             rl-handover   120   20   7'
    'gru_scenario             gru-handover   60   15   7'
)

green()  { printf '\033[32m%s\033[0m\n' "$*"; }
yellow() { printf '\033[33m%s\033[0m\n' "$*"; }
cyan()   { printf '\033[36m%s\033[0m\n' "$*"; }

echo ""
cyan "═══════════════════════════════════════════════════════════════"
cyan "  Multi-scenario demo: ${#RUNS[@]} runs ($([ $PARALLEL -eq 1 ] && echo parallel || echo serial))"
cyan "═══════════════════════════════════════════════════════════════"
echo ""

# Sanity check
if ! curl -fsS "$CTRL/k8s/health" >/dev/null 2>&1; then
    yellow "✗ Controller not reachable at $CTRL"
    yellow "  Make sure: kubectl port-forward -n oran svc/oran-oran-controller 8001:8001 &"
    exit 1
fi

RUN_IDS=()
for line in "${RUNS[@]}"; do
    read -r scen xapp st nu nm <<<"$line"

    payload=$(printf '{"scenario":"%s","xapp_id":"%s","sim_time":%s,"n_ues":%s,"n_mmwave":%s}' \
        "$scen" "$xapp" "$st" "$nu" "$nm")

    printf "  → %-25s %-15s simTime=%-3s UEs=%-3s cells=%-3s ... " "$scen" "$xapp" "$st" "$nu" "$nm"
    resp=$(curl -sS -X POST "$CTRL/k8s/sim/launch" -H 'Content-Type: application/json' -d "$payload")
    rid=$(echo "$resp" | jq -r .run_id)
    if [ "$rid" = "null" ] || [ -z "$rid" ]; then
        yellow "FAILED: $resp"
        continue
    fi
    green "$rid"
    RUN_IDS+=("$rid")

    if [ $PARALLEL -eq 0 ]; then
        # Wait for both Jobs of this run to complete before starting the next.
        kubectl wait --for=condition=complete -n oran "job/sim-$rid"             --timeout=120s 2>/dev/null || true
        kubectl wait --for=condition=complete -n oran "job/xapp-$xapp-$rid"      --timeout=120s 2>/dev/null || true
    fi
done

echo ""
cyan "── Summary ──"
echo "Run IDs created: ${#RUN_IDS[@]}"
for rid in "${RUN_IDS[@]}"; do echo "  $rid"; done

if [ $PARALLEL -eq 1 ]; then
    echo ""
    yellow "Parallel mode — waiting for all Jobs to complete (up to 5 min)..."
    for rid in "${RUN_IDS[@]}"; do
        kubectl wait --for=condition=complete -n oran -l "oran.farouk/run-id=$rid" \
            --all --timeout=300s 2>/dev/null || true
    done
fi

echo ""
cyan "── Final Job state (all runs from this script) ──"
kubectl get jobs -n oran -l app.kubernetes.io/managed-by=oran-controller \
    -o custom-columns='NAME:.metadata.name,KIND:.metadata.labels.oran\.farouk/job-kind,XAPP:.metadata.labels.oran\.farouk/xapp-id,RUN_ID:.metadata.labels.oran\.farouk/run-id,SUCCEEDED:.status.succeeded,FAILED:.status.failed' \
    | head -30

echo ""
green "✅ Multi-scenario run complete."
echo ""
yellow "Open Grafana to see the populated panels:"
yellow "  Total simulations:        will show ${#RUN_IDS[@]} new runs"
yellow "  Handovers per minute:     will show 3 lines (one per xApp)"
yellow "  Decision accuracy:        will show 3 gauges with sim006-style values"
yellow "  xApp summary table:       3 rows (gru-handover, rl-handover, lb-awf)"
echo ""
