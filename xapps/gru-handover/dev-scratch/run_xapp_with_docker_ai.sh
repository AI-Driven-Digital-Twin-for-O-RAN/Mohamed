#!/bin/bash
# تشغيل الـ xApp مع الـ AI في Docker
# يشغّل حاوية الـ AI إن لم تكن شغالة، ثم يشغّل الـ xApp (يجب تشغيل الـ RIC يدوياً في ترمينال آخر)

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLEXRIC_BUILD="${SCRIPT_DIR}/../flexric/build"
XAPP_BIN="${FLEXRIC_BUILD}/examples/xApp/c/handover_lstm/xapp_handover_lstm"

# تشغيل الـ AI في Docker إن لم تكن الحاوية شغالة
if ! docker ps --format '{{.Names}}' | grep -qx xapp-ai; then
  echo "[*] Starting AI container (mohamed710/xapp:v1) on port 5000..."
  docker run --rm -d -p 5000:5000 --name xapp-ai mohamed710/xapp:v1 || true
  sleep 5
fi

if ! docker ps --format '{{.Names}}' | grep -qx xapp-ai; then
  echo "[!] Could not start AI container. Run manually: docker run --rm -d -p 5000:5000 --name xapp-ai mohamed710/xapp:v1"
  exit 1
fi

if [[ ! -x "$XAPP_BIN" ]]; then
  echo "[!] xApp not found: $XAPP_BIN"
  echo "    Install deps: sudo apt install libcurl4-openssl-dev libjson-c-dev"
  echo "    Then: cd flexric/build && cmake .. -DE2AP_VERSION=E2AP_V1 -DKPM_VERSION=KPM_V3_00 && make xapp_handover_lstm"
  exit 1
fi

export LSTM_SERVICE_URL="${LSTM_SERVICE_URL:-http://localhost:5000}"
echo "[*] Running xApp (LSTM_SERVICE_URL=$LSTM_SERVICE_URL). Ensure nearRT-RIC is running in another terminal."
exec "$XAPP_BIN" "$@"
