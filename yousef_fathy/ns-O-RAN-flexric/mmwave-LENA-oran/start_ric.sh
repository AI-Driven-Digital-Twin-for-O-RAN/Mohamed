#!/bin/bash
# One-shot: clean up stale state and start FlexRIC
# Run this once per session, BEFORE using the GUI

FLEXRIC_BIN="/home/omar_farouk/open-ran-clean/yousef_fathy/flexric/build/examples/ric/nearRT-RIC"

echo "=== Stopping stale processes ==="
pkill -9 -f "xapp_rc_handover_ctrl" 2>/dev/null
pkill -9 -f "nearRT-RIC"            2>/dev/null
pkill -9 -f "gru_scenario"          2>/dev/null
sleep 1

echo "=== Cleaning stale xApp DBs ==="
rm -f /tmp/xapp_db_*

echo "=== Starting FlexRIC ==="
"$FLEXRIC_BIN" > /tmp/flexric.log 2>&1 &
FLEX_PID=$!
sleep 3

if ! kill -0 $FLEX_PID 2>/dev/null; then
  echo "ERROR: FlexRIC failed to start. Check /tmp/flexric.log"
  exit 1
fi

echo ""
echo "✓ FlexRIC running (PID $FLEX_PID)"
echo "✓ Ready — open http://localhost:3001 or http://localhost:8000"
echo "  Select GRU scenario → START → wait ~10s for 8 cells → START GRU xAPP"
