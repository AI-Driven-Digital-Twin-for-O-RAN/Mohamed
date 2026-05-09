#!/usr/bin/env python3
"""
Handover Optimization - Simple Inference Script
================================================
Predicts whether to AVOID or EXECUTE handovers.

Usage:
    python run_inference.py --input your_data.csv --output predictions.csv
"""
import argparse
import os
import sys
import numpy as np
import pandas as pd
import joblib

# Suppress TensorFlow warnings
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
os.environ['TF_ENABLE_ONEDNN_OPTS'] = '0'

import warnings
warnings.filterwarnings('ignore')

print("Loading TensorFlow...")
import tensorflow as tf
tf.get_logger().setLevel('ERROR')


def build_model(window_size, n_features):
    """Rebuild model architecture (for compatibility)."""
    from tensorflow.keras.models import Model
    from tensorflow.keras.layers import Input, GRU, Dense, Dropout
    
    inputs = Input(shape=(window_size, n_features))
    x = GRU(128, return_sequences=True)(inputs)
    x = Dropout(0.3)(x)
    x = GRU(64)(x)
    x = Dense(128, activation='relu')(x)
    x = Dropout(0.3)(x)
    tos_output = Dense(1, name='ToS_out')(x)
    class_output = Dense(3, activation='softmax', name='Class_out')(x)
    
    return Model(inputs=inputs, outputs=[tos_output, class_output])


def preprocess_data(df):
    """Preprocess raw data."""
    df = df.sort_values(['SessionID', 'ElapsedTime']).reset_index(drop=True)
    
    # Handover type
    curr = df[['Node', 'CellID']]
    prev = curr.shift(1)
    two_back = curr.shift(2)
    df['Handover_type'] = np.where((curr == prev).all(axis=1), 0,
                          np.where((curr == two_back).all(axis=1), 2, 1))
    df.loc[:1, 'Handover_type'] = 0
    
    # Time of Stay
    next_cell = curr.shift(-1)
    time_diff = df['ElapsedTime'].shift(-1) - df['ElapsedTime']
    df['ToS'] = np.where((curr == next_cell).all(axis=1), time_diff, 0).astype(float)
    df['ToS'] = df['ToS'].fillna(0)
    
    # Signal slopes
    dt = df['ElapsedTime'].diff()
    df['RSRP_slopes'] = df['Level'].diff() / dt
    df['SNR_slopes'] = df['SNR'].diff() / dt
    df.replace([np.inf, -np.inf], np.nan, inplace=True)
    
    # Bearing
    lat, lon = np.radians(df['Latitude']), np.radians(df['Longitude'])
    prev_lat = df.groupby('SessionID')['Latitude'].shift(1).apply(np.radians)
    prev_lon = df.groupby('SessionID')['Longitude'].shift(1).apply(np.radians)
    dLon = lon - prev_lon
    y = np.sin(dLon) * np.cos(lat)
    x = np.cos(prev_lat) * np.sin(lat) - np.sin(prev_lat) * np.cos(lat) * np.cos(dLon)
    df['Bearing'] = ((np.degrees(np.arctan2(y, x)) + 360) % 360).fillna(0.0)
    
    return df


def create_sequences(df, features, window_size):
    """Create sliding window sequences."""
    data = df[features].to_numpy(dtype=np.float32)
    n = len(df) - window_size
    if n <= 0:
        raise ValueError(f"Need at least {window_size + 1} rows.")
    return np.array([data[i:i+window_size] for i in range(n)])


def get_last_values(df, col, window_size):
    """Get last value in each window."""
    vals = df[col].fillna(0.0).to_numpy()
    return np.array([vals[i + window_size - 1] for i in range(len(df) - window_size)])


