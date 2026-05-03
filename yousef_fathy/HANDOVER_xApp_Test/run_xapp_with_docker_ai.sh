#!/bin/bash
# Run the GRU-enhanced handover xApp with the Fares prediction service
# Usage: ./run_xapp_with_docker_ai.sh [xapp args...]
#
# Terminal layout (3 terminals needed):
#   T1: ./nearRT-RIC -c flexric.conf
#   T2: ns3 run scratch/scenario-zero-with_parallel_loging
#   T3: ./run_xapp_with_docker_ai.sh     <-- this script

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLEXRIC_BUILD="${SCRIPT_DIR}/../flexric/build"
XAPP_BIN="${FLEXRIC_BUILD}/examples/xApp/c/handover_gru/xapp_handover_gru"
FARES_DIR="${SCRIPT_DIR}/../../Fares"
GRU_SERVICE_PY="${SCRIPT_DIR}/gru_xapp.py"
GRU_PORT=5000

# ── 1. Start GRU prediction service (Python/Flask) ───────────────────────────
echo "[*] Starting GRU prediction service on port ${GRU_PORT}..."
if lsof -i ":${GRU_PORT}" &>/dev/null; then
    echo "[*] Port ${GRU_PORT} already in use — assuming service is running."
else
    # Install deps if needed
    if ! python3 -c "import flask, tensorflow, joblib" 2>/dev/null; then
        echo "[*] Installing Python dependencies..."
        pip install -q -r "${SCRIPT_DIR}/requirements_lstm.txt"
    fi
    PORT=${GRU_PORT} python3 "${GRU_SERVICE_PY}" &
    GRU_PID=$!
    echo "[*] GRU service PID=${GRU_PID}"

    # Wait for it to become ready
    echo -n "[*] Waiting for GRU service..."
    for i in $(seq 1 30); do
        if curl -sf "http://localhost:${GRU_PORT}/health" &>/dev/null; then
            echo " ready!"
            break
        fi
        sleep 1
        echo -n "."
    done
    if ! curl -sf "http://localhost:${GRU_PORT}/health" &>/dev/null; then
        echo ""
        echo "[!] GRU service did not start. xApp will fall back to SINR-only mode."
    fi
fi

# ── 2. Check xApp binary ──────────────────────────────────────────────────────
if [[ ! -x "${XAPP_BIN}" ]]; then
    echo "[!] xapp_handover_gru not found at: ${XAPP_BIN}"
    echo ""
    echo "    Build it with:"
    echo "    sudo apt install -y libcurl4-openssl-dev libjson-c-dev"
    echo "    cd ${FLEXRIC_BUILD}"
    echo "    cmake .. -DE2AP_VERSION=E2AP_V1 -DKPM_VERSION=KPM_V3_00"
    echo "    make xapp_handover_gru -j\$(nproc)"
    exit 1
fi

# ── 3. Launch xApp ────────────────────────────────────────────────────────────
export LSTM_SERVICE_URL="http://localhost:${GRU_PORT}"
echo ""
echo "[*] Launching GRU-enhanced xApp..."
echo "[*] GRU_SERVICE_URL = ${LSTM_SERVICE_URL}"
echo "[*] Make sure nearRT-RIC and ns-3 are running!"
echo ""
exec "${XAPP_BIN}" "$@"
