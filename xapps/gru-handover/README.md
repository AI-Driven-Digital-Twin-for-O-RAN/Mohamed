# Handover Optimization Model

GRU-based model for predicting and avoiding ping-pong handovers in cellular networks.

Based on: *"Sequence-Based Deep Learning for Handover Optimization"*

## 📁 Repository Structure

```
├── model/
│   └── handover_model_final.keras   # Trained GRU model
├── artifacts/
│   ├── scaler.joblib                # Feature scaler
│   └── config.joblib                # Thresholds & config
├── predict.py                       # Inference script
├── requirements.txt                 # Dependencies
└── Handover_Optimization.ipynb      # Training notebook
```

##  Quick Start

### 1. Install Dependencies

```bash
# Create virtual environment (recommended)
python -m venv venv

# Activate (Windows)
venv\Scripts\activate

# Activate (Linux/Mac)
source venv/bin/activate

# Install requirements
pip install -r requirements.txt
```

### 2. Run Predictions

```bash
python predict.py --input your_data.csv --output predictions.csv
```

### 3. Output

The script outputs a CSV with these columns:

| Column | Description |
|--------|-------------|
| `index` | Row index in original data |
| `predicted_tos` | Predicted Time of Stay (seconds) |
| `predicted_class` | 0=No HO, 1=Normal, 2=Ping-Pong |
| `class_name` | Human-readable class name |
| `ping_pong_detected` | 1 if ping-pong detected |
| `unnecessary_detected` | 1 if unnecessary handover |
| `decision` | **AVOID** or **EXECUTE** |

##  Required Input Columns

Your input CSV must contain these columns:

```
SessionID, ElapsedTime, Node, CellID, Level, Qual, SNR, CQI,
SecondCell_RSRP, SecondCell_SNR, NRxLev1, NQual1, Speed,
DL_bitrate, UL_bitrate, BANDWIDTH, Latitude, Longitude
```

##  Model Details

- **Architecture**: GRU(128) → GRU(64) → Dense(128) → Dual Output
- **Window Size**: 10 timesteps
- **Features**: 12 signal/network metrics
- **Outputs**: ToS prediction + Handover classification

##  Expected Performance

| Metric | Value |
|--------|-------|
| Detection F1 | ~45-50 |
| PP Reduction | ~96-98% |
| HO Reduction | ~41-46% |

##  Example

```bash
# Run on test data
python predict.py -i processed_dataset.csv -o results.csv

# Output:
# ============================================================
#   RESULTS SUMMARY
# ============================================================
#   Total predictions:     4629
#   ─────────────────────────────────────
#   🛑 AVOID handover:     1921 ( 41.5%)
#   ✅ EXECUTE handover:   2708 ( 58.5%)
# ============================================================
```

##  License

MIT License