def main():
    parser = argparse.ArgumentParser(description='Handover Prediction')
    parser.add_argument('--input', '-i', required=True, help='Input CSV')
    parser.add_argument('--output', '-o', default='predictions.csv', help='Output CSV')
    args = parser.parse_args()
    
    print("\n" + "=" * 50)
    print("  HANDOVER OPTIMIZATION - INFERENCE")
    print("=" * 50)
    
    # Load config
    print("\n[1/5] Loading configuration...")
    config = joblib.load('artifacts/config.joblib')
    scaler = joblib.load('artifacts/scaler.joblib')
    
    WINDOW_SIZE = config['WINDOW_SIZE']
    feature_columns = config['feature_columns']
    
    # Load or rebuild model
    print("[2/5] Loading model...")
    model_path = 'model/handover_model_final.keras'
    weights_path = 'model/model_weights.h5'
    
    try:
        model = tf.keras.models.load_model(model_path, compile=False)
    except:
        print("   Rebuilding model architecture...")
        model = build_model(WINDOW_SIZE, len(feature_columns))
        if os.path.exists(weights_path):
            model.load_weights(weights_path)
        else:
            model.load_weights(model_path)
    
    # Load data
    print(f"[3/5] Loading data: {args.input}")
    df = pd.read_csv(args.input)
    print(f"   {len(df)} rows loaded")
    
    df = preprocess_data(df)
    df_original = df.copy()
    
    # Normalize
    print("[4/5] Preprocessing...")
    df[feature_columns] = df[feature_columns].apply(pd.to_numeric, errors='coerce').fillna(0.0)
    df[feature_columns] = scaler.transform(df[feature_columns])
    
    # Create sequences and predict
    X = create_sequences(df, feature_columns, WINDOW_SIZE)
    print(f"   {len(X)} sequences created")
    
    print("[5/5] Predicting...")
    pred_tos, pred_class = model.predict(X, verbose=0)
    pred_tos_flat = pred_tos.flatten()
    pred_class_labels = np.argmax(pred_class, axis=1)
    
    # Algorithm 1: Detection
    ToS_th = config['ToS_th']
    rsrp_th = config['rsrp_th']
    snr_th = config['snr_th']
    
    test_rsrp_slopes = get_last_values(df, 'RSRP_slopes', WINDOW_SIZE)
    test_snr_slopes = get_last_values(df, 'SNR_slopes', WINDOW_SIZE)
    
    short = pred_tos_flat < ToS_th
    osc = (np.abs(test_rsrp_slopes) > rsrp_th) | (np.abs(test_snr_slopes) > snr_th)
    model_pp = (pred_class_labels == 2)
    is_pp = (short & osc) | model_pp
    
    # Algorithm 2: Avoidance
    level_th = config['level_th']
    ToS_unnec_th = config['ToS_unnec_th']
    bearing_th = config['bearing_th']
    
    test_rsrp = get_last_values(df_original, 'Level', WINDOW_SIZE)
    bearing = df_original['Bearing'].fillna(0.0).to_numpy()
    n = len(df_original) - WINDOW_SIZE
    bear_curr = np.array([bearing[i + WINDOW_SIZE - 1] for i in range(n)])
    bear_prev = np.array([bearing[i + WINDOW_SIZE - 2] for i in range(n)])
    
    bear_diff = np.abs(bear_curr - bear_prev)
    bear_diff = np.minimum(bear_diff, 360 - bear_diff)
    
    safe = test_rsrp > level_th
    moving_away = bear_diff > bearing_th
    unnec = safe & moving_away & (pred_tos_flat < ToS_unnec_th)
    
    avoid = is_pp | unnec
    
    # Results
    results = pd.DataFrame({
        'index': range(WINDOW_SIZE, len(df)),
        'predicted_tos': np.round(pred_tos_flat, 3),
        'predicted_class': pred_class_labels,
        'class_name': ['No_HO' if c == 0 else 'Normal' if c == 1 else 'Ping-Pong' for c in pred_class_labels],
        'ping_pong_detected': is_pp.astype(int),
        'unnecessary_detected': unnec.astype(int),
        'decision': ['AVOID' if a else 'EXECUTE' for a in avoid]
    })
    
    results.to_csv(args.output, index=False)
    
    # Summary
    n_avoid = np.sum(avoid)
    n_execute = np.sum(~avoid)
    
    print("\n" + "=" * 50)
    print("  RESULTS")
    print("=" * 50)
    print(f"  Total:   {len(results)}")
    print(f"  AVOID:   {n_avoid} ({n_avoid/len(results)*100:.1f}%)")
    print(f"  EXECUTE: {n_execute} ({n_execute/len(results)*100:.1f}%)")
    print(f"\n  Saved to: {args.output}")
    print("=" * 50 + "\n")


if __name__ == "__main__":
    main()
