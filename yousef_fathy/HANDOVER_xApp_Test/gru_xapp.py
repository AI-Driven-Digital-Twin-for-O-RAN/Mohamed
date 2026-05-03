#!/usr/bin/env python3
"""
GRU xApp — O-RAN Handover Optimization
========================================
Combines three responsibilities in one process:
  1. REST prediction service  → answers requests from the C xApp (best2.c)
  2. InfluxDB push            → writes GRU decision metrics to InfluxDB
                                so they appear on the Grafana GUI alongside
                                the ns-3 simulation data
  3. Live log monitor         → tails ns-3 output CSV files and runs
                                autonomous GRU inference to populate the GUI
                                even when the C xApp is not running

Run order (4 terminals):
  T1: cd flexric/build && ./nearRT-RIC -c ../flexric.conf
  T2: cd ns-O-RAN-flexric/mmwave-LENA-oran && ./ns3 run scratch/scenario-zero-with_parallel_loging
  T3: python3 HANDOVER_xApp_Test/gru_xapp.py       ← this file
  T4: ./flexric/build/examples/xApp/c/handover_gru/xapp_handover_gru

Environment variables:
  LSTM_MODEL_DIR   Path to Fares model dir (default: auto-detected)
  GRU_PORT         Flask port (default: 5000)
  INFLUX_HOST      InfluxDB host (default: localhost)
  INFLUX_PORT      InfluxDB port (default: 8086)
  NS3_LOG_DIR      Directory with ns-3 CSV files (default: current dir)
"""

import os
import sys
import json
import time
import threading
import glob
from pathlib import Path

import numpy as np
import joblib
from flask import Flask, request, jsonify
from flask_cors import CORS

# ── InfluxDB (optional — gracefully degraded if not installed/running) ─────────
try:
    from influxdb import InfluxDBClient
    _INFLUX_AVAILABLE = True
except ImportError:
    _INFLUX_AVAILABLE = False

# ── TensorFlow ─────────────────────────────────────────────────────────────────
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import tensorflow as tf

# ══════════════════════════════════════════════════════════════════════════════
# CONFIGURATION
# ══════════════════════════════════════════════════════════════════════════════

_HERE = Path(__file__).parent
_FARES_DIR = Path(os.environ.get(
    "LSTM_MODEL_DIR",
    _HERE.parent.parent / "Fares"   # open-ran-clean/Fares
))

MODEL_PATH  = _FARES_DIR / "model"     / "handover_model_final.keras"
SCALER_PATH = _FARES_DIR / "artifacts" / "scaler.joblib"
CONFIG_PATH = _FARES_DIR / "artifacts" / "config.joblib"

GRU_PORT    = int(os.environ.get("GRU_PORT",    5000))
INFLUX_HOST = os.environ.get("INFLUX_HOST", "localhost")
INFLUX_PORT = int(os.environ.get("INFLUX_PORT", 8086))
INFLUX_DB   = os.environ.get("INFLUX_DB",   "influx")
NS3_LOG_DIR = Path(os.environ.get("NS3_LOG_DIR", "."))

EXPECTED_FEATURES = [
    "Level", "Qual", "SNR", "CQI",
    "SecondCell_RSRP", "SecondCell_SNR",
    "NRxLev1", "NQual1",
    "Speed", "DL_bitrate", "UL_bitrate", "BANDWIDTH"
]

WINDOW_SIZE = 10   # GRU expects sequences of this length

# ══════════════════════════════════════════════════════════════════════════════
# GLOBAL MODEL STATE
# ══════════════════════════════════════════════════════════════════════════════

_model  = None
_scaler = None
_config = None
_model_lock = threading.Lock()

# Rolling window of measurements per UE for sequence input
_ue_windows: dict = {}   # ue_id → list of feature vectors (len <= WINDOW_SIZE)
_ue_windows_lock = threading.Lock()

# Per-UE raw signal history for slope computation (Algorithm 1 oscillation check)
_ue_signal_history: dict = {}   # ue_id → list of (rsrp, snr) tuples
_ue_bearing_history: dict = {}  # ue_id → list of bearing values (degrees)
_ue_signal_lock = threading.Lock()
SLOPE_WINDOW = 5  # number of readings used for slope regression

_ue_position_history: dict = {}  # ue_id -> list of (lat, lon) tuples
_ue_latest_full_features: dict = {}  # ue_id -> latest full feature dict from lstm_features.csv
_ue_feature_lock = threading.Lock()

