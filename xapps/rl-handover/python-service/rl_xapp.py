#!/usr/bin/env python3
"""
RL xApp — O-RAN Handover Optimization using DDQN
Port: 5001 (separate from GRU service on 5000)
"""
import os, sys, json, threading
import numpy as np
import torch
from flask import Flask, request, jsonify
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "src"))
from agent import QNetwork

RL_PORT = int(os.environ.get("RL_PORT", 5001))

# v2 model (retrained on real NS-3 mmWave data) takes priority if it exists
_BASE = Path(__file__).parent / "models"
MODEL_PATH = _BASE / "rl_ns3_handover_v2.pth" if (_BASE / "rl_ns3_handover_v2.pth").exists() \
             else _BASE / "rl_ns3_handover.pth"

STATE_SIZE = 8 * 5   # 8 features × 5-step window
ACTION_SIZE = 2       # 0=stay, 1=handover
WINDOW_SIZE = 5

# Feature normalization ranges — updated to real NS-3 mmWave data ranges
# (measured from lstm_features.csv produced by gru_scenario with 20 UEs, 7 cells)
FEAT_RANGES = {
    "Level":           (-115.0, -35.0),   # RSRP dBm  (NS-3: -112 to -35)
    "SNR":             ( -15.0,  65.0),   # SINR dB   (NS-3:  -12 to  65)
    "CQI":             (   0.0,  15.0),   # CQI       (NS-3:    0 to  15)
    "DL_bitrate":      (   0.0, 310000.0),# bps       (NS-3:    0 to 307k)
    "SecondCell_RSRP": (-105.0, -23.0),   # nbr RSRP  (NS-3: -104 to -23)
    "SecondCell_SNR":  (  -5.0,  77.0),   # nbr SINR  (NS-3:   -4 to  77)
    "Speed":           (  15.0, 140.0),   # m/s       (NS-3:   15 to 137)
    "NRxLev1":         (-105.0, -23.0),   # = nbr RSRP (NS-3: -104 to -23)
}
FEAT_ORDER = ["Level", "SNR", "CQI", "DL_bitrate",
              "SecondCell_RSRP", "SecondCell_SNR", "Speed", "NRxLev1"]

# Load model
_model = None
_model_lock = threading.Lock()
_ue_windows = {}  # ue_id → deque of 5 feature vectors
_ue_windows_lock = threading.Lock()

_stats = {"total_predictions": 0, "handovers_recommended": 0, "stays_recommended": 0}
_stats_lock = threading.Lock()

def load_model():
    global _model
    with _model_lock:
        if _model is not None:
            return
        net = QNetwork(STATE_SIZE, ACTION_SIZE)
        if MODEL_PATH.exists():
            net.load_state_dict(torch.load(str(MODEL_PATH), map_location="cpu"))
            print(f"[RL xApp] Model loaded from {MODEL_PATH}")
        else:
            print(f"[RL xApp] WARNING: model not found at {MODEL_PATH}, using random weights")
        net.eval()
        _model = net

def normalize_features(raw: dict) -> np.ndarray:
    """Map raw NS-3 measurements to normalized [0,1] feature vector."""
    vec = []
    for feat in FEAT_ORDER:
        lo, hi = FEAT_RANGES[feat]
        val = float(raw.get(feat, raw.get(feat.lower(), (lo+hi)/2)))
        vec.append(max(0.0, min(1.0, (val - lo) / (hi - lo + 1e-9))))
    return np.array(vec, dtype=np.float32)

def update_window(ue_id: int, feat_vec: np.ndarray) -> np.ndarray:
    with _ue_windows_lock:
        if ue_id not in _ue_windows:
            _ue_windows[ue_id] = deque(maxlen=WINDOW_SIZE)
        _ue_windows[ue_id].append(feat_vec)
        window = list(_ue_windows[ue_id])
    # Pad if < WINDOW_SIZE
    while len(window) < WINDOW_SIZE:
        window = [window[0]] + window
    return np.concatenate(window, axis=0)  # (40,)

def rl_predict(ue_id: int, raw_features: dict) -> dict:
    load_model()
    feat_vec = normalize_features(raw_features)
    state = update_window(ue_id, feat_vec)  # (40,)
    state_t = torch.FloatTensor(state).unsqueeze(0)
    with torch.no_grad():
        q_values = _model(state_t).numpy()[0]
    action = int(np.argmax(q_values))
    return {
        "action": action,
        "should_handover": bool(action == 1),
        "q_values": q_values.tolist(),
        "q_stay": float(q_values[0]),
        "q_handover": float(q_values[1]),
        "confidence": float(abs(q_values[1] - q_values[0])),
    }

app = Flask(__name__)

@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok", "model_loaded": _model is not None, "stats": _stats})

@app.route("/predict", methods=["POST"])
def predict():
    try:
        data = request.json
        if not data:
            return jsonify({"error": "no data"}), 400
        ue_id = int(data.get("ue_id", 0))
        # Accept features either as flat dict or under "features" key
        raw = data.get("features", data)
        # Map C xApp field names to RL feature names
        mapped = {
            "Level":           data.get("serving_rsrp", data.get("Level", -90.0)),
            "SNR":             data.get("serving_sinr", data.get("SNR", 10.0)),
            "CQI":             data.get("cqi", data.get("CQI", 10.0)),
            "DL_bitrate":      data.get("dl_bitrate", data.get("DL_bitrate", 10.0)),
            "SecondCell_RSRP": data.get("neighbor_rsrp", data.get("SecondCell_RSRP", -100.0)),
            "SecondCell_SNR":  data.get("neighbor_sinr", data.get("SecondCell_SNR", 5.0)),
            "Speed":           data.get("speed", data.get("Speed", 3.0)),
            "NRxLev1":         data.get("neighbor_sinr", 5.0) - data.get("serving_sinr", 10.0),
        }
        result = rl_predict(ue_id, mapped)
        with _stats_lock:
            _stats["total_predictions"] += 1
            if result["should_handover"]:
                _stats["handovers_recommended"] += 1
            else:
                _stats["stays_recommended"] += 1
        return jsonify(result)
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/stats", methods=["GET"])
def stats():
    with _stats_lock:
        return jsonify(_stats)

if __name__ == "__main__":
    v = "v2 (NS-3 retrained)" if "v2" in MODEL_PATH.name else "v1 (synthetic data)"
    print("=" * 60)
    print("  RL xApp — DDQN Handover Optimization")
    print(f"  Model: {MODEL_PATH.name}  [{v}]")
    print(f"  Port:  {RL_PORT}")
    print("=" * 60)
    load_model()
    app.run(host="0.0.0.0", port=RL_PORT, debug=False, threaded=True)
