# Graduation Project — 5G O-RAN Intelligent Network Management Platform

**Author:** Omar Farouk  
**Institution:** Graduation Project — Orange Research Initiative  
**Platform:** FlexRIC · ns-3 mmWave O-RAN · GRU xApp · 3D Command Center GUI  

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Component Breakdown](#3-component-breakdown)
   - [FlexRIC — nearRT-RIC](#31-flexric--nearrt-ric)
   - [ns-3 mmWave O-RAN Simulation](#32-ns-3-mmwave-o-ran-simulation)
   - [GRU Handover xApp](#33-gru-handover-xapp)
   - [GRU Python Prediction Service](#34-gru-python-prediction-service)
   - [Load Balancing xApp](#35-load-balancing-xapp)
   - [Data Pusher](#36-data-pusher)
   - [2D GUI — Grafana Dashboard](#37-2d-gui--grafana-dashboard)
   - [3D Command Center GUI](#38-3d-command-center-gui)
   - [System Controller (FastAPI)](#39-system-controller-fastapi)
4. [GRU Model — Handover Optimization](#4-gru-model--handover-optimization)
5. [Ping-Pong Avoidance Logic](#5-ping-pong-avoidance-logic)
6. [3D GUI — Deep Dive](#6-3d-gui--deep-dive)
7. [Simulation Results](#7-simulation-results)
8. [Decision Log & SQLite Database](#8-decision-log--sqlite-database)
9. [Repository Structure](#9-repository-structure)
10. [How to Run](#10-how-to-run)
11. [Manual Step-by-Step](#11-manual-step-by-step)
12. [Troubleshooting](#12-troubleshooting)
13. [Key Parameters Reference](#13-key-parameters-reference)
14. [Technical Notes & Lessons Learned](#14-technical-notes--lessons-learned)

---

## 1. Project Overview

This project is a full-stack **5G O-RAN intelligent network management platform** built entirely in software. It simulates a real mmWave 5G network with 8 cells and 20 User Equipments (UEs), runs a GRU-based AI xApp that makes real-time handover decisions to prevent ping-pong behavior, and visualizes everything through both a **2D Grafana dashboard** and a custom-built **3D interactive Command Center GUI**.

The platform was developed as part of a graduation project under an **Orange** research initiative. It covers the full O-RAN stack: from the simulated RAN (ns-3), through the E2 interface (FlexRIC), to the xApp layer where AI-driven decisions are made and sent back as RC control commands.

**Two AI scenarios are implemented:**

1. **GRU Handover Optimization** — A GRU (Gated Recurrent Unit) neural network predicts whether a handover should be executed or avoided based on Time-of-Stay (ToS) estimation and ping-pong detection. The model was trained on sequential SINR/KPM features.

2. **Load Balancing (AWF)** — An Adaptive Weight Function algorithm distributes UE load evenly across cells by scoring candidate target cells using RSRP, load, and historical movement patterns.

Both scenarios run on the same simulation platform, the same FlexRIC instance, and are controllable from the same 3D GUI.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          FULL O-RAN PLATFORM                                │
│                                                                             │
│  ┌──────────────────┐   E2AP (SCTP:36421)   ┌─────────────────────────┐    │
│  │  ns-3 Simulation │◄─────────────────────►│  FlexRIC nearRT-RIC     │    │
│  │                  │                        │  (E2AP + E42 API)       │    │
│  │  - 8 mmWave cells│                        │  - KPM v3.00            │    │
│  │  - 1 LTE macro   │                        │  - RC v1.03             │    │
│  │  - 20 UEs        │                        │  - xApp lifecycle       │    │
│  │  - Mobile UEs    │                        └────────────┬────────────┘    │
│  │  - KPM reports   │                                     │ E42 API         │
│  │    every 0.05s   │                         ┌───────────┴──────────┐      │
│  └──────────────────┘                         │                      │      │
│          │                            ┌────────▼──────┐   ┌──────────▼────┐ │
│          │ CSV logs                   │ xapp_handover │   │  xapp_lb_awf  │ │
│          ▼                            │ _gru (C xApp) │   │  (C xApp)     │ │
│  ┌──────────────────┐                 │               │   │               │ │
│  │  sim_data_pusher │                 │ A3 event eval │   │ AWF scoring   │ │
│  │  (Python)        │                 │ GRU query     │   │ ΔHOM adapt    │ │
│  │  CSV → InfluxDB  │                 │ Cooldown mgmt │   │ TTT + guard   │ │
│  └──────────────────┘                 └───────┬───────┘   └──────┬────────┘ │
│          │                                    │                   │          │
│          ▼                                    └────────┬──────────┘          │
│  ┌──────────────────┐    REST :5000           │ RC CONTROL → ns-3 HO         │
│  │  InfluxDB        │◄──────────────── gru_xapp.py (GRU Python service)      │
│  │  (time-series DB)│                 │ - GRU inference                      │
│  └──────────────────┘                 │ - ToS estimation                     │
│          │                            │ - Ping-pong detection                │
│          ▼                            └──────────────────────────────────────│
│  ┌──────────────────┐                                                        │
│  │  2D Grafana GUI  │        ┌─────────────────────────────────────────────┐ │
│  │  (port 8000)     │        │  3D Command Center GUI (port 3001)          │ │
│  │  - Live charts   │        │  - Three.js 3D network visualization        │ │
│  │  - KPM metrics   │        │  - Real-time cell towers + UE positions     │ │
│  └──────────────────┘        │  - SINR strip, load bars, handover log      │ │
│                              │  - FastAPI Controller (port 8001)           │ │
│                              │  - Launch/stop all components from browser  │ │
│                              └─────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Component Breakdown

### 3.1 FlexRIC — nearRT-RIC

**Binary:** `yousef_fathy/flexric/build/examples/ric/nearRT-RIC`  
**Config:** `yousef_fathy/flexric/flexric.conf`  
**Log:** `/tmp/flexric.log`  

FlexRIC is the near-Real-Time RAN Intelligent Controller. It acts as the bridge between the simulated RAN (ns-3) and the xApps. It listens on SCTP port **36421** (not TCP — this is a common confusion).

Key responsibilities:
- Receives **E2 SETUP-REQUEST** from each ns-3 cell on startup
- Manages **KPM subscriptions** — each cell sends KPM indication reports every `indicationPeriodicity` seconds
- Routes **RC control commands** from xApps back to ns-3 cells to trigger handovers
- Provides the **E42 API** so xApps can subscribe and send commands

**Critical detail:** FlexRIC never logs to stdout. All output goes to `/tmp/flexric.log`. Always monitor this file, never `/tmp/farouk_flexric.log` which is a different (empty) file.

To verify E2 connections are up:
```bash
grep -c "E2 SETUP-REQUEST" /tmp/flexric.log
# Should equal the number of cells (7 for gru_scenario, 8 for lb_scenario)
```

---

### 3.2 ns-3 mmWave O-RAN Simulation

**Directory:** `yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/`  
**Binary:** `./ns3`  
**Scenarios:**
- `scratch/gru_scenario.cc` — GRU handover optimization
- `scratch/load_balancing_scenario.cc` — AWF load balancing

The simulation models a mmWave 5G heterogeneous network:
- **7–8 mmWave small cells** (gNBs) arranged in a hexagonal layout
- **1 LTE macro cell** as fallback
- **20 mobile UEs** moving according to a random waypoint model
- UEs connect to the best cell based on SINR measurements

**Key simulation parameters:**

| Parameter | GRU Scenario | LB Scenario | Description |
|---|---|---|---|
| `simTime` | 60 | 60 | Simulation duration in seconds |
| `N_MmWaveEnbNodes` | 7 | 7 | Number of mmWave cells |
| `N_Ues` | 20 | 20 | Number of user equipments |
| `indicationPeriodicity` | **0.05** | **1.5** | KPM report interval (seconds) |
| `hoSinrDifference` | 3 | 3 | A3 event SINR threshold (dB) |
| `e2TermIp` | 127.0.0.1 | 127.0.0.1 | FlexRIC IP |

**Why `indicationPeriodicity` differs:** The GRU model was trained on data collected at 0.05s intervals. Using a different interval changes the temporal patterns the model sees, causing degraded predictions. The LB xApp uses 1.5s because it only needs aggregate statistics per cell, not fine-grained per-UE time series.

Each cell sends a **KPM indication** to FlexRIC every `indicationPeriodicity` seconds containing SINR, throughput, latency, PRB utilization, and per-UE connection status. These reports trigger xApp decision cycles.

---

### 3.3 GRU Handover xApp

**Binary:** `yousef_fathy/flexric/build/examples/xApp/c/handover_gru/xapp_handover_gru`  
**Log:** `/tmp/farouk_xapp.log`  
**Source:** `yousef_fathy/flexric/examples/xApp/c/handover_gru/`

This is the C xApp that runs inside the FlexRIC environment. It subscribes to KPM indication reports and, on each report, evaluates every UE connected to the reporting cell.

**Decision pipeline per UE:**

```
KPM Report Received
       │
       ▼
Is UE in COOLDOWN state?
   YES → skip (prevents too-frequent handovers for same UE)
   NO  ↓
       ▼
A3 Event Evaluation:
  For each neighbor cell:
    SINR_neighbor > SINR_serving + hoSinrDifference (3 dB)?
  NO → no suitable target → skip
  YES ↓
       ▼
Best target cell identified (highest SINR gain)
       ▼
Query GRU Python service:
  POST http://localhost:5000/predict
  { features: [SINR, CQI, speed, bitrate, ...] (12 features × 10 timesteps) }
       ▼
GRU Response:
  { decision: "EXECUTE" | "AVOID", confidence: 0.XX, ToS: X.XXs }
       ▼
EXECUTE → Send RC CONTROL to FlexRIC → ns-3 triggers handover
           Write SUCCESS row to /home/omar_farouk/handover.csv
AVOID   → Block handover, log suppression reason
           Write START row to /home/omar_farouk/handover.csv (executed_ok=0)
```

**State machine per UE:**
- `IDLE` — ready to evaluate
- `WAITING_COMPLETION` — handover command sent, waiting for UE to appear in target cell
- `COOLDOWN` — recently handed over, waiting cooldown period before next evaluation

**handover.csv format:**
```
time_sec,ue_id,from_cell,to_cell,event,executed_ok
1.98,10,4,3,START,0      ← GRU said AVOID
1.99,10,4,3,SUCCESS,1    ← GRU said EXECUTE, handover completed
```

---

### 3.4 GRU Python Prediction Service

**File:** `yousef_fathy/HANDOVER_xApp_Test/gru_xapp.py`  
**Port:** 5000  
**Log:** `/tmp/farouk_gru.log`

This is a Flask REST service that wraps the trained GRU model. The C xApp sends feature vectors to it and receives EXECUTE/AVOID decisions.

**Endpoints:**
- `GET /health` — liveness check, returns `{"status": "ok"}`
- `POST /predict` — takes 12 features × 10 timesteps, returns decision + confidence + ToS

**Features consumed:**
```python
EXPECTED_FEATURES = [
    "Level",           # RSRP level
    "Qual",            # RSRQ quality
    "SNR",             # Signal-to-noise ratio
    "CQI",             # Channel quality indicator
    "SecondCell_RSRP", # Best neighbor RSRP
    "SecondCell_SNR",  # Best neighbor SNR
    "NRxLev1",         # Neighbor cell level
    "NQual1",          # Neighbor cell quality
    "Speed",           # UE speed
    "DL_bitrate",      # Downlink bitrate
    "UL_bitrate",      # Uplink bitrate
    "BANDWIDTH"        # Allocated bandwidth
]
```

**Window size:** 10 timesteps — the model looks at the last 10 KPM reports to capture temporal dynamics before deciding.

The service also:
- Pushes GRU decision metrics to **InfluxDB** so they appear on the Grafana dashboard alongside the ns-3 KPM data
- Monitors ns-3 CSV log files and runs autonomous inference for GUI visualization even when the C xApp is not active

---

### 3.5 Load Balancing xApp

**Binary:** `yousef_fathy/flexric/build/examples/xApp/c/lb_awf/xapp_lb_awf`  
**Log:** `/tmp/farouk_xapp.log`  
**Source:** `load balancing/xapp_lb.c`  
**Reference paper:** Gures et al., "Load balancing in 5G HetNets based on AWF," *ICT Express*, 2023  

The AWF (Adaptive Weight Function) load balancing xApp redistributes UEs from overloaded cells to underloaded cells. It does NOT use a GRU model — it uses a mathematical scoring function.

**Algorithm:**
1. For each overloaded cell (PRB utilization > threshold), identify candidate UEs to offload
2. For each candidate UE, score each neighbor cell using the AWF formula:
   ```
   score = w1 * RSRPpilot + w2 * (1 - load) + w3 * history_factor
   ```
3. Trigger handover to the highest-scoring cell
4. Apply guard timer and TTT (Time-To-Trigger) to prevent oscillation

**Key differences from GRU scenario:**
- No Python prediction service needed
- Uses `indicationPeriodicity=1.5` (aggregate stats, not fine-grained time series)
- Decisions based on cell load, not SINR gain
- 8 cells instead of 7

---

### 3.6 Data Pusher

**File:** `yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/sim_data_pusher.py`  
**Log:** `/tmp/farouk_pusher.log`

A Python script that watches ns-3's CSV output files and streams the data to InfluxDB in real time. This feeds the **2D Grafana dashboard** with live KPM metrics (SINR per UE, cell throughput, latency, PRB usage).

---

### 3.7 2D GUI — Grafana Dashboard

**Port:** 8000  
**Managed by:** Docker Compose  
**Docker dir:** `yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/GUI/`

The 2D GUI is the original monitoring dashboard. It runs two Docker containers:
- **InfluxDB** — time-series database storing all KPM metrics
- **FastAPI + Grafana backend** — serves the dashboard on port 8000

This GUI was the starting point of the project. The 3D GUI was built alongside it as an enhancement, keeping both available simultaneously.

To start only the 2D GUI:
```bash
cd yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/GUI
docker compose up -d influxdb gui
```

---

### 3.8 3D Command Center GUI

**Directory:** `5g-gui-v2/`  
**Port:** 3001  
**Stack:** Vite + Three.js + vanilla JavaScript  

This is the major GUI contribution of this project — a full 3D interactive visualization of the live O-RAN network, built from scratch using Three.js and WebGL.

**Features:**
- **3D cell tower rendering** — each mmWave cell rendered as a tower with a glowing coverage sphere whose color shifts green→yellow→red based on load
- **Live UE positions** — each of the 20 UEs rendered as a moving node, repositioned on every API poll
- **SINR strip** — bottom-left bar chart showing per-UE SINR in dB, color-coded
- **Handover log** — scrolling live feed of every handover event with UE ID, source→target cell, and timestamp
- **Cell inspect tooltip** — click any cell tower to see its current SINR, load, UE count, throughput, latency
- **KPI top bar** — live counts of cells, UEs, aggregate throughput, latency, load
- **Control panel** — start/stop each system component individually (Docker, FlexRIC, Simulation, Pusher, xApp) with real-time status dots
- **Scenario selector** — choose between gru_scenario and load_balancing_scenario
- **Parameter inputs** — set UE count, cell count, and simulation time directly from the browser

**Architecture:** The 3D GUI communicates with the **System Controller** (port 8001) via REST API. The browser polls `/ctrl/status`, `/ctrl/network-state`, and `/ctrl/handover-log` every second to update the visualization.

---

### 3.9 System Controller (FastAPI)

**File:** `5g-gui-v2/controller.py`  
**Port:** 8001  
**Log:** `/tmp/controller.log`

The controller is the brain of the 3D GUI. It is a FastAPI application that:
- Starts and stops every simulation component as subprocesses
- Monitors process health and reports status to the 3D frontend
- Orchestrates the full launch sequence with proper timing
- Waits for FlexRIC E2 connections before starting the xApp
- Detects simulation completion and auto-saves results
- Generates decision logs, plots, and SQLite entries

**Key API endpoints:**

| Endpoint | Method | Description |
|---|---|---|
| `/ctrl/status` | GET | Returns `{docker, flexric, simulation, pusher, xapp}` all true/false |
| `/ctrl/launch-all` | POST | Full orchestrated launch with timing |
| `/ctrl/start/{component}` | POST | Start a single component |
| `/ctrl/stop/{component}` | POST | Stop a single component |
| `/ctrl/network-state` | GET | Live cell/UE positions and metrics for 3D rendering |
| `/ctrl/handover-log` | GET | Latest handover events for the log panel |
| `/ctrl/last-result` | GET | Summary of the most recent completed simulation |
| `/ctrl/decisions` | GET | Query decision history from SQLite |

**Launch sequence (automatic when pressing LAUNCH ALL):**
```
1. Kill all stale processes
2. Clear /tmp/flexric.log
3. Start Docker (InfluxDB + 2D backend)
4. Start FlexRIC
5. Wait for SCTP port 36421 to open (up to 60s)
6. Start GRU Python service (port 5000) — GRU scenario only
7. Start NS-3 simulation with correct parameters
8. Wait for N E2 SETUP-REQUESTs in /tmp/flexric.log
9. Start Data Pusher
10. Start xApp (GRU or LB based on scenario)
11. Monitor until simulation finishes
12. Auto-save results to numbered sim### folder
```

---

## 4. GRU Model — Handover Optimization

**Model file:** `Fares/model/handover_model_final.keras`  
**Scaler:** `Fares/artifacts/scaler.joblib`  
**Config:** `Fares/artifacts/config.joblib`  
**Training notebook:** `Fares/Handover_Optimization.ipynb`

The GRU (Gated Recurrent Unit) model is a deep learning sequence model trained to predict whether a handover will result in a ping-pong (UE returning to source cell within 5 seconds). It is a **binary classifier**:
- Output 0 → EXECUTE (handover is safe and beneficial)
- Output 1 → AVOID (handover predicted to cause ping-pong or be unnecessary)

**Input:** Sequence of 10 × 12 feature vectors (10 timesteps, 12 KPM features per timestep)  
**Output:** Probability of ping-pong, confidence score, estimated Time-of-Stay (ToS)

**Decision thresholds (from config.joblib):**
- `ToS_th = 1.2s` — minimum time UE is expected to stay in target cell
- `ToS_unnec_th = 1.35s` — threshold below which handover is considered unnecessary
- AVOID if: `(is_short AND is_oscillating) OR is_model_predicted_pp OR is_unnecessary`

**Why `indicationPeriodicity=0.05` is critical:**  
The model was trained on data collected at 0.05s intervals. At higher intervals (e.g., 0.1s), the temporal patterns in the input sequence look different — the model sees slower oscillations and generates near-constant AVOID decisions (99.8% AVOID rate observed at 0.1s). At 0.05s, the model performs as trained, yielding ~1.7% ping-pong rate matching the 2D reference conditions.

---

## 5. Ping-Pong Avoidance Logic

A **ping-pong handover** occurs when a UE is handed from cell A→B, and then shortly after (<5 seconds) is handed back from B→A. This wastes resources and degrades user experience.

**Detection algorithm (per-UE grouping):**
```python
from collections import defaultdict

ue_history = defaultdict(list)
for r in executed_handovers:
    ue_history[r['ue_id']].append((time, from_cell, to_cell))

ping_pong_count = 0
for ue, handovers in ue_history.items():
    for i in range(1, len(handovers)):
        prev_time, prev_from, prev_to = handovers[i-1]
        curr_time, curr_from, curr_to = handovers[i]
        # Ping-pong: same UE, reversed direction, within 5 seconds
        if (curr_to == prev_from and curr_from == prev_to
                and (curr_time - prev_time) <= 5.0):
            ping_pong_count += 1
```

**Important:** The detection must group handovers **per UE first**, then check consecutive entries within each UE. A global sequential scan misses ping-pongs when handovers from different UEs interleave in the CSV file — this was a bug found and fixed during this project.

---

## 6. 3D GUI — Deep Dive

**Entry point:** `5g-gui-v2/index.html`  
**Main logic:** `5g-gui-v2/src/main.js`  
**Styles:** inline CSS in `index.html`  
**Build tool:** Vite  

The 3D GUI is built as a single-page application using Three.js for 3D rendering and native JavaScript for UI and API communication.

**3D scene elements:**
- **Ground plane** — dark hexagonal grid representing the network coverage area
- **Cell towers** — 7–8 glowing vertical towers, each with:
  - A pulsing sphere whose radius represents signal coverage
  - Color coding: `#00ff88` (low load) → `#ffaa00` (medium) → `#ff3333` (high)
  - Cell ID label
- **UE nodes** — 20 moving dots with trails, color-coded by SINR
- **Handover beams** — brief animated lines connecting source and target cells when a handover occurs
- **Scan line effect** — retro CRT-style overlay for visual aesthetics

**API polling (every 1 second):**
```javascript
// Fetches from http://localhost:8001/ctrl/network-state
{
  cells: [{ id, load, ues_connected, throughput, latency, sinr, position }],
  ues:   [{ id, cell_id, sinr, position }],
  handovers: [{ time, ue_id, from_cell, to_cell }]
}
```

**Control panel actions:**
- Each system card (Docker, FlexRIC, Simulation, Pusher, xApp) has individual START/STOP buttons
- Status dots update in real time: grey (stopped) → orange (starting) → green (running)
- LAUNCH ALL button triggers the full orchestrated sequence via `/ctrl/launch-all`

**Bottom bar:**
- **UE · SINR · dB strip** — real-time per-UE SINR bars (green = strong signal, red = weak)
- **Energy track** — cumulative energy consumption indicator
- **Scenario label** — shows current scenario name, cell count, UE count

---

## 7. Simulation Results

All results are auto-saved to `3D_GUI_Sim_Results/` in numbered folders.

### GRU Scenario Results

| Sim | Date | Handovers | Ping-pong | Rate | indicationPeriodicity | Notes |
|---|---|---|---|---|---|---|
| sim001 | 2026-05-02 | — | — | — | 1.5 | Early test run |
| sim002 | 2026-05-02 | — | — | — | 1.5 | Parameter tuning |
| sim003 | 2026-05-03 | — | — | — | 0.1 | Too conservative (99.8% AVOID) |
| sim004 | 2026-05-03 | 88 | 0 | 0.0% | 0.1 | GRU barely triggering |
| sim005 | 2026-05-03 | ~26 | — | — | 0.1 | Near-zero execution |
| **sim006** | **2026-05-03** | **174** | **3** | **1.72%** | **0.05** | **Clean reference run** |

**2D GUI reference result (baseline):** 334 handovers, 2.1% ping-pong @ indicationPeriodicity=0.05

**sim006 is the first valid 3D run** matching the 2D reference conditions. The key fix was changing `indicationPeriodicity` from 0.1 to 0.05 to match training conditions.

### Result Folder Contents

Each `sim###_TIMESTAMP_SCENARIO/` folder contains:

```
sim006_20260503_121100_gru_scenario/
├── handover.csv          — raw handover events (time_sec, ue_id, from_cell, to_cell, event, executed_ok)
├── decision_log.csv      — UUID per handover + is_correct flag
├── decision_summary.json — accuracy %, ping-pong rate, counts
├── summary.txt           — human-readable summary
├── lstm_features.csv     — GRU feature vectors used per decision
├── plots/
│   ├── decision_quality.png      — scatter plot: green=correct, red X=ping-pong
│   └── handovers_over_time.png   — cumulative handover curve with ping-pong markers
├── flexric.log           — FlexRIC E2 activity during this sim
├── simulation.log        — ns-3 MAC-layer output (large)
├── xapp.log              — per-UE decision cycle output (large)
├── gru.log               — GRU Python service inference log
└── pusher.log            — InfluxDB data push log
```

---

## 8. Decision Log & SQLite Database

Every simulation appends its handover decisions to a persistent SQLite database:

**DB path:** `sim_decisions.db`  
**Table:** `decisions`

| Column | Type | Description |
|---|---|---|
| `uuid` | TEXT | Unique ID per handover |
| `sim` | TEXT | Simulation label (e.g., sim006) |
| `time_sec` | REAL | Simulation time of the handover |
| `ue_id` | TEXT | UE identifier |
| `from_cell` | TEXT | Source cell |
| `to_cell` | TEXT | Target cell |
| `is_correct` | INTEGER | 1 if not followed by ping-pong within 5s |

**Query via API (while controller is running):**
```bash
curl http://localhost:8001/ctrl/decisions               # all decisions
curl "http://localhost:8001/ctrl/decisions?sim=sim006"  # filter by sim
curl http://localhost:8001/ctrl/last-result             # latest run summary
```

---

## 9. Repository Structure

```
open-ran-clean/
├── gru.sh                          ← One-command launcher (use this after every reboot)
├── MANUAL_COMMANDS.txt             ← Full manual reference for every command
├── sim_decisions.db                ← SQLite: all handover decisions across all sims
├── README.md                       ← This file
│
├── 5g-gui-v2/                      ← 3D Command Center GUI
│   ├── index.html                  ← Single-page app entry point
│   ├── src/
│   │   └── main.js                 ← Three.js rendering + API polling + UI logic
│   ├── controller.py               ← FastAPI controller (port 8001)
│   ├── package.json                ← Vite + dependencies
│   └── vite.config.js
│
├── 3D_GUI_Sim_Results/             ← Auto-saved simulation results
│   ├── sim001_*/
│   ├── sim002_*/
│   ├── sim003_*/
│   ├── sim004_*/
│   ├── sim005_*/
│   └── sim006_*/                   ← Latest clean run (indicationPeriodicity=0.05)
│
├── Fares/                          ← GRU model assets
│   ├── model/
│   │   └── handover_model_final.keras
│   ├── artifacts/
│   │   ├── scaler.joblib
│   │   └── config.joblib
│   ├── Handover_Optimization.ipynb ← Training notebook
│   ├── predict.py
│   └── requirements.txt
│
├── load balancing/                 ← LB xApp source + docs + results
│   ├── load_balancing_scenario.cc
│   ├── xapp_lb.c
│   ├── SIMULATION_DOCUMENTATION.md ← Detailed LB sim history (sim001-sim008)
│   └── sim_results/
│
└── yousef_fathy/                   ← Core simulation stack
    ├── flexric/                    ← FlexRIC nearRT-RIC
    │   ├── flexric.conf
    │   └── examples/xApp/c/
    │       ├── handover_gru/       ← GRU C xApp source
    │       └── lb_awf/             ← LB C xApp source
    ├── ns-O-RAN-flexric/
    │   └── mmwave-LENA-oran/
    │       ├── scratch/
    │       │   ├── gru_scenario.cc
    │       │   └── load_balancing_scenario.cc
    │       ├── sim_data_pusher.py
    │       └── GUI/                ← 2D Grafana GUI (Docker)
    │           └── docker-compose.yml
    └── HANDOVER_xApp_Test/
        └── gru_xapp.py             ← GRU Python prediction service (port 5000)
```

---

## 10. How to Run

### Prerequisites

- Ubuntu 20.04 / 22.04
- Python 3.10+
- Node.js 18+
- Docker + Docker Compose
- FlexRIC and ns-3 compiled (see `yousef_fathy/flexric/` and `yousef_fathy/ns-O-RAN-flexric/`)

### One-Command Launch (Recommended)

```bash
bash /home/omar_farouk/open-ran-clean/gru.sh
```

This single command:
1. Kills all stale processes from any previous run
2. Clears `/tmp/flexric.log`
3. Starts Docker (InfluxDB + 2D backend on port 8000)
4. Starts the 3D controller (port 8001)
5. Starts the 3D frontend (port 3001)
6. Triggers LAUNCH ALL via the controller API

With custom parameters:
```bash
bash gru.sh [simTime] [nUEs] [nCells]
bash gru.sh 60           # 60s sim, default UEs=20, cells=7
bash gru.sh 100 30 8     # 100s sim, 30 UEs, 8 cells
```

Then open your browser:
- **3D GUI** → http://localhost:3001
- **2D GUI** → http://localhost:8000

### Using the 3D GUI Instead

If you want to set parameters from the browser instead of the command line, run the infrastructure manually (without auto-launch) and then press LAUNCH ALL in the browser:

```bash
# Kill stale processes
pkill -9 -f 'nearRT-RIC'; pkill -9 -f 'ns3\.42'; pkill -9 -f 'xapp_handover_gru'
pkill -9 -f 'sim_data_pusher'; pkill -9 -f 'gru_xapp\.py'
> /tmp/flexric.log

# Start Docker
cd yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/GUI
docker compose up -d influxdb gui

# Start controller
cd 5g-gui-v2
python3 -m uvicorn controller:app --host 0.0.0.0 --port 8001 --log-level warning &

# Start frontend
npm run dev &

# Open browser, set parameters, press LAUNCH ALL
# http://localhost:3001
```

---

## 11. Manual Step-by-Step

For the full manual terminal commands (every step explained), see:

**`MANUAL_COMMANDS.txt`** — covers:
- Kill all processes
- Start each component individually
- GRU scenario (Option C)
- Load Balancing scenario (Option D)
- Status checks
- Ping-pong recalculation
- Troubleshooting

---

## 12. Troubleshooting

### xApp prints "Insufficient samples 1/2" — 0 handovers
**Cause:** xApp started too late (>60s after simulation began), not enough KPM samples accumulated.  
**Fix:** Always do Step 0 (kill all), clear `/tmp/flexric.log`, and wait for N E2 SETUP-REQUESTs before starting the xApp.

### FlexRIC crashes — "assertion sr->len_e2_nodes_conn > 0 failed"
**Cause:** A stale xApp from a previous crashed run reconnected to FlexRIC before ns-3 did, confusing the E2 state.  
**Fix:** `pkill -9` everything before every new run. Never skip the kill step.

### GRU xApp makes 0 handovers even though it started on time
**Cause:** `gru_xapp.py` is not running on port 5000, or `LSTM_SERVICE_URL` environment variable is not set.  
**Fix:** Start `gru_xapp.py` first, verify with `curl http://localhost:5000/health`, then start the C xApp.

### FlexRIC shows 0 E2 SETUP-REQUESTs forever
**Cause:** Reading the wrong log file. FlexRIC ONLY writes to `/tmp/flexric.log`.  
**Fix:** `grep -c "E2 SETUP-REQUEST" /tmp/flexric.log` — not `farouk_flexric.log`.

### ns-3 or FlexRIC won't start — "address already in use"
**Cause:** Previous run left zombie processes on SCTP port 36421.  
**Fix:** Run all `pkill` commands from the kill step.

### Ping-pong rate is 0.0% but it should not be
**Cause:** Sequential scan bug — comparing consecutive rows globally instead of per-UE.  
**Fix:** Always group handovers by `ue_id` before checking A→B→A reversals within 5 seconds. See `controller.py:_calc_pingpong`.

---

## 13. Key Parameters Reference

| Parameter | Value | Where set | Why this value |
|---|---|---|---|
| `indicationPeriodicity` (GRU) | **0.05** | `controller.py` | Matches GRU model training conditions |
| `indicationPeriodicity` (LB) | **1.5** | `controller.py` | LB only needs aggregate stats |
| `simTime` | 60 | `gru.sh`, `controller.py`, `index.html` | ~3 hours wall-clock per 60 sim-seconds |
| `N_Ues` | 20 | all configs | Reference scenario spec |
| `N_MmWaveEnbNodes` | 7 (GRU) / 8 (LB) | all configs | Reference scenario spec |
| `hoSinrDifference` | 3 dB | `controller.py` | A3 event trigger threshold |
| `GRU_PORT` | 5000 | `controller.py` | Flask prediction service port |
| Controller port | 8001 | `gru.sh`, `controller.py` | 3D GUI backend |
| Frontend port | 3001 | `vite.config.js` | 3D GUI browser |
| 2D GUI port | 8000 | Docker Compose | Grafana dashboard |
| Ping-pong window | 5.0s | `controller.py` | Standard 3GPP ping-pong definition |
| GRU cooldown | varies | `config.joblib` | Per-UE post-handover lockout |
| E2 SETUP-REQUEST target | 7 (GRU) / 8 (LB) | `controller.py` | One per mmWave cell |
| FlexRIC SCTP port | 36421 | `flexric.conf` | Standard E2 interface port |

---

## 14. Technical Notes & Lessons Learned

### SCTP vs TCP
FlexRIC uses **SCTP** (not TCP) for the E2 interface on port 36421. Standard port checkers like `ss -tlnp` won't show it. Use `ss -anp | grep 36421` or `ss -Scnp` to verify the port is open.

### Why the GRU only saw AVOID at indicationPeriodicity=0.1
At 0.1s intervals, the GRU sees the same underlying signal dynamics as at 0.05s, but with half the temporal resolution. The 10-timestep window now covers 1.0s of history instead of 0.5s. The model was trained to see fast oscillation patterns at 0.05s resolution — at 0.1s those patterns look smoother and the model interprets them as oscillation risk, triggering 99.8% AVOID. This was the root cause of the near-zero handover rates in sim003–sim005.

### Two indicationPeriodicity locations in controller.py
The parameter appears in TWO separate places — one in `start_simulation()` (manual start) and one in `_launch_all_task()` (automatic launch). Both must always be set to the same value. This caused inconsistent behavior early in development.

### Ping-pong calculation bug
The initial `_calc_pingpong` implementation compared consecutive rows in the global CSV. When UE 5 did A→B at t=10s, UE 8 did something at t=11s, then UE 5 did B→A at t=12s — the two UE 5 rows were not adjacent, so the ping-pong was missed. The fix groups by `ue_id` first. This caused sim006 to incorrectly report 0.0% ping-pong before the fix.

### Wall clock vs simulation time
60 simulation-seconds takes approximately **3 hours** of wall-clock time on this hardware. This is because each 0.05s KPM reporting cycle involves ns-3 computing channel conditions, SINR, and MAC allocations for 20 UEs across 8 cells simultaneously.

### FlexRIC log must be cleared between runs
If `/tmp/flexric.log` is not cleared before a new run, the E2 SETUP-REQUEST count includes entries from the previous run, causing the controller to think all E2 connections are already up and starting the xApp too early.

---

*Built with ❤️ — Omar Farouk*