# Counters pushed to InfluxDB
_stats = {
    "total_predictions":  0,
    "handovers_executed":  0,
    "handovers_prevented": 0,
    "ping_pong_blocks":    0,
    "unnecessary_blocks":  0,
}
_stats_lock = threading.Lock()


# ══════════════════════════════════════════════════════════════════════════════
# MODEL LOADING
# ══════════════════════════════════════════════════════════════════════════════

def _load_model_compat(model_path: Path):
    """
    Load .keras model saved with Keras 3 weights format into a Keras 2.15
    environment by manually remapping h5 weight keys.

    Keras 3 stores weights under top-level flat keys like:
      'layers\\gru\\cell'  (literal backslash in the key name)
    Keras 2.15 load_weights() cannot find these — we do it by hand.
    """
    import zipfile, tempfile, shutil, json, h5py

    tmpdir = tempfile.mkdtemp(prefix="gru_model_")
    try:
        with zipfile.ZipFile(str(model_path)) as z:
            z.extractall(tmpdir)

        with open(os.path.join(tmpdir, "config.json")) as f:
            config_data = json.load(f)

        model = tf.keras.models.model_from_json(json.dumps(config_data))
        model.trainable = False

        weights_path = os.path.join(tmpdir, "model.weights.h5")
        with h5py.File(weights_path, "r") as f:
            def get_vars(key):
                grp = f[key]["vars"]
                out, i = [], 0
                while str(i) in grp:
                    out.append(grp[str(i)][:])
                    i += 1
                return out

            # Keras 3 h5 key  →  Keras 2 layer name
            # Keys use literal single backslash as separator (Windows-style path)
            mapping = {
                "layers\\" + "gru\\" + "cell":    "gru_2",
                "layers\\" + "gru_1\\" + "cell":  "gru_3",
                "layers\\" + "dense":              "dense_1",
                "layers\\" + "dense_1":            "ToS_out",
                "layers\\" + "dense_2":            "Class_out",
            }
            layer_by_name = {l.name: l for l in model.layers}
            for h5_key, layer_name in mapping.items():
                if layer_name not in layer_by_name:
                    continue
                layer = layer_by_name[layer_name]
                vars_data = get_vars(h5_key)
                if len(vars_data) == len(layer.weights):
                    layer.set_weights(vars_data)

        return model

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def load_model():
    global _model, _scaler, _config
    with _model_lock:
        if _model is not None:
            return

        print(f"[GRU xApp] Loading model  : {MODEL_PATH}")
        try:
            _model = tf.keras.models.load_model(str(MODEL_PATH), compile=False)
        except Exception:
            # Keras 3 weight format in a Keras 2 environment — use compat loader
            print("[GRU xApp] Standard load failed → using compat loader (Keras3→Keras2)")
            _model = _load_model_compat(MODEL_PATH)
        _model.trainable = False
        print("[GRU xApp] Model loaded ✓")

        if SCALER_PATH.exists():
            _scaler = joblib.load(str(SCALER_PATH))
            print("[GRU xApp] Scaler loaded ✓")
        else:
            print(f"[GRU xApp] WARNING: scaler not found at {SCALER_PATH}")

        if CONFIG_PATH.exists():
            _config = joblib.load(str(CONFIG_PATH))
            print(f"[GRU xApp] Config loaded ✓  {_config}")


# ══════════════════════════════════════════════════════════════════════════════
# FEATURE PREPARATION
# ══════════════════════════════════════════════════════════════════════════════

def _build_feature_vector(data: dict) -> np.ndarray:
    """Map incoming measurement dict → (1, 12) float32 array."""
    fmap = {
        "Level":          data.get("serving_rsrp",  data.get("Level",         0.0)),
        "Qual":           data.get("serving_qual",   data.get("Qual",          0.0)),
        "SNR":            data.get("serving_sinr",   data.get("SNR",           0.0)),
        "CQI":            data.get("cqi",            data.get("CQI",           10.0)),
        "SecondCell_RSRP":data.get("neighbor_rsrp",  data.get("SecondCell_RSRP",0.0)),
        "SecondCell_SNR": data.get("neighbor_sinr",  data.get("SecondCell_SNR", 0.0)),
        "NRxLev1":        data.get("nrxlev1",        data.get("NRxLev1",       0.0)),
        "NQual1":         data.get("nqual1",         data.get("NQual1",        0.0)),
        "Speed":          data.get("speed",          data.get("Speed",         3.0)),
        "DL_bitrate":     data.get("dl_bitrate",     data.get("DL_bitrate",    0.0)),
        "UL_bitrate":     data.get("ul_bitrate",     data.get("UL_bitrate",    0.0)),
        "BANDWIDTH":      data.get("bandwidth",      data.get("BANDWIDTH",     20.0)),
    }
    return np.array([[fmap[f] for f in EXPECTED_FEATURES]], dtype=np.float32)


