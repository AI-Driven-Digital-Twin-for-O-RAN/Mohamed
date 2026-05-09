#!/bin/bash
# ══════════════════════════════════════════════════════════════════════
#  rl.sh  —  RL (DDQN) xApp O-RAN Full Launcher
#  After any reboot, just run:  bash rl.sh   (from the repo root)
#
#  What this script does (in order):
#    1. Kills all stale processes from any previous run
#    2. Clears /tmp/flexric.log so E2 count starts from 0
#    3. Starts Docker  (InfluxDB + 2D backend on port 8000)
#    4. Starts the 3D controller  (port 8001)
#    5. Starts the 3D frontend    (port 3001)
#    6. Triggers LAUNCH ALL via the controller API (RL xApp)
#
#  The controller handles ALL timing internally:
#    - Detects FlexRIC SCTP port 36421 (not TCP)
#    - Waits up to 60s for FlexRIC to be ready
#    - Waits for 7 SCTP E2 connections (ESTAB on port 36421)
#    - Sets RL_PORT=5001 env var for rl_xapp.py
#    - Auto-saves results when simulation finishes
#
#  Optional args:   bash rl.sh [simTime] [nUEs] [nCells]
#  Defaults:        simTime=60  nUEs=20  nCells=7
# ══════════════════════════════════════════════════════════════════════

SIM_TIME="${1:-60}"
N_UES="${2:-20}"
N_CELLS="${3:-7}"

# PROJECT_ROOT = repo root. This script lives at tools/rl.sh, so we go one dir up.
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
RUNTIME_CSV_HOME="${RUNTIME_CSV_HOME:-$HOME}"

CONTROLLER_DIR="${PROJECT_ROOT}/platform/controller"
GUI_FRONTEND_DIR="${PROJECT_ROOT}/platform/gui"
GUI_DIR="${PROJECT_ROOT}/platform/2d-gui"
FLEXRIC_BIN="${PROJECT_ROOT}/platform/flexric/build/examples/ric/nearRT-RIC"
XAPP_RL="${PROJECT_ROOT}/platform/flexric/build/examples/xApp/c/handover_rl/xapp_handover_rl"
RL_PY="${PROJECT_ROOT}/xapps/rl-handover/python-service/rl_xapp.py"
NS3="${PROJECT_ROOT}/platform/ns3-sim/mmwave-LENA-oran/ns3"
PUSHER="${PROJECT_ROOT}/platform/ns3-sim/mmwave-LENA-oran/sim_data_pusher.py"
CONTROLLER="$CONTROLLER_DIR/controller.py"
export PROJECT_ROOT RUNTIME_CSV_HOME

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║       RL (DDQN) O-RAN Simulator — LAUNCHER       ║"
echo "║  simTime=${SIM_TIME}s   UEs=${N_UES}   Cells=${N_CELLS}                 ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# ── Guard: refuse to run if GRU service is already running ───────────
if pgrep -f "gru_xapp\.py" > /dev/null; then
    echo "WARNING: GRU service is running on port 5000. Stop it first with kill_sim.sh"
    exit 1
fi

# ── Sanity check: all required files must exist ──────────────────────
echo "[0/5] Checking required files..."
MISSING=0
for F in "$FLEXRIC_BIN" "$XAPP_RL" "$RL_PY" "$NS3" "$PUSHER" "$CONTROLLER"; do
    if [ ! -f "$F" ]; then
        echo "    MISSING: $F"
        MISSING=1
    fi
done
if [ "$MISSING" -eq 1 ]; then
    echo "    ERROR: Missing files above. Cannot continue."
    exit 1
fi
echo "    All files present."

# ── STEP 1: Kill all stale processes ────────────────────────────────
echo ""
echo "[1/5] Killing stale processes..."
pkill -9 -f 'nearRT-RIC'          2>/dev/null || true
pkill -9 -f 'ns3\.42'             2>/dev/null || true
pkill -9 -f 'xapp_handover_rl'    2>/dev/null || true
pkill -9 -f 'xapp_lb_awf'         2>/dev/null || true
pkill -9 -f 'xapp_rc_handover'    2>/dev/null || true
pkill -9 -f 'sim_data_pusher'     2>/dev/null || true
pkill -9 -f 'rl_xapp\.py'         2>/dev/null || true
pkill -f   'uvicorn controller:app' 2>/dev/null || true
> /tmp/flexric.log
# Clear stale component logs
for log in /tmp/farouk_ns3.log /tmp/farouk_xapp.log /tmp/farouk_rl_xapp.log /tmp/farouk_pusher.log /tmp/controller.log; do
    > "$log" 2>/dev/null || true
