#!/usr/bin/env python3
"""
lb_influx_bridge.py
Reads the scenario's CSV files and ns-3 log (written even in E2 mode)
and pushes to InfluxDB so the GUI shows live data while the xApp runs.

Data sources:
  ~/load_balance_kpis.csv       → per-cell PRB, UE count
  ~/kpm_handover_features.csv   → per-UE SINR
  /tmp/ns3_lb.log               → UE positions + serving cell
"""

import csv, os, re, time, math, requests
from collections import defaultdict

INFLUX_URL  = "http://localhost:8086/write?db=influx&precision=ns"
INFLUX_AUTH = ("admin", "admin")
KPI_FILE    = os.path.expanduser("~/load_balance_kpis.csv")
KPM_FILE    = os.path.expanduser("~/kpm_handover_features.csv")
NS3_LOG     = "/tmp/ns3_lb.log"
PUSH_INTERVAL = 3   # seconds between pushes

# ── Cell layout from scenario (isd=300m, center=1000,1000) ──────────────────
ISD = 300.0
CX, CY = 1000.0, 1000.0
N_CONSTELLATION = 6   # nMmWaveEnbNodes-1

CELL_POSITIONS = {
    1: (CX, CY, 'lte'),    # LTE macro at centre
    2: (CX, CY, 'mmwave'), # mmWave co-located at centre
}
for i in range(N_CONSTELLATION):
    angle = (2 * math.pi * i) / N_CONSTELLATION
    cx = CX + ISD * math.cos(angle)
    cy = CY + ISD * math.sin(angle)
    CELL_POSITIONS[3 + i] = (round(cx, 2), round(cy, 2), 'mmwave')

# ── InfluxDB helpers ─────────────────────────────────────────────────────────
def write_points(lines: list[str]):
    if not lines:
        return
    body = "\n".join(lines)
    try:
        r = requests.post(INFLUX_URL, data=body.encode(), auth=INFLUX_AUTH, timeout=5)
        if r.status_code not in (204, 200):
            print(f"[bridge] InfluxDB error {r.status_code}: {r.text[:120]}")
    except Exception as e:
        print(f"[bridge] write error: {e}")

def _esc(name: str) -> str:
    return name.replace(' ', r'\ ').replace(',', r'\,')

def field_float(name: str, val: float, ts: int) -> str:
    return f'{_esc(name)} value={val} {ts}'

def field_str(name: str, val: str, ts: int) -> str:
    return f'{_esc(name)} value="{val}" {ts}'

# ── One-time init: cell positions, chart dims ────────────────────────────────
def push_cell_positions():
    ts = time.time_ns()
    lines = [
        field_float('gnbs_x_0', 2000, ts),
        field_float('gnbs_y_0', 2000, ts),
    ]
    for cid, (x, y, ctype) in CELL_POSITIONS.items():
        if ctype == 'lte':
            lines.append(field_float(f'enbs_x_{cid}', x, ts))
            lines.append(field_float(f'enbs_y_{cid}', y, ts))
        else:
            lines.append(field_float(f'gnbs_x_{cid}', x, ts))
            lines.append(field_float(f'gnbs_y_{cid}', y, ts))
    write_points(lines)
    print(f"[bridge] Pushed {len(lines)} cell position points")

# ── Parse ns-3 log for UE positions ─────────────────────────────────────────
UE_POS_RE = re.compile(
    r'Position of UE with IMSI (\d+) is ([\d.]+):([\d.]+):[\d.]+ at time ([\d.]+), UE connected to Cell: (\d+)'
)

def parse_ns3_positions(log_path: str) -> dict:
    """Return latest position per IMSI from log."""
    latest = {}
    try:
        with open(log_path, 'r', errors='replace') as f:
            for line in f:
                m = UE_POS_RE.search(line)
                if m:
                    imsi = int(m.group(1))
                    latest[imsi] = {
                        'x': float(m.group(2)),
                        'y': float(m.group(3)),
                        't': float(m.group(4)),
                        'cell': int(m.group(5)),
                    }
    except Exception as e:
        print(f"[bridge] log parse error: {e}")
    return latest