def _update_ue_window(ue_id: int, feat_vec: np.ndarray) -> np.ndarray:
    """
    Append feat_vec to UE's rolling window.
    Returns (1, WINDOW_SIZE, 12) array, padding with repeats if < WINDOW_SIZE.
    """
    with _ue_windows_lock:
        if ue_id not in _ue_windows:
            _ue_windows[ue_id] = []
        _ue_windows[ue_id].append(feat_vec[0].tolist())
        if len(_ue_windows[ue_id]) > WINDOW_SIZE:
            _ue_windows[ue_id].pop(0)
        window = _ue_windows[ue_id]

    # Pad to WINDOW_SIZE by repeating oldest entry
    while len(window) < WINDOW_SIZE:
        window = [window[0]] + window

    return np.array([window], dtype=np.float32)  # (1, 10, 12)


def _update_signal_history(ue_id: int, rsrp: float, snr: float, bearing: float = None):
    """
    Track raw RSRP/SNR/bearing per UE with timestamps for slope computation.
    Uses (timestamp, rsrp, snr) tuples so we can compute df/dt correctly.
    """
    ts = time.time()
    with _ue_signal_lock:
        if ue_id not in _ue_signal_history:
            _ue_signal_history[ue_id] = []
            _ue_bearing_history[ue_id] = []
        _ue_signal_history[ue_id].append((ts, rsrp, snr))
        if len(_ue_signal_history[ue_id]) > SLOPE_WINDOW:
            _ue_signal_history[ue_id].pop(0)
        if bearing is not None:
            _ue_bearing_history[ue_id].append(bearing)
            if len(_ue_bearing_history[ue_id]) > SLOPE_WINDOW:
                _ue_bearing_history[ue_id].pop(0)


def _instantaneous_slope(entries: list, col: int) -> float:
    """
    Compute instantaneous slope at the last entry using finite differences,
    matching the paper's  df['Level'].diff() / df['ElapsedTime'].diff()  formula.

    entries: list of (timestamp, rsrp, snr) tuples
    col:     1 = RSRP, 2 = SNR
    Returns: last-step slope in units/second (0.0 if fewer than 2 points).
    """
    if len(entries) < 2:
        return 0.0
    t1, v1 = entries[-2][0], entries[-2][col]
    t2, v2 = entries[-1][0], entries[-1][col]
    dt = t2 - t1
    if dt < 1e-6:
        return 0.0
    return (v2 - v1) / dt


import math as _math

