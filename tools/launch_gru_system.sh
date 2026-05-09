#!/bin/bash
# ══════════════════════════════════════════════════════════════════════
#  GRU-Enhanced O-RAN System Launcher
#  Scenario: gru_scenario (7 gNBs, 20 UEs, 600s)
#  Stack: nearRT-RIC  +  ns-3  +  GRU xApp  +  C xApp (xapp_handover_gru)
# ══════════════════════════════════════════════════════════════════════
# Uses tmux to manage all 4 processes in split panes.
# If tmux is not installed: run each block in a separate terminal manually.
# ══════════════════════════════════════════════════════════════════════

set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLEXRIC_BUILD="${ROOT}/flexric/build"
NS3_DIR="${ROOT}/ns-O-RAN-flexric/mmwave-LENA-oran"
XAPP_TEST="${ROOT}/HANDOVER_xApp_Test"
RIC_BIN="${FLEXRIC_BUILD}/examples/ric/nearRT-RIC"
XAPP_BIN="${FLEXRIC_BUILD}/examples/xApp/c/handover_gru/xapp_handover_gru"
NS3_SCENARIO="scratch/gru_scenario"
GRU_SERVICE="${XAPP_TEST}/gru_xapp.py"
GRU_PORT=5000
SIM_TIME=60    # seconds

# ── Sanity checks ────────────────────────────────────────────────────
if [[ ! -x "${RIC_BIN}" ]]; then
    echo "[ERROR] nearRT-RIC not found: ${RIC_BIN}"
    exit 1
fi
if [[ ! -x "${XAPP_BIN}" ]]; then
    echo "[ERROR] xapp_handover_gru not found: ${XAPP_BIN}"
    echo "  Build: cd ${FLEXRIC_BUILD} && make xapp_handover_gru -j\$(nproc)"
    exit 1
fi
if [[ ! -f "${NS3_DIR}/ns3" ]]; then
    echo "[ERROR] ns3 binary not found: ${NS3_DIR}/ns3"
    exit 1
fi

echo "══════════════════════════════════════════════════════════════════"
echo "  GRU-Enhanced O-RAN System"
echo "  Scenario : gru_scenario (7 gNBs, 20 UEs)"
echo "  Sim time : ${SIM_TIME}s"
echo "══════════════════════════════════════════════════════════════════"

# ── tmux launcher ────────────────────────────────────────────────────
SESSION="gru_oran"

if command -v tmux &>/dev/null; then
    echo "[*] Launching with tmux (session: ${SESSION})"
    tmux kill-session -t "${SESSION}" 2>/dev/null || true
    tmux new-session -d -s "${SESSION}" -x 220 -y 55

    # Pane 0 — nearRT-RIC
    tmux rename-window -t "${SESSION}:0" "System"
    tmux send-keys -t "${SESSION}:0" \
        "echo '=== T1: nearRT-RIC ===' && cd ${FLEXRIC_BUILD}/examples/ric && ./nearRT-RIC -c ${ROOT}/flexric/flexric.conf" Enter

    # Pane 1 — GRU Python xApp  (start first so it's ready before C xApp)
    tmux split-window -v -t "${SESSION}:0"
    tmux send-keys -t "${SESSION}:0.1" \
        "echo '=== T2: GRU xApp (Python) ===' && sleep 2 && PORT=${GRU_PORT} NS3_LOG_DIR=${NS3_DIR} python3 ${GRU_SERVICE}" Enter

    # Pane 2 — ns-3 simulation (auto-saves results on completion)
    tmux split-window -v -t "${SESSION}:0"
    tmux send-keys -t "${SESSION}:0.2" \
        "echo '=== T3: ns-3 Energy Saving ===' && sleep 4 && cd ${NS3_DIR} && ./ns3 run '${NS3_SCENARIO} --simTime=${SIM_TIME}' && echo '[*] Simulation done — saving results...' && bash ${ROOT}/save_sim_results.sh gru_t${SIM_TIME}s && echo '[*] Results saved to ~/sim_results/'" Enter

    # Pane 3 — C xApp (GRU-enhanced)
    tmux split-window -h -t "${SESSION}:0.2"
    tmux send-keys -t "${SESSION}:0.3" \
        "echo '=== T4: C xApp (GRU-enhanced) ===' && sleep 10 && LSTM_SERVICE_URL=http://localhost:${GRU_PORT} ${XAPP_BIN} -c ${ROOT}/flexric/flexric.conf" Enter

    echo ""
    echo "[*] All processes launching in tmux session '${SESSION}'"
    echo "[*] Attach with:  tmux attach -t ${SESSION}"
    echo "[*] Kill all:     tmux kill-session -t ${SESSION}"
    tmux attach -t "${SESSION}"

else
    # ── No tmux: print manual instructions ──────────────────────────
    echo ""
    echo "[!] tmux not found. Open 4 terminals and run each command:"
    echo ""
    echo "══ TERMINAL 1 — nearRT-RIC ══════════════════════════════"
    echo "  cd ${FLEXRIC_BUILD}/examples/ric"
    echo "  ./nearRT-RIC -c ${ROOT}/flexric/flexric.conf"
    echo ""
    echo "══ TERMINAL 2 — GRU Python xApp ════════════════════════"
    echo "  PORT=${GRU_PORT} NS3_LOG_DIR=${NS3_DIR} python3 ${GRU_SERVICE}"
    echo ""
    echo "══ TERMINAL 3 — ns-3 Simulation ════════════════════════"
    echo "  cd ${NS3_DIR}"
    echo "  ./ns3 run '${NS3_SCENARIO} --simTime=${SIM_TIME}'"
    echo ""
    echo "══ TERMINAL 4 — C xApp (GRU-Enhanced) ══════════════════"
    echo "  LSTM_SERVICE_URL=http://localhost:${GRU_PORT} \\"
    echo "  ${XAPP_BIN} -c ${ROOT}/flexric/flexric.conf"
    echo ""
    echo "══ OUTPUT FILES (written to \$HOME/) ════════════════════"
    echo "  ~/lstm_features.csv          — 12-feature GRU input per UE per 0.25s"
    echo "  ~/kpm_handover_features.csv  — 18-feature KPM log"
    echo "  ~/handover.csv               — per-handover event log"
    echo ""
    echo "══ GRU xApp REST API ════════════════════════════════════"
    echo "  http://localhost:${GRU_PORT}/health   — service status"
    echo "  http://localhost:${GRU_PORT}/stats    — prediction counters"
    echo "  http://localhost:${GRU_PORT}/predict  — POST single UE prediction"
    echo ""
fi
