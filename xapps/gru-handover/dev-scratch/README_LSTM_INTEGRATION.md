# LSTM Model Integration for O-RAN Handover xApp

This document describes how to integrate the LSTM model with the rule-based handover xApp.

## Architecture

```
┌─────────────────┐         HTTP/REST         ┌──────────────────┐
│   C xApp        │───────────────────────────>│  Python Service  │
│  (best2.c)      │                             │  (Flask API)     │
│                 │<───────────────────────────│                  │
│  - RC Control   │      JSON Response         │  - LSTM Model    │
│  - APIs         │                             │  - Predictions   │
└─────────────────┘                             └──────────────────┘
```

## Components

1. **Python LSTM Service** (`lstm_predictor_service.py`)
   - Flask REST API service
   - Loads LSTM model and scaler
   - Provides `/predict` endpoint

2. **C Client Library** (`lstm_predictor_client.c/h`)
   - HTTP client using libcurl
   - JSON parsing using json-c
   - Simple API for C code

3. **Modified xApp** (`best2.c`)
   - Uses LSTM for handover decisions
   - Falls back to rule-based if LSTM unavailable
   - All APIs and RC control preserved

## Setup Instructions

### 1. Install Python Dependencies

```bash
cd ~/Documents/o-ran/yousef_fathy/HANDOVER_xApp_Test
python3 -m venv lstm_venv
source lstm_venv/bin/activate
pip install -r requirements_lstm.txt
```

### 2. Copy Model Files

Ensure the LSTM model and scaler are accessible:

```bash
# Option 1: Symlink to existing model
ln -s ~/Documents/o-ran/ai/Fares/model ~/Documents/o-ran/yousef_fathy/ai/Fares/model
ln -s ~/Documents/o-ran/ai/Fares/artifacts ~/Documents/o-ran/yousef_fathy/ai/Fares/artifacts

# Option 2: Copy files
mkdir -p ~/Documents/o-ran/yousef_fathy/ai/Fares/{model,artifacts}
cp ~/Documents/o-ran/ai/Fares/model/handover_model_final.keras \
   ~/Documents/o-ran/yousef_fathy/ai/Fares/model/
cp ~/Documents/o-ran/ai/Fares/artifacts/scaler.joblib \
   ~/Documents/o-ran/yousef_fathy/ai/Fares/artifacts/
```

### 3. Install C Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev libjson-c-dev

# Or build from source if needed
```

### 4. Start LSTM Service

```bash
cd ~/Documents/o-ran/yousef_fathy/HANDOVER_xApp_Test
source lstm_venv/bin/activate
python3 lstm_predictor_service.py
```

The service will start on `http://localhost:5000`

### 5. Build xApp with LSTM Support

```bash
cd ~/Documents/o-ran/yousef_fathy/HANDOVER_xApp_Test
make -f Makefile.lstm
```

Or integrate into your existing build system by:
- Adding `lstm_predictor_client.c` to sources
- Adding `-lcurl -ljson-c` to linker flags
- Including `lstm_predictor_client.h`

### 6. Run xApp

```bash
# Terminal 1: Start LSTM service
cd ~/Documents/o-ran/yousef_fathy/HANDOVER_xApp_Test
source lstm_venv/bin/activate
python3 lstm_predictor_service.py

# Terminal 2: Run xApp
./handover_xapp_lstm [your xApp arguments]
```

## How It Works

1. **xApp receives KPM measurements** (SINR, RSRP, etc.)
2. **Decision loop calls `evaluate_a3_event()`**
3. **Function checks if LSTM service is available**
4. **If available:**
   - Prepares measurements for LSTM
   - Calls `lstm_predict_handover()`
   - Uses AI prediction for handover decision
5. **If unavailable:**
   - Falls back to original rule-based A3 evaluation
6. **RC Control and APIs remain unchanged**

## API Endpoints

### POST `/predict`

Request:
```json
{
  "serving_sinr": -98.0,
  "serving_rsrp": -90.0,
  "neighbor_sinr": -85.0,
  "neighbor_rsrp": -80.0,
  "neighbor_cell_id": 2,
  "cqi": 6,
  "speed": 0,
  "dl_bitrate": 0,
  "ul_bitrate": 0,
  "bandwidth": 20
}
```

Response:
```json
{
  "should_handover": true,
  "target_cell": 2,
  "confidence": 0.85,
  "time_to_handover": 2.5,
  "class_probabilities": [0.1, 0.85, 0.05]
}
```

### GET `/health`

Returns service status.

## Configuration

Edit `lstm_predictor_client.h` to change:
- `LSTM_SERVICE_URL` - Service endpoint
- `LSTM_SERVICE_TIMEOUT_SEC` - Request timeout

## Troubleshooting

1. **LSTM service not found**: Check if service is running on port 5000
2. **Model loading errors**: Verify model path in `lstm_predictor_service.py`
3. **Build errors**: Ensure libcurl and json-c are installed
4. **Prediction failures**: Check service logs for errors

## Notes

- The xApp gracefully falls back to rule-based logic if LSTM is unavailable
- All original APIs and RC control mechanisms are preserved
- Only the decision logic (`evaluate_a3_event`) is modified
- The LSTM model expects 12 features - adjust mapping in `prepare_features()` if needed