def _compute_gps_bearing(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Compute compass bearing (degrees) from (lat1,lon1) to (lat2,lon2)."""
    lat1, lon1, lat2, lon2 = map(_math.radians, [lat1, lon1, lat2, lon2])
    dlon = lon2 - lon1
    y = _math.sin(dlon) * _math.cos(lat2)
    x = _math.cos(lat1) * _math.sin(lat2) - _math.sin(lat1) * _math.cos(lat2) * _math.cos(dlon)
    return (_math.degrees(_math.atan2(y, x)) + 360.0) % 360.0

def _update_ue_position(ue_id: int, lat: float, lon: float):
    with _ue_feature_lock:
        if ue_id not in _ue_position_history:
            _ue_position_history[ue_id] = []
        _ue_position_history[ue_id].append((lat, lon))
        if len(_ue_position_history[ue_id]) > 5:
            _ue_position_history[ue_id].pop(0)

def _get_bearing_from_gps(ue_id: int) -> float | None:
    with _ue_feature_lock:
        hist = list(_ue_position_history.get(ue_id, []))
    if len(hist) < 2:
        return None
    (lat1, lon1), (lat2, lon2) = hist[-2], hist[-1]
    # If UE hasn't moved, bearing change is 0 (not moving away)
    if abs(lat2 - lat1) < 1e-9 and abs(lon2 - lon1) < 1e-9:
        return 0.0
    return _compute_gps_bearing(lat1, lon1, lat2, lon2)

def _get_bearing_change(ue_id: int) -> float:
    # First try GPS-based bearing change (more accurate)
    gps = _get_bearing_from_gps(ue_id)
    if gps is not None:
        with _ue_feature_lock:
            hist = list(_ue_position_history.get(ue_id, []))
        if len(hist) >= 3:
            prev_b = _compute_gps_bearing(hist[-3][0], hist[-3][1], hist[-2][0], hist[-2][1])
            curr_b = gps
            diff = abs(curr_b - prev_b)
            return min(diff, 360.0 - diff)
    # Fall back to bearing from explicit bearing field
    with _ue_signal_lock:
        hist2 = _ue_bearing_history.get(ue_id, [])
    if len(hist2) < 2:
        return 0.0
    diff = abs(hist2[-1] - hist2[-2])
    return min(diff, 360.0 - diff)


# ══════════════════════════════════════════════════════════════════════════════
# GRU INFERENCE
# ══════════════════════════════════════════════════════════════════════════════

def _run_gru(feat_vec: np.ndarray, ue_id: int = 0, bearing: float = None) -> dict:
    """
    Run GRU model on feat_vec and apply the paper's two algorithms:

    Algorithm 1 — Ping-Pong Detection (Section 3.1):
      short     = predicted_ToS < ToS_th (1.2 s)
      osc       = |RSRP_slope| > rsrp_th  OR  |SNR_slope| > snr_th
      model_pp  = predicted_class == 2
      is_pp     = (short AND osc) OR model_pp

    Algorithm 2 — Unnecessary Handover Avoidance (Section 3.2):
      safe      = serving_RSRP > level_th (-110 dBm)
      maway     = |bearing_change| > bearing_th (45°)
      unnec     = safe AND maway AND (predicted_ToS < ToS_unnec_th (1.35 s))

    Final decision: AVOID if (is_pp OR unnec), else EXECUTE
    """
    if _model is None:
        load_model()

    # ── Extract raw signals BEFORE scaling (for Algorithm 1 slope check) ──────
    # EXPECTED_FEATURES order: Level(0), Qual(1), SNR(2), ...
    raw_rsrp = float(feat_vec[0, 0])   # Level / RSRP
    raw_snr  = float(feat_vec[0, 2])   # SNR
    _update_signal_history(ue_id, raw_rsrp, raw_snr, bearing)

    # Cold-start: if fewer than 2 signal readings, trust SINR (don't block)
    with _ue_signal_lock:
        n_readings = len(_ue_signal_history.get(ue_id, []))
    if n_readings < 2:
        return {
            "should_handover": True,
            "target_cell": 0,
            "confidence": 1.0,
            "time_to_handover": 9.9,
            "raw_predictions": [],
            "is_ping_pong": False, "is_unnecessary": False,
            "is_short_stay": False, "is_oscillating": False,
            "rsrp_slope": 0.0, "snr_slope": 0.0,
            "bearing_change": 0.0, "predicted_class": 0,
            "class_name": "No_HO", "cold_start": True
        }

    # ── Scale for model ───────────────────────────────────────────────────────
    if _scaler is not None:
        feat_scaled = _scaler.transform(feat_vec)
    else:
        feat_scaled = feat_vec

    # ── Build sequence (1, WINDOW_SIZE, 12) ───────────────────────────────────
    X_seq = _update_ue_window(ue_id, feat_scaled)

    preds = _model.predict(X_seq, verbose=0)

    # ── Parse multi-output predictions ───────────────────────────────────────
    if isinstance(preds, (list, tuple)):
        preds_arr = np.concatenate([np.asarray(p) for p in preds], axis=1)
    elif isinstance(preds, dict):
        preds_arr = np.concatenate([np.asarray(p) for p in preds.values()], axis=1)
    else:
        preds_arr = np.asarray(preds)

    if preds_arr.shape[1] >= 4:
        tos_pred    = float(preds_arr[0, 0])
        class_probs = preds_arr[0, 1:4]
        best_class  = int(np.argmax(class_probs))
        confidence  = float(np.max(class_probs))
        target_cell = best_class if best_class > 0 else 0
    else:
        prob        = float(preds_arr[0, 0]) if preds_arr.ndim > 1 else float(preds_arr[0])
        tos_pred    = 0.0
        best_class  = 1 if prob > 0.5 else 0
        confidence  = float(abs(prob - 0.5) * 2)
        target_cell = 1 if best_class > 0 else 0
        class_probs = np.array([1 - prob, prob, 0.0])

    # ── Load decision thresholds from Fares config.joblib ────────────────────
    if _config:
        ToS_th       = float(_config.get("ToS_th",       1.2))
        ToS_unnec_th = float(_config.get("ToS_unnec_th", 1.35))
        level_th     = float(_config.get("level_th",    -110.0))
        bearing_th   = float(_config.get("bearing_th",   45.0))
        rsrp_th      = float(_config.get("rsrp_th",       3.0))
        snr_th       = float(_config.get("snr_th",        3.0))
    else:
        ToS_th, ToS_unnec_th = 1.2, 1.35
        level_th, bearing_th = -110.0, 45.0
        rsrp_th, snr_th      = 3.0, 3.0

    # ── Algorithm 1 — Ping-Pong Detection ────────────────────────────────────
    with _ue_signal_lock:
        sig_hist = list(_ue_signal_history.get(ue_id, []))
    # Instantaneous slope: (last_val - prev_val) / dt   [units/second]
    rsrp_slope = _instantaneous_slope(sig_hist, col=1)
    snr_slope  = _instantaneous_slope(sig_hist, col=2)

    is_short    = tos_pred < ToS_th
    is_osc      = (abs(rsrp_slope) > rsrp_th) or (abs(snr_slope) > snr_th)
    # Block ping-pong when model predicts class 2 with confidence > 0.82
    is_model_pp = (best_class == 2) and (confidence > 0.82)
    is_pp       = (is_short and is_osc) or is_model_pp

    # ── Algorithm 2 — Unnecessary Handover Avoidance ─────────────────────────
    is_safe      = raw_rsrp > level_th
    bearing_chg  = _get_bearing_change(ue_id)
    is_maway     = bearing_chg > bearing_th
    is_unnec     = is_safe and is_maway and (tos_pred < ToS_unnec_th)

    # ── Final decision ────────────────────────────────────────────────────────
    should_avoid = is_pp or is_unnec
    should_ho    = not should_avoid

    return {
        "should_handover":  bool(should_ho),
        "target_cell":      int(target_cell),
        "confidence":       confidence,
        "time_to_handover": tos_pred,
        "raw_predictions":  preds_arr[0].tolist(),
        # Algorithm diagnostics
        "is_ping_pong":     bool(is_pp),
        "is_unnecessary":   bool(is_unnec),
        "is_short_stay":    bool(is_short),
        "is_oscillating":   bool(is_osc),
        "rsrp_slope":       round(rsrp_slope, 4),
        "snr_slope":        round(snr_slope, 4),
        "bearing_change":   round(bearing_chg, 2),
        "predicted_class":  int(best_class),
        "class_name":       ["No_HO", "Normal_HO", "Ping-Pong"][best_class] if best_class < 3 else "Unknown",
    }


# ══════════════════════════════════════════════════════════════════════════════
# INFLUXDB PUSH
# ══════════════════════════════════════════════════════════════════════════════

_influx_client = None

def _get_influx():
    global _influx_client
    if not _INFLUX_AVAILABLE:
        return None
    if _influx_client is None:
        try:
            _influx_client = InfluxDBClient(
                host=INFLUX_HOST, port=INFLUX_PORT,
                username="root", password="root",
                database=INFLUX_DB
            )
            _influx_client.create_database(INFLUX_DB)
        except Exception as e:
            print(f"[GRU xApp] InfluxDB unavailable: {e}")
            _influx_client = None
    return _influx_client


def _push_to_influx(ue_id: int, result: dict, decision_source: str = "xapp"):
    """Push GRU decision metrics to InfluxDB for Grafana dashboard."""
    client = _get_influx()
    if client is None:
        return

    points = [
        {
            "measurement": f"gru_decision_ue_{ue_id}",
            "fields": {
                "should_handover":  int(result["should_handover"]),
                "confidence":       result["confidence"],
                "time_to_handover": result["time_to_handover"],
                "target_cell":      result["target_cell"],
                "is_ping_pong":     int(result.get("is_ping_pong", False)),
                "is_unnecessary":   int(result.get("is_unnecessary", False)),
                "is_oscillating":   int(result.get("is_oscillating", False)),
                "rsrp_slope":       result.get("rsrp_slope", 0.0),
                "bearing_change":   result.get("bearing_change", 0.0),
                "predicted_class":  result.get("predicted_class", 0),
            },
            "tags": {"source": decision_source},
        },
    ]

    with _stats_lock:
        points.append({
            "measurement": "gru_xapp_totals",
            "fields": {
                "total_predictions":   _stats["total_predictions"],
                "handovers_executed":  _stats["handovers_executed"],
                "handovers_prevented": _stats["handovers_prevented"],
                "ping_pong_blocks":    _stats["ping_pong_blocks"],
                "unnecessary_blocks":  _stats["unnecessary_blocks"],
            },
        })

    try:
        client.write_points(points)
    except Exception as e:
        print(f"[GRU xApp] InfluxDB write error: {e}")


# ══════════════════════════════════════════════════════════════════════════════
# NS-3 LOG MONITOR (autonomous inference for GUI)
# ══════════════════════════════════════════════════════════════════════════════

def _parse_du_cell_line(headers: list, fields: list) -> dict | None:
    """
    Parse a line from du-cell-*.txt produced by ns-3 / sim_data_pusher.
    Returns a measurement dict or None if line is malformed.
    """
    if len(fields) < len(headers):
        return None
    row = {h.strip().lower(): f.strip() for h, f in zip(headers, fields)}

    def safe_float(key, default=0.0):
        try:
            return float(row.get(key, default))
        except (ValueError, TypeError):
            return default

    # Map ns-3 field names → GRU feature names
    return {
        "ue_id":         int(safe_float("id", 0)),
        "serving_sinr":  safe_float("l3 serving sinr"),
        "serving_rsrp":  safe_float("l3 serving sinr") - 10.0,
        "serving_qual":  safe_float("l3 serving sinr") - 5.0,
        "neighbor_sinr": safe_float("l3 neigh sinr 1"),
        "neighbor_rsrp": safe_float("l3 neigh sinr 1") - 10.0,
        "neighbor_cell_id": int(safe_float("l3 neigh id 1 (cellid)", 0)),
        "cqi":           safe_float("drb.uethpdl.ueid", 10.0),
        "speed":         3.0,
        "dl_bitrate":    safe_float("drb.pdcpsduvolumedl_filter.ueid(txbytes)"),
        "ul_bitrate":    0.0,
        "bandwidth":     20.0,
    }


# lstm_features.csv format (written by ns-3 scenario):
# Col 0: Time, 1: IMSI, 2: Level(RSRP), 3: Qual, 4: SNR, 5: CQI,
#     6: SecondCell_RSRP, 7: SecondCell_SNR, 8: NRxLev1, 9: NQual1,
#    10: Speed, 11: DL_bitrate, 12: UL_bitrate, 13: BANDWIDTH,
#    14: serving_cell, 15: best_neigh_cell
LSTM_CSV    = Path.home() / "lstm_features.csv"

# kpm_handover_features.csv format (contains GPS lat/lon):
# Col 0: SessionID, 1: ElapsedTime, 2: Node, 3: CellID, 4: Level, 5: Qual,
#     6: SNR, 7: CQI, 8: SecondCell_RSRP, 9: SecondCell_SNR,
#    10: NRxLev1, 11: NQual1, 12: Speed, 13: DL_bitrate, 14: UL_bitrate,
#    15: BANDWIDTH, 16: Latitude, 17: Longitude
KPM_CSV     = Path.home() / "kpm_handover_features.csv"


def _ns3_log_monitor():
    """
    Background thread: monitors ~/lstm_features.csv (written by ns-3 scenario)
    and runs GRU autonomously on every new row, pushing decisions to InfluxDB.
    Uses correct column indices:
      IMSI=1, Level=2, Qual=3, SNR=4, CQI=5,
      SecondCell_RSRP=6, SecondCell_SNR=7, NRxLev1=8, NQual1=9,
      Speed=10, DL_bitrate=11, UL_bitrate=12, BANDWIDTH=13
    """
    print(f"[GRU xApp] Log monitor watching: {LSTM_CSV}")
    csv_pos = 0
    header_done = False

    while True:
        try:
            if LSTM_CSV.exists():
                with open(str(LSTM_CSV), "r") as f:
                    f.seek(csv_pos)
                    new_lines = f.readlines()
                    csv_pos = f.tell()

                for line in new_lines:
                    line = line.strip()
                    if not line:
                        continue
                    # Skip header (first row or any row starting with "Time")
                    if not header_done or line.startswith("Time"):
                        header_done = True
                        if line.startswith("Time"):
                            continue

                    row = line.split(",")
                    if len(row) < 14:       # need at least cols 0-13
                        continue

                    try:
                        ue_id = int(float(row[1]))   # IMSI
                        feat_dict = {
                            "Level":           float(row[2]),   # RSRP dBm
                            "Qual":            float(row[3]),
                            "SNR":             float(row[4]),
                            "CQI":             float(row[5]),
                            "SecondCell_RSRP": float(row[6]),
                            "SecondCell_SNR":  float(row[7]),
                            "NRxLev1":         float(row[8]),
                            "NQual1":          float(row[9]),
                            "Speed":           float(row[10]),
                            "DL_bitrate":      float(row[11]),
                            "UL_bitrate":      float(row[12]),
                            "BANDWIDTH":       float(row[13]),
                        }
                        with _ue_feature_lock:
                            _ue_latest_full_features[ue_id] = {**feat_dict, "ue_id": ue_id}

                        feat    = _build_feature_vector(feat_dict)
                        bearing = _get_bearing_from_gps(ue_id)
                        result  = _run_gru(feat, ue_id, bearing=bearing)

                        with _stats_lock:
                            _stats["total_predictions"] += 1
                            if result["should_handover"]:
                                _stats["handovers_executed"] += 1
                            else:
                                _stats["handovers_prevented"] += 1
                                if result["is_ping_pong"]:
                                    _stats["ping_pong_blocks"] += 1
                                if result["is_unnecessary"]:
                                    _stats["unnecessary_blocks"] += 1

                        _push_to_influx(ue_id, result, decision_source="log_monitor")

                    except (ValueError, IndexError):
                        continue

        except Exception:
            pass  # File not ready yet, skip silently

        time.sleep(1)


def _kpm_gps_monitor():
    """
    Background thread: monitors ~/kpm_handover_features.csv for GPS coordinates.
    Updates _ue_position_history so Algorithm 2 bearing change is accurate.
    Format: SessionID(0), ElapsedTime(1), Node(2), CellID(3), Level(4..15),
            Latitude(16), Longitude(17)
    Node field is 'UE_<n>' — extract <n> as ue_id.
    """
    print(f"[GRU xApp] GPS monitor watching: {KPM_CSV}")
    kpm_pos    = 0
    kpm_header = False

    while True:
        try:
            if KPM_CSV.exists():
                with open(str(KPM_CSV), "r") as f:
                    f.seek(kpm_pos)
                    new_lines = f.readlines()
                    kpm_pos = f.tell()

                for line in new_lines:
                    line = line.strip()
                    if not line:
                        continue
                    if not kpm_header or line.startswith("SessionID"):
                        kpm_header = True
                        if line.startswith("SessionID"):
                            continue

                    row = line.split(",")
                    if len(row) < 18:   # need cols 0-17
                        continue

                    try:
                        node = row[2].strip()      # e.g. "UE_1" or "1"
                        if node.startswith("UE_"):
                            ue_id = int(node[3:])
                        else:
                            ue_id = int(float(node))
                        lat = float(row[16])
                        lon = float(row[17])
                        _update_ue_position(ue_id, lat, lon)
                    except (ValueError, IndexError):
                        continue

        except Exception:
            pass

        time.sleep(1)


# ══════════════════════════════════════════════════════════════════════════════
# FLASK REST API  (called by C xApp best2.c via lstm_predictor_client.c)
# ══════════════════════════════════════════════════════════════════════════════

app = Flask(__name__)
CORS(app)


@app.route("/health", methods=["GET"])
def health():
    return jsonify({
        "status": "ok",
        "model_loaded": _model is not None,
        "scaler_loaded": _scaler is not None,
        "influxdb": _INFLUX_AVAILABLE and _get_influx() is not None,
        "stats": _stats,
    })


@app.route("/predict", methods=["POST"])
def predict():
    """Single UE prediction — called by C xApp."""
    try:
        data = request.json
        if not data:
            return jsonify({"error": "No data provided"}), 400

        ue_id = int(data.get("ue_id", 0))

        # Merge C xApp data with latest full features from ns-3 log (if available)
        with _ue_feature_lock:
            cached = dict(_ue_latest_full_features.get(ue_id, {}))
        if cached:
            # Cached ns-3 data has real RSRP; C xApp data has real neighbor info
            merged = {**cached, **data}  # data overrides cached for same keys
            feat = _build_feature_vector(merged)
        else:
            feat = _build_feature_vector(data)

        # Get GPS-based bearing
        bearing = _get_bearing_from_gps(ue_id)
        if bearing is None and "bearing" in data:
            bearing = float(data["bearing"])
        result = _run_gru(feat, ue_id, bearing=bearing)

        # Use the C xApp's neighbor_cell_id as ground-truth target if HO recommended
        if result["should_handover"] and data.get("neighbor_cell_id"):
            result["target_cell"] = int(data["neighbor_cell_id"])

        # Update stats (Algorithm-aware)
        with _stats_lock:
            _stats["total_predictions"] += 1
            if result["should_handover"]:
                _stats["handovers_executed"] += 1
            else:
                _stats["handovers_prevented"] += 1
                if result["is_ping_pong"]:
                    _stats["ping_pong_blocks"] += 1
                if result["is_unnecessary"]:
                    _stats["unnecessary_blocks"] += 1

        # Push to InfluxDB for GUI
        _push_to_influx(ue_id, result, decision_source="c_xapp")

        return jsonify(result)

    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/predict_batch", methods=["POST"])
def predict_batch():
    """Batch prediction — multiple UEs in one call."""
    try:
        data = request.json
        if "ue_measurements" not in data:
            return jsonify({"error": "ue_measurements array required"}), 400

        results = []
        for ue_data in data["ue_measurements"]:
            feat    = _build_feature_vector(ue_data)
            ue_id   = int(ue_data.get("ue_id", 0))
            bearing = float(ue_data["bearing"]) if "bearing" in ue_data else None
            result  = _run_gru(feat, ue_id, bearing=bearing)
            result["ue_id"] = ue_id

            if result["should_handover"] and ue_data.get("neighbor_cell_id"):
                result["target_cell"] = int(ue_data["neighbor_cell_id"])

            with _stats_lock:
                _stats["total_predictions"] += 1
                if result["should_handover"]:
                    _stats["handovers_executed"] += 1
                else:
                    _stats["handovers_prevented"] += 1
                    if result["is_ping_pong"]:
                        _stats["ping_pong_blocks"] += 1
                    if result["is_unnecessary"]:
                        _stats["unnecessary_blocks"] += 1

            _push_to_influx(ue_id, result, decision_source="c_xapp_batch")
            results.append(result)

        return jsonify({"predictions": results})

    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/stats", methods=["GET"])
def stats():
    """GRU decision statistics — readable by Grafana SimpleJSON source."""
    with _stats_lock:
        return jsonify(_stats)


@app.route("/metrics", methods=["GET"])
def metrics():
    """Paper-style performance metrics."""
    with _stats_lock:
        s = dict(_stats)
    total = s["total_predictions"]
    if total == 0:
        return jsonify({"status": "no predictions yet"})
    return jsonify({
        "total_predictions":      total,
        "handovers_executed":     s["handovers_executed"],
        "handovers_prevented":    s["handovers_prevented"],
        "ping_pong_blocks":       s["ping_pong_blocks"],
        "unnecessary_blocks":     s["unnecessary_blocks"],
        "prevention_rate_pct":    round(100.0 * s["handovers_prevented"] / total, 2),
        "ping_pong_rate_pct":     round(100.0 * s["ping_pong_blocks"] / max(1, s["handovers_prevented"]), 2),
        "ues_tracked":            len(_ue_windows),
    })


# ══════════════════════════════════════════════════════════════════════════════
# ENTRY POINT
# ══════════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    print("=" * 64)
    print("  GRU xApp — O-RAN Handover Optimization")
    print("  Fares GRU model + InfluxDB/Grafana dashboard")
    print("=" * 64)
    print(f"  Model dir : {_FARES_DIR}")
    print(f"  Model     : {MODEL_PATH}")
    print(f"  Port      : {GRU_PORT}")
    print(f"  InfluxDB  : {INFLUX_HOST}:{INFLUX_PORT}/{INFLUX_DB}  "
          f"({'available' if _INFLUX_AVAILABLE else 'influxdb-client not installed'})")
    print(f"  NS3 logs  : {NS3_LOG_DIR}")
    print("=" * 64)

    # Pre-load model at startup
    try:
        load_model()
    except Exception as e:
        print(f"[GRU xApp] WARNING: Could not pre-load model: {e}")
        print("[GRU xApp] Service will start but predictions will fail until model is available")

    # Start ns-3 log monitor in background thread
    monitor_thread = threading.Thread(target=_ns3_log_monitor, daemon=True)
    monitor_thread.start()
    print("[GRU xApp] Log monitor started (watching lstm_features.csv)")

    # Start GPS/bearing monitor (reads kpm_handover_features.csv for lat/lon)
    gps_thread = threading.Thread(target=_kpm_gps_monitor, daemon=True)
    gps_thread.start()
    print("[GRU xApp] GPS monitor started (watching kpm_handover_features.csv)")

    # Test InfluxDB connection
    if _INFLUX_AVAILABLE:
        client = _get_influx()
        if client:
            print("[GRU xApp] InfluxDB connected — GUI metrics ACTIVE")
        else:
            print("[GRU xApp] InfluxDB not reachable — GUI metrics DISABLED")
    else:
        print("[GRU xApp] influxdb-client not installed — install with: pip install influxdb")

    print(f"\n[GRU xApp] REST service starting on http://0.0.0.0:{GRU_PORT}")
    print(f"[GRU xApp] Endpoints: /health  /predict  /predict_batch  /stats\n")

    app.run(host="0.0.0.0", port=GRU_PORT, debug=False, threaded=True)