done
# Free RL port
fuser -k 5001/tcp 2>/dev/null || true
# Clear runtime CSVs — ns-3 APPENDS, stale data from previous run must be wiped
echo "time_sec,ue_id,from_cell,to_cell,event,executed_ok" > "$RUNTIME_CSV_HOME/handover.csv"
> "$RUNTIME_CSV_HOME/lstm_features.csv"
> "$RUNTIME_CSV_HOME/kpm_handover_features.csv" 2>/dev/null || true
sleep 2
echo "    Done — all clear."

# ── STEP 2: Docker ───────────────────────────────────────────────────
echo ""
echo "[2/5] Starting Docker (InfluxDB + backend on port 8000)..."
cd "$GUI_DIR"
docker compose up -d influxdb gui
sleep 3
echo "    Done."

# ── STEP 3: Controller (port 8001) ───────────────────────────────────
echo ""
echo "[3/5] Starting 3D controller (port 8001)..."
cd "$CONTROLLER_DIR"
python3 -m uvicorn controller:app --host 0.0.0.0 --port 8001 --log-level warning \
    > /tmp/controller.log 2>&1 &

# Wait up to 15 seconds for controller to respond
READY=0
for i in $(seq 1 15); do
    sleep 1
    if curl -s http://localhost:8001/ctrl/status > /dev/null 2>&1; then
        READY=1
        break
    fi
done
if [ "$READY" -eq 0 ]; then
    echo "    ERROR: Controller did not start. Check /tmp/controller.log"
    exit 1
fi
echo "    Ready on http://localhost:8001"

# ── STEP 4: Frontend (port 3001) ─────────────────────────────────────
echo ""
echo "[4/5] Starting 3D frontend (port 3001)..."
cd "$GUI_FRONTEND_DIR"
nohup npm run dev > /tmp/vite.log 2>&1 &
sleep 4
echo "    Ready on http://localhost:3001"

# ── STEP 5: Launch simulation via controller API ──────────────────────
echo ""
echo "[5/5] Triggering RL simulation (LAUNCH ALL)..."
RESP=$(curl -s -X POST http://localhost:8001/ctrl/launch-all \
    -H "Content-Type: application/json" \
    -d "{\"scenario\":\"gru_scenario\",\"xapp_type\":\"rl\",\"n_ues\":${N_UES},\"n_mmwave\":${N_CELLS},\"sim_time\":${SIM_TIME}}")
echo "    Controller response: $RESP"

# ── Done ──────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  RL SIMULATION IS RUNNING                                    ║"
echo "║                                                              ║"
echo "║  Open your browser:                                          ║"
echo "║    3D GUI  →  http://localhost:3001  (Command Center)        ║"
echo "║    2D GUI  →  http://localhost:8000  (Grafana charts)        ║"
echo "║                                                              ║"
echo "║  Expected duration: ~3 hours (simTime=60 sim-seconds)        ║"
echo "║                                                              ║"
echo "║  Check E2 connections (target = ${N_CELLS}):                        ║"
echo "║    ss -Snp | grep ':36421' | grep -c ESTAB                   ║"
echo "║                                                              ║"
echo "║  Monitor logs:                                               ║"
echo "║    tail -f /tmp/farouk_ns3.log       (NS-3 simulation)       ║"
echo "║    tail -f /tmp/farouk_xapp.log      (RL C xApp)             ║"
echo "║    tail -f /tmp/farouk_rl_xapp.log   (RL Python service)     ║"
echo "║    tail -f /tmp/flexric.log          (FlexRIC E2 activity)   ║"
echo "║                                                              ║"
echo "║  Results auto-saved when done:                               ║"
echo "║    ${PROJECT_ROOT}/sim-results/"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
