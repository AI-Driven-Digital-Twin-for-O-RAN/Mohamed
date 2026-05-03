#!/bin/bash
# ── Start everything for the GRU O-RAN demo ──────────────────────
# Run once. Everything starts automatically in the correct order.

set -e

FLEXRIC_BIN="/home/omar_farouk/open-ran-clean/yousef_fathy/flexric/build/examples/ric/nearRT-RIC"
NS3_DIR="/home/omar_farouk/open-ran-clean/yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran"
GUI_DIR="$NS3_DIR/GUI"
XAPP_TRIGGER_DIR="$GUI_DIR/FlexRIC xApp GUI trigger"
GUI3D_DIR="/home/omar_farouk/open-ran-clean/5g-gui-v2"
BACKEND_URL="http://localhost:8000"

log() { echo "[$(date '+%H:%M:%S')] $*"; }

# ── 1. Kill everything stale ──────────────────────────────────────
log "Stopping any stale processes..."
pkill -9 -f "xapp_rc_handover_ctrl" 2>/dev/null || true
pkill -9 -f "nearRT-RIC"            2>/dev/null || true
pkill -9 -f "gru_scenario"          2>/dev/null || true
pkill -9 -f "sim_data_pusher"       2>/dev/null || true
pkill -9 -f "xApp_trigger"          2>/dev/null || true
pkill -9 -f "stop_xApp"             2>/dev/null || true
fuser -k 38868/tcp 2>/dev/null || true
fuser -k 38869/tcp 2>/dev/null || true
rm -f /tmp/xapp_db_* /tmp/flexric*.log
sleep 1

# ── 2. Start Docker backend (if not running) ──────────────────────
log "Starting Docker backend..."
cd "$GUI_DIR"
docker compose up -d influxdb gui 2>/dev/null || true
sleep 4

# ── 3. Start FlexRIC ──────────────────────────────────────────────
log "Starting FlexRIC..."
"$FLEXRIC_BIN" > /tmp/flexric.log 2>&1 &
FLEX_PID=$!
sleep 3
kill -0 $FLEX_PID 2>/dev/null || { log "ERROR: FlexRIC failed. Check /tmp/flexric.log"; exit 1; }
log "FlexRIC running (PID $FLEX_PID)"

# ── 4. Start ns-3 GRU scenario ───────────────────────────────────
log "Starting ns-3 GRU scenario..."
curl -s -X POST "$BACKEND_URL/reset_simulation" > /dev/null 2>&1 || true
curl -s -X POST "$BACKEND_URL/start_simulation" \
  -H "Content-Type: application/json" \
  -d '{"scenario":"scratch/gru_scenario.cc","flexric":"true","flags":"true","e2TermIp":"127.0.0.1","hoSinrDifference":"3","indicationPeriodicity":"1.5","simTime":"3600","KPM_E2functionID":"2","RC_E2functionID":"3","N_MmWaveEnbNodes":"7","N_Ues":"20"}' \
  > /dev/null 2>&1 || true

# Wait for ns-3 to start and connect to FlexRIC
log "Waiting for ns-3 E2 connection..."
for i in $(seq 1 40); do
  sleep 3
  E2_COUNT=$(grep -c "E2 SETUP-REQUEST" /tmp/flexric.log 2>/dev/null; true)
  [ "${E2_COUNT:-0}" -ge 7 ] && break
  printf "  waiting... (%d/40)\r" $i
done
E2_COUNT=$(grep -c "E2 SETUP-REQUEST" /tmp/flexric.log 2>/dev/null; true)
log "E2 nodes connected: ${E2_COUNT:-0}"

# ── 5. Start sim_data_pusher ─────────────────────────────────────
log "Starting sim_data_pusher..."
cd "$NS3_DIR"
python3 sim_data_pusher.py > /tmp/sim_data_pusher.log 2>&1 &
sleep 2

# ── 6. Start xApp trigger servers ────────────────────────────────
log "Starting xApp trigger servers..."
cd "$XAPP_TRIGGER_DIR"
python3 xApp_trigger.py > /tmp/xapp_trigger.log 2>&1 &
sleep 2

# ── 7. Start 3D GUI (Vite) ───────────────────────────────────────
if ! ss -tlnp | grep -q ":3001"; then
  log "Starting 3D GUI..."
  cd "$GUI3D_DIR"
  npm run dev > /tmp/gui3d.log 2>&1 &
  sleep 4
fi

# ── Done ─────────────────────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Everything is running."
echo ""
echo "  Open in browser: http://localhost:3001"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
