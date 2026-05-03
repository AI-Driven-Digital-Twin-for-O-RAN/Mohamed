#!/usr/bin/env python3
"""
LSTM Model Prediction Service for O-RAN xApp
Provides REST API endpoint for handover decision predictions
"""

import json
import sys
import os
from pathlib import Path
import numpy as np
import joblib
import tensorflow as tf
from flask import Flask, request, jsonify
from flask_cors import CORS

# Paths to model and scaler (override with env LSTM_MODEL_DIR if needed)
# Service is at: yousef_fathy/HANDOVER_xApp_Test/lstm_predictor_service.py
# Fares repo is at: open-ran-clean/Fares/
_default_model_dir = Path(__file__).parent.parent.parent / "Fares"
MODEL_DIR = Path(os.environ.get("LSTM_MODEL_DIR", _default_model_dir))
MODEL_PATH = MODEL_DIR / "model" / "handover_model_final.keras"
SCALER_PATH = MODEL_DIR / "artifacts" / "scaler.joblib"

# Expected features (12 features matching scaler)
EXPECTED_FEATURES = [
    "Level", "Qual", "SNR", "CQI",
    "SecondCell_RSRP", "SecondCell_SNR",
    "NRxLev1", "NQual1",
    "Speed", "DL_bitrate", "UL_bitrate", "BANDWIDTH"
]

app = Flask(__name__)
CORS(app)

# Global model and scaler
model = None
scaler = None


def load_model():
    """Load LSTM model and scaler"""
    global model, scaler
    
    if model is None:
        print(f"[LSTM Service] Loading model from {MODEL_PATH}")
        try:
            model = tf.keras.models.load_model(str(MODEL_PATH), compile=False)
            model.trainable = False
            print("[LSTM Service] Model loaded successfully")
        except Exception as e:
            print(f"[LSTM Service] Error loading model: {e}")
            raise
    
    if scaler is None:
        print(f"[LSTM Service] Loading scaler from {SCALER_PATH}")
        try:
            scaler = joblib.load(str(SCALER_PATH))
            print("[LSTM Service] Scaler loaded successfully")
        except Exception as e:
            print(f"[LSTM Service] Error loading scaler: {e}")
            scaler = None  # Continue without scaler if not available


def prepare_features(data_dict):
    """
    Prepare features from input dictionary to match expected format.
    Maps O-RAN measurements to model features.
    
    Args:
        data_dict: Dictionary with measurement values
        
    Returns:
        numpy array of shape (1, 12) ready for model input
    """
    # Map O-RAN measurements to model features
    # You may need to adjust these mappings based on your actual data
    feature_map = {
        "Level": data_dict.get("serving_rsrp", data_dict.get("Level", 0)),
        "Qual": data_dict.get("serving_qual", data_dict.get("Qual", 0)),
        "SNR": data_dict.get("serving_sinr", data_dict.get("SNR", 0)),
        "CQI": data_dict.get("cqi", data_dict.get("CQI", 0)),
        "SecondCell_RSRP": data_dict.get("neighbor_rsrp", data_dict.get("SecondCell_RSRP", 0)),
        "SecondCell_SNR": data_dict.get("neighbor_sinr", data_dict.get("SecondCell_SNR", 0)),
        "NRxLev1": data_dict.get("nrxlev1", data_dict.get("NRxLev1", 0)),
        "NQual1": data_dict.get("nqual1", data_dict.get("NQual1", 0)),
        "Speed": data_dict.get("speed", data_dict.get("Speed", 0)),
        "DL_bitrate": data_dict.get("dl_bitrate", data_dict.get("DL_bitrate", 0)),
        "UL_bitrate": data_dict.get("ul_bitrate", data_dict.get("UL_bitrate", 0)),
        "BANDWIDTH": data_dict.get("bandwidth", data_dict.get("BANDWIDTH", 20)),
    }
    
    # Create feature array in correct order
    features = np.array([[feature_map[f] for f in EXPECTED_FEATURES]], dtype=np.float32)
    
    return features