# ── Parse load_balance_kpis.csv for cell KPIs ───────────────────────────────
def parse_cell_kpis(kpi_path: str) -> dict:
    """Return latest KPIs per cell_id."""
    latest = {}
    try:
        with open(kpi_path, newline='') as f:
            for row in csv.DictReader(f):
                try:
                    cid = int(row['cell_id'])
                    latest[cid] = {
                        'prb': float(row.get('prb_util_pct', 0) or 0),
                        'ue_count': float(row.get('ue_count', 0) or 0),
                    }
                except (ValueError, KeyError):
                    pass
    except Exception:
        pass
    return latest

# ── Parse kpm_handover_features.csv for per-UE SINR ─────────────────────────
def parse_ue_sinr(kpm_path: str) -> dict:
    """Return latest SINR per UE (Node column = IMSI)."""
    latest = {}
    try:
        with open(kpm_path, newline='') as f:
            for row in csv.DictReader(f):
                try:
                    ue = int(row.get('Node', 0))
                    snr = float(row.get('SNR', 0) or 0)
                    if ue > 0:
                        latest[ue] = snr
                except (ValueError, KeyError):
                    pass
    except Exception:
        pass
    return latest

# ── Main push loop ───────────────────────────────────────────────────────────
def main():
    print("[bridge] Starting lb_influx_bridge — xApp + GUI live mode")
    print(f"[bridge] Cell layout: {len(CELL_POSITIONS)} cells")
    for cid, (x, y, ct) in CELL_POSITIONS.items():
        print(f"  Cell {cid}: ({x:.0f},{y:.0f}) [{ct}]")

    # Wait until ns-3 log exists
    while not os.path.exists(NS3_LOG):
        print("[bridge] Waiting for ns-3 log...")
        time.sleep(2)

    # Push cell positions immediately and register sim with GUI
    push_cell_positions()

    # Register sim start with GUI API
    try:
        r = requests.post("http://localhost:8000/start_simulation",
                          json={"scenario": "scratch/load_balancing_scenario.cc",
                                "flexric": "false", "flags": "false",
                                "N_Ues": "20", "N_MmWaveEnbNodes": "7", "simTime": "60"},
                          timeout=5)
        print(f"[bridge] GUI start_simulation: {r.status_code}")
    except Exception as e:
        print(f"[bridge] GUI registration error: {e}")

    time.sleep(1)
    # Re-push cell positions AFTER GUI anchor is set
    push_cell_positions()

    sim_id = int(time.time() * 1000)
    cycle = 0

    while True:
        ts = time.time_ns()
        lines = []

        # ── UE positions from ns-3 log ───────────────────────────────────
        ue_pos = parse_ns3_positions(NS3_LOG)
        for imsi, p in ue_pos.items():
            lines.append(field_float(f'ue_position_x_{imsi}', p['x'], ts))
            lines.append(field_float(f'ue_position_y_{imsi}', p['y'], ts))
            lines.append(field_float(f'ue_position_cell_{imsi}', p['cell'], ts))
            lines.append(field_str(f'ue_position_type_{imsi}', 'mc', ts))
        if ue_pos:
            lines.append(field_float(f'ue_position_simid_1', sim_id, ts))
            lines.append(field_float(f'ue_position_count', len(ue_pos), ts))

        # ── UE SINR from kpm_handover_features.csv ───────────────────────
        ue_sinr = parse_ue_sinr(KPM_FILE)
        for ue, snr in ue_sinr.items():
            lines.append(field_float(f'ue_{ue}_l3 serving sinr', snr, ts))

        # ── Cell KPIs from load_balance_kpis.csv ─────────────────────────
        cell_kpis = parse_cell_kpis(KPI_FILE)
        for cid, kpi in cell_kpis.items():
            lines.append(field_float(f'du-cell-{cid}_dlprbusage', kpi['prb'], ts))
            lines.append(field_float(f'du-cell-{cid}_drb.meanactiveuedl', kpi['ue_count'], ts))

        # Re-push cell positions every 10 cycles so they stay fresh
        if cycle % 10 == 0:
            push_cell_positions()

        if lines:
            write_points(lines)
            if cycle % 5 == 0:
                n_ues = len(ue_pos)
                n_cells = len(cell_kpis)
                sim_t = max((p['t'] for p in ue_pos.values()), default=0)
                print(f"[bridge] cycle={cycle}  sim={sim_t:.1f}s  UEs={n_ues}  cells={n_cells}  points={len(lines)}")

        cycle += 1
        time.sleep(PUSH_INTERVAL)

if __name__ == '__main__':
    main()
