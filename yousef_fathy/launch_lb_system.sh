#!/bin/bash
# ══════════════════════════════════════════════════════════════════════
#  AWF Load Balancing xApp System Launcher
#  Paper: Gures et al. ICT Express 2023 — "Load balancing in 5G HetNets
#         based on automatic weight function"
#  Scenario: lb_awf_scenario (7 mmWave gNBs, 1 LTE eNB, 20 UEs)
#  Stack: nearRT-RIC  +  ns-3 (lb_awf_scenario)  +  xapp_lb_awf
# ══════════════════════════════════════════════════════════════════════
# Usage:
#   ./launch_lb_system.sh           → 60s simulation (quick test)
#   ./launch_lb_system.sh 600       → 600s simulation (full baseline)
#   ./launch_lb_system.sh 300 lb_run1  → 300s + auto-save with label
# ══════════════════════════════════════════════════════════════════════

set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLEXRIC_BUILD="${ROOT}/flexric/build"
NS3_DIR="${ROOT}/ns-O-RAN-flexric/mmwave-LENA-oran"
RIC_BIN="${FLEXRIC_BUILD}/examples/ric/nearRT-RIC"
XAPP_BIN="${FLEXRIC_BUILD}/examples/xApp/c/lb_awf/xapp_lb_awf"
NS3_SCENARIO="scratch/lb_awf_scenario"
SAVE_SCRIPT="${ROOT}/save_lb_results.sh"

SIM_TIME="${1:-60}"      # default 60s for quick test; use 600 for full baseline
SAVE_LABEL="${2:-}"      # optional: auto-save results with this label after run

# ── Sanity checks ────────────────────────────────────────────────────
for bin in "${RIC_BIN}" "${XAPP_BIN}"; do
    if [[ ! -x "${bin}" ]]; then
        echo "[ERROR] Binary not found or not executable: ${bin}"
        echo "  Build: cd ${FLEXRIC_BUILD} && cmake --build . --target xapp_lb_awf -j\$(nproc)"
        exit 1
    fi
done
if [[ ! -f "${NS3_DIR}/ns3" ]]; then
    echo "[ERROR] ns3 script not found: ${NS3_DIR}/ns3"
    exit 1
fi

echo "══════════════════════════════════════════════════════════════════"
echo "  AWF Load Balancing xApp System"
echo "  Scenario : lb_awf_scenario (7 gNBs, 20 UEs, bounded mobility)"
echo "  Sim time : ${SIM_TIME}s"
echo "  Outputs  : ~/alyaadone.csv  ~/sinr_xapp.csv  ~/handover.csv"
[[ -n "${SAVE_LABEL}" ]] && echo "  Auto-save: ~/sim_results/ with label '${SAVE_LABEL}'"
echo "══════════════════════════════════════════════════════════════════"

# ── Kill any stale processes ─────────────────────────────────────────
pkill -9 -f xapp_lb_awf 2>/dev/null || true
pkill -9 -f nearRT-RIC  2>/dev/null || true
sleep 1

SESSION="lb_oran"

if command -v tmux &>/dev/null; then
    echo "[*] Launching with tmux (session: ${SESSION})"
    tmux kill-session -t "${SESSION}" 2>/dev/null || true
    tmux new-session -d -s "${SESSION}" -x 220 -y 55

    # ── Pane 0: nearRT-RIC ───────────────────────────────────────────
    tmux rename-window -t "${SESSION}:0" "LB-System"
    tmux send-keys -t "${SESSION}:0" \
        "echo '=== PANE 0: nearRT-RIC ===' && \
         cd ${FLEXRIC_BUILD}/examples/ric && \
         ./nearRT-RIC -c ${ROOT}/flexric/flexric.conf" Enter

    # ── Pane 1: ns-3 simulation ──────────────────────────────────────
    tmux split-window -v -t "${SESSION}:0"
    NS3_CMD="cd ${NS3_DIR} && ./ns3 run '${NS3_SCENARIO} --simTime=${SIM_TIME}'"
    if [[ -n "${SAVE_LABEL}" ]]; then
        NS3_CMD="${NS3_CMD} && echo '[*] Sim done — saving...' && bash ${SAVE_SCRIPT} ${SAVE_LABEL} && echo '[*] Saved to ~/sim_results/'"
    fi
    tmux send-keys -t "${SESSION}:0.1" \
        "echo '=== PANE 1: ns-3 lb_awf_scenario ===' && sleep 3 && ${NS3_CMD}" Enter

    # ── Pane 2: xapp_lb_awf ─────────────────────────────────────────
    # CRITICAL: must start AFTER ns-3 E2 nodes connect (wait ~12s)
    tmux split-window -v -t "${SESSION}:0"
    tmux send-keys -t "${SESSION}:0.2" \
        "echo '=== PANE 2: xapp_lb_awf ===' && \
         echo 'Waiting 12s for ns-3 E2 nodes to connect...' && \
         sleep 12 && \
         ${XAPP_BIN} -c ${ROOT}/flexric/flexric.conf" Enter

    echo ""
    echo "[*] All processes launching in tmux session '${SESSION}'"
    echo "[*] Attach  : tmux attach -t ${SESSION}"
    echo "[*] Kill all: tmux kill-session -t ${SESSION}"
    echo ""
    echo "[*] CRITICAL: watch PANE 1 for 'E2 SETUP REQUEST' before xApp connects"
    echo "[*] Output files appear in ~/:"
    echo "    alyaadone.csv   — full HO log (paper metrics Eq.1-14)"
    echo "    sinr_xapp.csv   — per-UE SINR snapshots"
    echo "    handover.csv    — ns-3 handover events"
    echo "    lstm_features.csv — KPM features"
    tmux attach -t "${SESSION}"

else
    # ── No tmux: print manual instructions ──────────────────────────
    echo ""
    echo "[!] tmux not found. Run each command in a separate terminal:"
    echo ""
    echo "══ TERMINAL 1 — nearRT-RIC ══════════════════════════════════"
    echo "  cd ${FLEXRIC_BUILD}/examples/ric"
    echo "  ./nearRT-RIC -c ${ROOT}/flexric/flexric.conf"
    echo ""
    echo "══ TERMINAL 2 — ns-3 (wait for E2 SETUP REQUEST first) ═════"
    echo "  cd ${NS3_DIR}"
    echo "  ./ns3 run '${NS3_SCENARIO} --simTime=${SIM_TIME}'"
    echo ""
    echo "══ TERMINAL 3 — xApp (ONLY after ns-3 E2 nodes connected) ══"
    echo "  pkill -9 -f xapp_lb_awf   # kill stale if any"
    echo "  ${XAPP_BIN} -c ${ROOT}/flexric/flexric.conf"
    echo ""
    echo "══ OUTPUT FILES (written to \$HOME/) ═══════════════════════"
    echo "  ~/alyaadone.csv     — HO events with all paper metrics (Eq.1-14)"
    echo "  ~/sinr_xapp.csv     — per-UE SINR snapshots per KPM cycle"
    echo "  ~/handover.csv      — ns-3 handover events (START/SUCCESS/FAILURE)"
    echo "  ~/lstm_features.csv — 14-feature KPM log"
    echo ""
fi