def predict_handover(features):
    """
    Predict handover decision using LSTM model
    
    Args:
        features: numpy array of shape (1, 12) or (1, 10, 12) for sequence input
        
    Returns:
        dict with predictions:
        - should_handover: bool
        - target_cell: int (best neighbor cell ID)
        - confidence: float
        - predictions: array of raw model outputs
    """
    global model, scaler
    
    if model is None:
        load_model()
    
    # Scale features if scaler available
    if scaler is not None:
        X_proc = scaler.transform(features)
    else:
        X_proc = features
    
    # Check if model expects sequence input (timesteps > 1)
    input_shape = model.input_shape
    if len(input_shape) == 3 and input_shape[1] is not None:
        timesteps = input_shape[1]  # e.g., 10
        # Reshape to (1, timesteps, features)
        X_seq = np.repeat(X_proc[:, np.newaxis, :], timesteps, axis=1)
        preds = model.predict(X_seq, verbose=0)
    else:
        preds = model.predict(X_proc, verbose=0)
    
    # Handle multi-output models
    if isinstance(preds, (list, tuple)):
        preds_arr = np.concatenate([np.asarray(p) for p in preds], axis=1)
    elif isinstance(preds, dict):
        preds_arr = np.concatenate([np.asarray(p) for p in preds.values()], axis=1)
    else:
        preds_arr = np.asarray(preds)
    
    # Extract predictions
    # Assuming model outputs: [ToS_out, Class_out]
    # Class_out is 3-class classification (0=no handover, 1=handover to cell1, 2=handover to cell2)
    # ToS_out is time-to-handover prediction
    
    if preds_arr.shape[1] >= 4:
        # Multi-output: [ToS, Class probabilities]
        tos_pred = preds_arr[0, 0] if preds_arr.shape[1] > 0 else 0.0
        class_probs = preds_arr[0, 1:4] if preds_arr.shape[1] >= 4 else preds_arr[0, 1:]
        
        # Decision: handover if highest probability class is not 0
        best_class = np.argmax(class_probs)
        should_handover = best_class > 0
        confidence = float(np.max(class_probs))
        
        # Map class to target cell (you may need to adjust this mapping)
        target_cell = best_class if should_handover else 0
        
        return {
            "should_handover": bool(should_handover),
            "target_cell": int(target_cell),
            "confidence": float(confidence),
            "time_to_handover": float(tos_pred),
            "class_probabilities": class_probs.tolist(),
            "raw_predictions": preds_arr[0].tolist()
        }
    else:
        # Single output - treat as probability
        prob = float(preds_arr[0, 0]) if preds_arr.ndim > 1 else float(preds_arr[0])
        should_handover = prob > 0.5
        
        return {
            "should_handover": bool(should_handover),
            "target_cell": 1 if should_handover else 0,  # Default to cell 1
            "confidence": float(abs(prob - 0.5) * 2),  # Normalize to 0-1
            "raw_predictions": preds_arr[0].tolist() if preds_arr.ndim > 1 else [float(prob)]
        }


@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint"""
    return jsonify({"status": "ok", "model_loaded": model is not None, "scaler_loaded": scaler is not None})


@app.route('/predict', methods=['POST'])
def predict():
    """
    Predict handover decision
    
    Request body:
    {
        "serving_sinr": float,
        "serving_rsrp": float,
        "serving_qual": float,
        "neighbor_sinr": float,  # Best neighbor
        "neighbor_rsrp": float,
        "neighbor_cell_id": int,
        "cqi": float,
        "speed": float,
        "dl_bitrate": float,
        "ul_bitrate": float,
        "bandwidth": int,
        ... (other measurements)
    }
    
    Response:
    {
        "should_handover": bool,
        "target_cell": int,
        "confidence": float,
        "time_to_handover": float,
        "class_probabilities": [float, float, float]
    }
    """
    try:
        data = request.json
        
        if not data:
            return jsonify({"error": "No data provided"}), 400
        
        # Prepare features
        features = prepare_features(data)
        
        # Predict
        result = predict_handover(features)
        
        # Override target_cell with provided neighbor_cell_id if handover recommended
        if result["should_handover"] and "neighbor_cell_id" in data:
            result["target_cell"] = int(data["neighbor_cell_id"])
        
        return jsonify(result)
    
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route('/predict_batch', methods=['POST'])
def predict_batch():
    """
    Predict handover decisions for multiple UEs
    
    Request body:
    {
        "ue_measurements": [
            {
                "ue_id": int,
                "serving_sinr": float,
                "neighbor_sinr": float,
                "neighbor_cell_id": int,
                ...
            },
            ...
        ]
    }
    """
    try:
        data = request.json
        
        if "ue_measurements" not in data:
            return jsonify({"error": "ue_measurements array required"}), 400
        
        results = []
        for ue_data in data["ue_measurements"]:
            features = prepare_features(ue_data)
            result = predict_handover(features)
            result["ue_id"] = ue_data.get("ue_id", 0)
            if result["should_handover"] and "neighbor_cell_id" in ue_data:
                result["target_cell"] = int(ue_data["neighbor_cell_id"])
            results.append(result)
        
        return jsonify({"predictions": results})
    
    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == '__main__':
    print("[LSTM Service] Starting LSTM Prediction Service...")
    print(f"[LSTM Service] Model path: {MODEL_PATH}")
    print(f"[LSTM Service] Scaler path: {SCALER_PATH}")
    
    # Load model on startup
    try:
        load_model()
    except Exception as e:
        print(f"[LSTM Service] Warning: Could not load model: {e}")
        print("[LSTM Service] Service will start but predictions will fail")
    
    # Run Flask app (if PORT is set use it; else try 5000, then 5001, 5002...)
    port = int(os.environ.get('PORT', 0))
    if port == 0:
        import socket
        port = 5000
        for try_port in [5000, 5001, 5002, 5003]:
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                    s.bind(('', try_port))
                port = try_port
                break
            except OSError:
                continue
        else:
            print(f"[LSTM Service] Warning: Ports 5000-5003 busy, using 5000 anyway")
    print(f"[LSTM Service] Starting server on port {port}")
    if port != 5000:
        print(f"[LSTM Service] If xApp uses default URL, set: LSTM_SERVICE_URL=http://localhost:{port}")
    app.run(host='0.0.0.0', port=port, debug=False)
