#!/bin/bash
# ══════════════════════════════════════════════════════════════════════
#  kill_sim.sh — Kill EVERYTHING from a GRU O-RAN simulation run
#  Usage:  bash /home/omar_farouk/open-ran-clean/kill_sim.sh
# ══════════════════════════════════════════════════════════════════════

GUI_DIR="/home/omar_farouk/open-ran-clean/yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/GUI"

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║           KILLING ALL SIM PROCESSES              ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# ── 1. Simulation processes ──────────────────────────────────────────
echo "[1/4] Killing simulation processes..."
pkill -9 -f 'nearRT-RIC'            2>/dev/null && echo "    ✓ nearRT-RIC"         || echo "    - nearRT-RIC (not running)"
pkill -9 -f 'ns3'                   2>/dev/null && echo "    ✓ ns3"                || echo "    - ns3 (not running)"
pkill -9 -f 'xapp_handover_gru'     2>/dev/null && echo "    ✓ xapp_handover_gru"  || echo "    - xapp_handover_gru (not running)"
pkill -9 -f 'xapp_lb_awf'           2>/dev/null && echo "    ✓ xapp_lb_awf"        || echo "    - xapp_lb_awf (not running)"
pkill -9 -f 'xapp_rc_handover'      2>/dev/null && echo "    ✓ xapp_rc_handover"   || echo "    - xapp_rc_handover (not running)"
pkill -9 -f 'gru_xapp\.py'          2>/dev/null && echo "    ✓ gru_xapp.py"        || echo "    - gru_xapp.py (not running)"
pkill -9 -f 'sim_data_pusher'       2>/dev/null && echo "    ✓ sim_data_pusher"    || echo "    - sim_data_pusher (not running)"

# ── 2. Controller + 3D GUI (Vite) ───────────────────────────────────
echo ""
echo "[2/4] Killing controller + 3D GUI..."
pkill -f 'uvicorn controller:app'   2>/dev/null && echo "    ✓ uvicorn controller" || echo "    - uvicorn controller (not running)"
pkill -f 'vite'                     2>/dev/null && echo "    ✓ vite (3D frontend)" || echo "    - vite (not running)"
pkill -f 'npm run dev'              2>/dev/null && echo "    ✓ npm run dev"         || echo "    - npm run dev (not running)"

# Kill anything still holding ports 3001 and 8001
fuser -k 3001/tcp 2>/dev/null && echo "    ✓ freed port 3001" || true
fuser -k 8001/tcp 2>/dev/null && echo "    ✓ freed port 8001" || true

# ── 3. Docker (GUI backend + InfluxDB + Grafana) ─────────────────────
echo ""
echo "[3/4] Stopping Docker containers (GUI + InfluxDB + Grafana)..."
if [ -d "$GUI_DIR" ]; then
    cd "$GUI_DIR"
    docker compose down 2>&1 | sed 's/^/    /'
    echo "    ✓ Docker stopped"
else
    echo "    ⚠  GUI_DIR not found: $GUI_DIR"
fi

# Extra: kill any stray docker containers related to this project
docker ps -q --filter "name=gui" --filter "name=influxdb" --filter "name=grafana" 2>/dev/null \
    | xargs -r docker kill 2>/dev/null && echo "    ✓ Stray containers killed" || true

# ── 4. Free remaining ports ──────────────────────────────────────────
echo ""
echo "[4/4] Freeing remaining ports..."
fuser -k 8000/tcp 2>/dev/null && echo "    ✓ freed port 8000 (GUI backend)" || true
fuser -k 8086/tcp 2>/dev/null && echo "    ✓ freed port 8086 (InfluxDB)"    || true
fuser -k 3000/tcp 2>/dev/null && echo "    ✓ freed port 3000 (Grafana)"     || true
fuser -k 5000/tcp 2>/dev/null && echo "    ✓ freed port 5000 (gru_xapp)"    || true

# Clear FlexRIC log so next run starts clean
> /tmp/flexric.log 2>/dev/null && echo "    ✓ cleared /tmp/flexric.log" || true

# Clear stale component logs so next run starts with empty logs
for log in /tmp/farouk_ns3.log /tmp/farouk_xapp.log /tmp/farouk_gru.log /tmp/farouk_pusher.log /tmp/controller.log; do
    > "$log" 2>/dev/null && echo "    ✓ cleared $(basename $log)" || true
done

# Clear runtime CSV files — ns-3 APPENDS, so stale data must be wiped before each run
echo "time_sec,ue_id,from_cell,to_cell,event,executed_ok" > /home/omar_farouk/handover.csv \
    && echo "    ✓ reset handover.csv" || true
> /home/omar_farouk/lstm_features.csv \
    && echo "    ✓ cleared lstm_features.csv" || true
> /home/omar_farouk/kpm_handover_features.csv 2>/dev/null \
    && echo "    ✓ cleared kpm_handover_features.csv" || true

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║  ALL CLEAR — Everything killed, ports freed.     ║"
echo "║  Safe to restart with:  bash gru.sh              ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""
