#!/bin/bash
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  Farouk GUI — One-command launcher
#  Usage:  ./start.sh
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_BACKEND="$SCRIPT_DIR/../yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/GUI"
# Resolve to absolute path
GUI_BACKEND="$(realpath "$GUI_BACKEND" 2>/dev/null || echo "$GUI_BACKEND")"

clear
echo ""
echo "  ███████╗ █████╗ ██████╗  ██████╗ ██╗   ██╗██╗  ██╗"
echo "  ██╔════╝██╔══██╗██╔══██╗██╔═══██╗██║   ██║██║ ██╔╝"
echo "  █████╗  ███████║██████╔╝██║   ██║██║   ██║█████╔╝ "
echo "  ██╔══╝  ██╔══██║██╔══██╗██║   ██║██║   ██║██╔═██╗ "
echo "  ██║     ██║  ██║██║  ██║╚██████╔╝╚██████╔╝██║  ██╗"
echo "  ╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝  ╚═════╝ ╚═╝  ╚═╝"
echo ""
echo "          5G O-RAN COMMAND CENTER  ·  Farouk GUI"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ── 0. Kill stale processes on our ports ──────────────────────────
echo "  Cleaning stale processes..."
pkill -f "uvicorn controller:app" 2>/dev/null || true
fuser -k 3001/tcp 2>/dev/null || true
fuser -k 8001/tcp 2>/dev/null || true
sleep 1

# ── 1. Docker backend (InfluxDB + data API on port 8000) ──────────
echo "  [1/3] Starting Docker backend  (port 8000)..."
if [ -d "$GUI_BACKEND" ]; then
  cd "$GUI_BACKEND"
  docker compose up -d influxdb gui > /tmp/farouk_docker.log 2>&1
  sleep 3
  echo "        ✓ Docker backend ready"
else
  echo "        ⚠  GUI_BACKEND not found at: $GUI_BACKEND"
fi

# ── 2. System controller (port 8001) ─────────────────────────────
echo "  [2/3] Starting system controller  (port 8001)..."
cd "$SCRIPT_DIR"
python3 -m uvicorn controller:app \
  --host 0.0.0.0 --port 8001 \
  --log-level warning \
  > /tmp/farouk_controller.log 2>&1 &
sleep 2
echo "        ✓ Controller ready"

# ── 3. Vite 3D frontend (port 3001) ──────────────────────────────
echo "  [3/3] Starting 3D GUI  (port 3001)..."
cd "$SCRIPT_DIR"
npm run dev -- --host > /tmp/farouk_vite.log 2>&1 &
sleep 3
echo "        ✓ GUI ready"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  ✓  Open browser →  http://localhost:3001"
echo ""
echo "  From the GUI control panel you can now:"
echo "   • Start / Stop FlexRIC"
echo "   • Choose scenario & start ns-3 simulation"
echo "   • Start Data Pusher (CSV → InfluxDB)"
echo "   • Start / Stop xApps"
echo "   • Or hit  ⚡ LAUNCH ALL  for one-click start"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# Keep script alive so Ctrl-C stops everything
wait
