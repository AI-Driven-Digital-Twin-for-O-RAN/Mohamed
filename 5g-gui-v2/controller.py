"""
Farouk GUI — System Controller  (port 8001)
Host-level FastAPI backend that starts/stops every simulation component.
"""

import asyncio
import csv
import json
import os
import shutil
import signal
import sqlite3
import subprocess
import uuid
from datetime import datetime
from typing import Optional

from fastapi import BackgroundTasks, FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI(title="Farouk GUI Controller", version="1.0")
app.add_middleware(CORSMiddleware, allow_origins=["*"],
                  allow_methods=["*"], allow_headers=["*"])

# ── Paths ──────────────────────────────────────────────────────────────────────
ROOT_DIR    = "/home/omar_farouk/open-ran-clean/yousef_fathy"
FLEXRIC_BIN = f"{ROOT_DIR}/flexric/build/examples/ric/nearRT-RIC"
FLEXRIC_CONF= f"{ROOT_DIR}/flexric/flexric.conf"
NS3_DIR     = f"{ROOT_DIR}/ns-O-RAN-flexric/mmwave-LENA-oran"
GUI_DIR     = f"{NS3_DIR}/GUI"
XAPP_DIR    = f"{GUI_DIR}/FlexRIC xApp GUI trigger"

# xApp binaries — chosen based on scenario / xapp_type
XAPP_RC_BIN  = f"{ROOT_DIR}/flexric/build/examples/xApp/c/ctrl/xapp_rc_handover_ctrl"
XAPP_GRU_BIN = f"{ROOT_DIR}/flexric/build/examples/xApp/c/handover_gru/xapp_handover_gru"
XAPP_RL_BIN  = f"{ROOT_DIR}/flexric/build/examples/xApp/c/handover_rl/xapp_handover_rl"
GRU_SERVICE  = f"{ROOT_DIR}/HANDOVER_xApp_Test/gru_xapp.py"
RL_SERVICE   = "/home/omar_farouk/open-ran-clean/omar_salama/rl_xapp.py"
GRU_PORT     = 5000
RL_PORT      = 5001

LOG = {
    "flexric":    "/tmp/flexric.log",        # FlexRIC always writes here internally
    "simulation": "/tmp/farouk_ns3.log",
    "pusher":     "/tmp/farouk_pusher.log",
    "xapp":       "/tmp/farouk_xapp.log",
    "gru":        "/tmp/farouk_gru.log",
    "rl":         "/tmp/farouk_rl_xapp.log",
}

RESULTS_DIR   = "/home/omar_farouk/open-ran-clean/3D_GUI_Sim_Results"
DECISIONS_DB  = "/home/omar_farouk/open-ran-clean/sim_decisions.db"
HANDOVER_CSV  = "/home/omar_farouk/handover.csv"
LSTM_CSV      = "/home/omar_farouk/lstm_features.csv"
KPM_CSV       = "/home/omar_farouk/kpm_handover_features.csv"

def _calc_pingpong(csv_path: str, window_sec: float = 5.0) -> dict:
    """Count ping-pong handovers: UE handed A→B then B→A within window_sec."""
    rows = []
    try:
        with open(csv_path) as f:
            for r in csv.DictReader(f):
                try:
                    rows.append({
                        "t":    float(r["time_sec"]),
                        "ue":   int(r["ue_id"]),
                        "src":  int(r["from_cell"]),
                        "dst":  int(r["to_cell"]),
                        "ok":   r.get("executed_ok", "1").strip() == "1",
                    })
                except Exception:
                    pass
    except Exception:
        return {"total": 0, "pingpong": 0, "rate_pct": 0.0}

    executed = [r for r in rows if r["ok"]]
    # Group by UE so interleaved handovers from other UEs don't break detection
    from collections import defaultdict
    ue_history: dict = defaultdict(list)
    for r in executed:
        ue_history[r["ue"]].append(r)
    pp = 0
    for ue_rows in ue_history.values():
        for i in range(1, len(ue_rows)):
            a, b = ue_rows[i-1], ue_rows[i]
            if (a["dst"] == b["src"] and a["src"] == b["dst"] and
                    (b["t"] - a["t"]) <= window_sec):
                pp += 1
    total = len(executed)
    rate  = round(pp / total * 100, 2) if total else 0.0
    return {"total": total, "pingpong": pp, "rate_pct": rate}

def _next_sim_number() -> int:
    """Count existing sim### folders and return the next number."""
    try:
        existing = [d for d in os.listdir(RESULTS_DIR)
                    if d.startswith("sim") and len(d) > 5 and d[3:6].isdigit()]
        return len(existing) + 1
    except Exception:
        return 1

def _build_decision_log(csv_path: str, sim_label: str) -> list:
    """Assign UUID to each executed handover and flag ping-pong events."""
    rows = []
    try:
        with open(csv_path) as f:
            for r in csv.DictReader(f):
                if r.get("executed_ok", "0").strip() == "1":
                    rows.append(r)
        rows.sort(key=lambda r: float(r["time_sec"]))
    except Exception:
        return []

    # Mark ping-pong: UE did A→B then B→A within 5 seconds (grouped per UE)
    ue_rows: dict = defaultdict(list)
    for idx, r in enumerate(rows):
        ue_rows[r["ue_id"]].append((idx, r))

    pp_indices = set()
    for ue_list in ue_rows.values():
        for i in range(1, len(ue_list)):
            prev_idx, a = ue_list[i - 1]
            curr_idx, b = ue_list[i]
            if (a["to_cell"] == b["from_cell"] and a["from_cell"] == b["to_cell"] and
                    float(b["time_sec"]) - float(a["time_sec"]) <= 5.0):
                pp_indices.add(prev_idx)

    decisions = []
    for idx, r in enumerate(rows):
        decisions.append({
            "uuid":       str(uuid.uuid4()),
            "sim":        sim_label,
            "time_sec":   float(r["time_sec"]),
            "ue_id":      int(r["ue_id"]),
            "from_cell":  int(r["from_cell"]),
            "to_cell":    int(r["to_cell"]),
            "is_correct": idx not in pp_indices,
        })
    return decisions

def _write_decisions_to_db(decisions: list):
    """Persist decision log to SQLite."""
    try:
        conn = sqlite3.connect(DECISIONS_DB)
        conn.execute("""CREATE TABLE IF NOT EXISTS decisions (
            uuid TEXT PRIMARY KEY,
            sim TEXT,
            time_sec REAL,
            ue_id INTEGER,
            from_cell INTEGER,
            to_cell INTEGER,
            is_correct INTEGER,
            saved_at TEXT
        )""")
        saved_at = datetime.now().isoformat()
        conn.executemany(
            "INSERT OR IGNORE INTO decisions VALUES (?,?,?,?,?,?,?,?)",
            [(d["uuid"], d["sim"], d["time_sec"], d["ue_id"],
              d["from_cell"], d["to_cell"], int(d["is_correct"]), saved_at)
             for d in decisions]
        )
        conn.commit()
        conn.close()
    except Exception:
        pass

def _generate_plots(decisions: list, dest: str, sim_label: str):
    """Generate and save decision quality plots."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        plots_dir = os.path.join(dest, "plots")
        os.makedirs(plots_dir, exist_ok=True)

        times   = [d["time_sec"] for d in decisions]
        correct = [d["is_correct"] for d in decisions]

        # Plot 1: decision quality scatter
        fig, ax = plt.subplots(figsize=(10, 4))
        ax.scatter([t for t, c in zip(times, correct) if c],
                   [1] * sum(correct), color="green", label="Correct", alpha=0.7, s=20)
        ax.scatter([t for t, c in zip(times, correct) if not c],
                   [0] * sum(1 for c in correct if not c), color="red", label="Ping-pong", alpha=0.7, s=20)
        ax.set_xlabel("Simulation Time (s)")
        ax.set_yticks([0, 1])
        ax.set_yticklabels(["Ping-pong", "Correct"])
        ax.set_title(f"{sim_label} — GRU Decision Quality")
        ax.legend()
        fig.tight_layout()
        fig.savefig(os.path.join(plots_dir, "decision_quality.png"), dpi=120)
        plt.close(fig)

        # Plot 2: cumulative handovers over time
        fig, ax = plt.subplots(figsize=(10, 4))
        ax.plot(times, range(1, len(times) + 1), color="steelblue", linewidth=2)
        ax.set_xlabel("Simulation Time (s)")
        ax.set_ylabel("Cumulative Handovers")
        ax.set_title(f"{sim_label} — Handovers Over Time")
        fig.tight_layout()
        fig.savefig(os.path.join(plots_dir, "handovers_over_time.png"), dpi=120)
        plt.close(fig)
    except Exception:
        pass

def save_sim_results(tag: str = ""):
    """Copy results + logs into a numbered folder, generate decision log, plots, and SQLite entry."""
    sim_num   = _next_sim_number()
    sim_label = f"sim{sim_num:03d}"
    ts        = datetime.now().strftime("%Y%m%d_%H%M%S")
    name      = f"{sim_label}_{ts}_{tag}" if tag else f"{sim_label}_{ts}"
    dest      = os.path.join(RESULTS_DIR, name)
    os.makedirs(dest, exist_ok=True)

    # Copy data files
    for src in [HANDOVER_CSV, LSTM_CSV]:
        if os.path.exists(src):
            shutil.copy2(src, dest)

    # Copy component logs
    for key, path in LOG.items():
        if os.path.exists(path):
            shutil.copy2(path, os.path.join(dest, f"{key}.log"))

    # Ping-pong stats
    pp = _calc_pingpong(os.path.join(dest, "handover.csv"))

    # Decision log with UUIDs
    decisions     = _build_decision_log(os.path.join(dest, "handover.csv"), sim_label)
    correct_count = sum(1 for d in decisions if d["is_correct"])
    total_dec     = len(decisions)
    accuracy      = round(100 * correct_count / total_dec, 2) if total_dec else 0.0

    # Write decision_log.csv
    if decisions:
        with open(os.path.join(dest, "decision_log.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=decisions[0].keys())
            w.writeheader()
            w.writerows(decisions)

    # Persist to SQLite
    _write_decisions_to_db(decisions)

    # Generate plots
    if decisions:
        _generate_plots(decisions, dest, sim_label)

    # decision_summary.json
    summary_data = {
        "sim":              sim_label,
        "tag":              tag,
        "timestamp":        ts,
        "total_handovers":  pp["total"],
        "pingpong_events":  pp["pingpong"],
        "pingpong_rate_pct": pp["rate_pct"],
        "correct_decisions": correct_count,
        "total_decisions":  total_dec,
        "accuracy_pct":     accuracy,
    }
    with open(os.path.join(dest, "decision_summary.json"), "w") as f:
        json.dump(summary_data, f, indent=2)

    # summary.txt
    summary_txt = (
        f"Simulation Results — {sim_label} — {ts}\n"
        f"{'='*44}\n"
        f"Total handovers   : {pp['total']}\n"
        f"Ping-pong events  : {pp['pingpong']}\n"
        f"Ping-pong rate    : {pp['rate_pct']}%\n"
        f"GRU accuracy      : {accuracy}% ({correct_count}/{total_dec} correct decisions)\n"
    )
    with open(os.path.join(dest, "summary.txt"), "w") as f:
        f.write(summary_txt)

    return {"folder": dest, "sim": sim_label, "pingpong": pp, "accuracy_pct": accuracy}

_last_result: dict = {}

# ── Process registry ───────────────────────────────────────────────────────────
_procs: dict = {}   # key -> Popen

def _alive(key: str) -> bool:
    p = _procs.get(key)
    if p is not None and p.poll() is None:
        return True
    # For simulation: shell=True means the shell wrapper exits immediately;
    # fall back to pgrep for the actual ns3.42 binary.
    if key == "simulation":
        r = subprocess.run("pgrep -f 'build/scratch/[n]s3.42-'", shell=True, capture_output=True)
        return r.returncode == 0
    return False

def _popen(key: str, cmd, cwd: str, log_key: str, shell=False, env=None):
    _kill(key)
    _procs[key] = subprocess.Popen(
        cmd, shell=shell,
        stdout=open(LOG[log_key], "w"),
        stderr=subprocess.STDOUT,
        cwd=cwd,
        env=env,
        preexec_fn=os.setsid,
    )
    return _procs[key]

def _kill(key: str):
    p = _procs.pop(key, None)
    if p and p.poll() is None:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        except Exception:
            try:
                p.terminate()
            except Exception:
                pass

# ── Helpers ───────────────────────────────────────────────────────────────────
def _docker_running() -> bool:
    r = subprocess.run(
        ["docker", "compose", "ps", "--services", "--filter", "status=running"],
        capture_output=True, text=True, cwd=GUI_DIR
    )
    return bool(r.stdout.strip())

def _xapp_proc_alive() -> bool:
    r = subprocess.run(
        "ps aux | grep '[x]app_rc_handover_ctrl' | grep -v python | grep -v pgrep",
        shell=True, capture_output=True, text=True
    )
    return bool(r.stdout.strip())

# ── Status ─────────────────────────────────────────────────────────────────────
def _xapp_any_alive() -> bool:
    return _alive("xapp_gru") or _alive("xapp_rl") or _xapp_proc_alive()

def _active_xapp_type() -> str:
    if _alive("xapp_rl"):  return "rl"
    if _alive("xapp_gru"): return "gru"
    if _xapp_proc_alive(): return "rc"
    return "none"

@app.get("/ctrl/status")
async def get_status():
    return {
        "docker":           _docker_running(),
        "flexric":          _alive("flexric"),
        "simulation":       _alive("simulation"),
        "pusher":           _alive("pusher"),
        "xapp":             _xapp_any_alive(),
        "active_xapp_type": _active_xapp_type(),
    }

# ── Scenarios ──────────────────────────────────────────────────────────────────
@app.get("/ctrl/scenarios")
async def list_scenarios():
    from pathlib import Path
    scratch = Path(f"{NS3_DIR}/scratch")
    return sorted(f.stem for f in scratch.glob("*.cc") if f.is_file())

# ── Docker backend ─────────────────────────────────────────────────────────────
@app.post("/ctrl/docker/start")
async def start_docker():
    r = subprocess.run(
        ["docker", "compose", "up", "-d", "influxdb", "gui"],
        capture_output=True, text=True, cwd=GUI_DIR
    )
    return {"status": "ok", "out": (r.stdout + r.stderr)[-400:]}

@app.post("/ctrl/docker/stop")
async def stop_docker():
    r = subprocess.run(
        ["docker", "compose", "down"],
        capture_output=True, text=True, cwd=GUI_DIR
    )
    return {"status": "ok", "out": (r.stdout + r.stderr)[-400:]}

# ── FlexRIC ────────────────────────────────────────────────────────────────────
@app.post("/ctrl/flexric/start")
async def start_flexric():
    if _alive("flexric"):
        return {"status": "already_running"}
    _popen("flexric", [FLEXRIC_BIN], cwd=NS3_DIR, log_key="flexric")
    return {"status": "started", "pid": _procs["flexric"].pid}

@app.post("/ctrl/flexric/stop")
async def stop_flexric():
    _kill("flexric")
    subprocess.run("pkill -f nearRT-RIC", shell=True)
    return {"status": "stopped"}

# ── Simulation ─────────────────────────────────────────────────────────────────
class SimParams(BaseModel):
    scenario:   str = "gru_scenario"
    xapp_type:  str = "gru"          # "gru" or "rl"
    n_ues:      int = 20
    n_mmwave:   int = 7
    sim_time:   int = 60
    e2_term_ip: str = "127.0.0.1"

@app.post("/ctrl/simulation/start")
async def start_simulation(params: SimParams):
    if _alive("simulation"):
        return {"status": "already_running"}

    # Clear stale InfluxDB data
    subprocess.run(
        'docker exec $(docker ps -q -f name=influxdb) '
        'influx -database influx -execute "delete from /..*/"',
        shell=True, capture_output=True
    )

    flags = (
        f"--e2TermIp={params.e2_term_ip}"
        f" --hoSinrDifference=3"
        f" --indicationPeriodicity=0.05"
        f" --simTime={params.sim_time}"
        f" --KPM_E2functionID=2"
        f" --RC_E2functionID=3"
        f" --N_MmWaveEnbNodes={params.n_mmwave}"
        f" --N_Ues={params.n_ues}"
    )
    cmd = f'./ns3 run "scratch/{params.scenario}.cc {flags}"'
    _popen("simulation", cmd, cwd=NS3_DIR, log_key="simulation", shell=True)
    return {"status": "started", "pid": _procs["simulation"].pid}

@app.post("/ctrl/simulation/stop")
async def stop_simulation():
    _kill("simulation")
    subprocess.run("pkill -9 -f 'gru_scenario\\|ns3.42'", shell=True)
    return {"status": "stopped"}

# ── Data Pusher ────────────────────────────────────────────────────────────────
@app.post("/ctrl/pusher/start")
async def start_pusher():
    if _alive("pusher"):
        return {"status": "already_running"}
    _popen("pusher", ["python3", "sim_data_pusher.py"],
           cwd=NS3_DIR, log_key="pusher")
    return {"status": "started"}

@app.post("/ctrl/pusher/stop")
async def stop_pusher():
    _kill("pusher")
    subprocess.run("pkill -f sim_data_pusher", shell=True)
    return {"status": "stopped"}

# ── xApp ───────────────────────────────────────────────────────────────────────
@app.post("/ctrl/xapp/start")
async def start_xapp():
    # Ensure trigger server is running
    if not _alive("xapp_trigger"):
        _popen("xapp_trigger", ["python3", "xApp_trigger.py"],
               cwd=XAPP_DIR, log_key="xapp")
        await asyncio.sleep(1.5)

    # Send start command to trigger server
    import urllib.request
    try:
        req = urllib.request.Request(
            "http://localhost:38868/",
            data=b"start",
            headers={"Content-Type": "text/plain"},
            method="POST",
        )
        urllib.request.urlopen(req, timeout=5)
        return {"status": "started"}
    except Exception as e:
        return {"status": "error", "detail": str(e)}

@app.post("/ctrl/xapp/stop")
async def stop_xapp():
    import urllib.request
    try:
        req = urllib.request.Request(
            "http://localhost:38868/",
            data=b"stop",
            headers={"Content-Type": "text/plain"},
            method="POST",
        )
        urllib.request.urlopen(req, timeout=4)
    except Exception:
        pass
    subprocess.run("pkill -f xapp_rc_handover_ctrl", shell=True)
    return {"status": "stopped"}

# ── Launch all ─────────────────────────────────────────────────────────────────
@app.post("/ctrl/launch-all")
async def launch_all(params: SimParams, bg: BackgroundTasks):
    bg.add_task(_launch_all_task, params)
    return {"status": "launching"}

async def _launch_all_task(params: SimParams):
    is_gru = (params.xapp_type == "gru")
    is_rl  = (params.xapp_type == "rl")

    # 0 — Kill ALL stale processes before starting fresh
    subprocess.run("pkill -9 -f 'xapp_handover_gru|xapp_handover_rl|xapp_rc_handover_ctrl'", shell=True)
    subprocess.run("pkill -9 -f 'sim_data_pusher'", shell=True)
    subprocess.run("pkill -9 -f 'gru_xapp.py'", shell=True)
    subprocess.run("pkill -9 -f 'rl_xapp.py'", shell=True)
    subprocess.run("pkill -9 -f 'nearRT-RIC'", shell=True)
    subprocess.run("pkill -9 -f 'ns3.42'", shell=True)
    for key in list(_procs.keys()):
        _kill(key)
    # Clear FlexRIC's own log so E2 count starts fresh
    open("/tmp/flexric.log", "w").close()
    await asyncio.sleep(3)

    # 1 — Docker (InfluxDB + 2D GUI backend — shared by both GUIs)
    subprocess.run(["docker", "compose", "up", "-d", "influxdb", "gui"],
                   capture_output=True, cwd=GUI_DIR)
    # Wait for InfluxDB to accept connections, then reset DB to clear stale field schemas
    for _ in range(30):
        await asyncio.sleep(1)
        r = subprocess.run("curl -sf http://localhost:8086/ping",
                           shell=True, capture_output=True)
        if r.returncode == 0:
            break
    subprocess.run('curl -s -X POST "http://localhost:8086/query" '
                   '--data-urlencode "q=DROP DATABASE influx"',
                   shell=True, capture_output=True)
    subprocess.run('curl -s -X POST "http://localhost:8086/query" '
                   '--data-urlencode "q=CREATE DATABASE influx"',
                   shell=True, capture_output=True)
    await asyncio.sleep(1)

    # 2 — FlexRIC nearRT-RIC — wait until it is actually listening on its E2 port
    _popen("flexric", [FLEXRIC_BIN, "-c", FLEXRIC_CONF],
           cwd=NS3_DIR, log_key="flexric")
    for _ in range(60):
        await asyncio.sleep(1)
        r = subprocess.run("ss -anp | grep ':36421'", shell=True, capture_output=True)
        if r.stdout.strip():
            break   # SCTP E2 port is open — FlexRIC is ready
    await asyncio.sleep(2)   # extra margin

    if not _alive("flexric"):
        return   # FlexRIC failed to start

    # 3a — ML Python service (GRU on port 5000, RL on port 5001)
    if is_gru:
        _popen("gru_service",
               ["python3", GRU_SERVICE],
               cwd=os.path.dirname(GRU_SERVICE), log_key="gru",
               env={**os.environ, "GRU_PORT": str(GRU_PORT)})
        await asyncio.sleep(3)
    elif is_rl:
        _popen("rl_service",
               ["python3", RL_SERVICE],
               cwd=os.path.dirname(RL_SERVICE), log_key="rl",
               env={**os.environ, "RL_PORT": str(RL_PORT)})
        await asyncio.sleep(3)

    # Clear runtime CSVs — ns-3 APPENDS, so stale data from previous run must be wiped
    with open(HANDOVER_CSV, "w") as f:
        f.write("time_sec,ue_id,from_cell,to_cell,event,executed_ok\n")
    open(LSTM_CSV, "w").close()
    open(KPM_CSV, "w").close()

    # 3b — ns-3 simulation
    flags = (
        f"--e2TermIp={params.e2_term_ip}"
        f" --hoSinrDifference=3 --indicationPeriodicity=0.05"
        f" --simTime={params.sim_time}"
        f" --KPM_E2functionID=2 --RC_E2functionID=3"
        f" --N_MmWaveEnbNodes={params.n_mmwave}"
        f" --N_Ues={params.n_ues}"
    )
    cmd = f'./ns3 run "scratch/{params.scenario}.cc {flags}"'
    _popen("simulation", cmd, cwd=NS3_DIR, log_key="simulation", shell=True)

    # Wait for E2 connections via SCTP — FlexRIC doesn't log to stdout
    for _ in range(60):
        await asyncio.sleep(3)
        if not _alive("simulation"):
            return   # ns-3 crashed — stop here, don't start xApp
        r = subprocess.run(
            "ss -Snp 2>/dev/null | grep ':36421' | grep -c ESTAB",
            shell=True, capture_output=True, text=True
        )
        try:
            if int(r.stdout.strip()) >= int(params.n_mmwave):
                break
        except Exception:
            pass

    # Check FlexRIC is still alive before proceeding
    if not _alive("flexric"):
        return

    # 4 — Data pusher
    _popen("pusher", ["python3", "sim_data_pusher.py"],
           cwd=NS3_DIR, log_key="pusher")
    await asyncio.sleep(3)

    # Check simulation still alive before starting xApp
    if not _alive("simulation"):
        return

    # 5 — xApp
    if is_gru:
        # GRU xApp: communicates with gru_xapp.py on port 5000
        if not _alive("xapp_gru"):
            env = {**os.environ, "LSTM_SERVICE_URL": f"http://localhost:{GRU_PORT}"}
            _popen("xapp_gru", [XAPP_GRU_BIN, "-c", FLEXRIC_CONF],
                   cwd=NS3_DIR, log_key="xapp", env=env)
    elif is_rl:
        # RL xApp: communicates with rl_xapp.py on port 5001
        if not _alive("xapp_rl"):
            env = {**os.environ, "RL_SERVICE_URL": f"http://localhost:{RL_PORT}"}
            _popen("xapp_rl", [XAPP_RL_BIN, "-c", FLEXRIC_CONF],
                   cwd=NS3_DIR, log_key="xapp", env=env)
    else:
        # Default: use rc_handover_ctrl via trigger server
        if not _alive("xapp_trigger"):
            _popen("xapp_trigger", ["python3", "xApp_trigger.py"],
                   cwd=XAPP_DIR, log_key="xapp")
            await asyncio.sleep(2)
        import urllib.request
        try:
            urllib.request.urlopen(urllib.request.Request(
                "http://localhost:38868/", data=b"start",
                headers={"Content-Type": "text/plain"}, method="POST"), timeout=5)
        except Exception:
            pass

    # 6 — Wait for simulation to finish, then save results
    while _alive("simulation"):
        await asyncio.sleep(5)

    global _last_result
    _last_result = save_sim_results(tag=f"{params.scenario}_{params.xapp_type}")

# ── Last result ────────────────────────────────────────────────────────────────
@app.get("/ctrl/last-result")
async def last_result():
    return _last_result if _last_result else {"status": "no results yet"}

# ── Stop all ───────────────────────────────────────────────────────────────────
@app.post("/ctrl/stop-all")
async def stop_all():
    import urllib.request
    try:
        urllib.request.urlopen(urllib.request.Request(
            "http://localhost:38868/", data=b"stop",
            headers={"Content-Type": "text/plain"}, method="POST"), timeout=4)
    except Exception:
        pass
    for key in list(_procs.keys()):
        _kill(key)
    subprocess.run("pkill -9 -f 'xapp_rc_handover_ctrl|xapp_handover_gru|xapp_handover_rl'", shell=True)
    subprocess.run("pkill -9 -f 'gru_xapp.py'", shell=True)
    subprocess.run("pkill -9 -f 'rl_xapp.py'", shell=True)
    subprocess.run("pkill -9 -f 'sim_data_pusher'", shell=True)
    subprocess.run("pkill -9 -f 'nearRT-RIC'", shell=True)
    subprocess.run("pkill -9 -f 'ns3.42'", shell=True)
    _kill("flexric");  subprocess.run("pkill -f nearRT-RIC", shell=True)
    return {"status": "all_stopped"}

# ── Logs ───────────────────────────────────────────────────────────────────────
@app.get("/ctrl/logs/{component}")
async def get_logs(component: str, lines: int = 40):
    path = LOG.get(component)
    if not path or not os.path.exists(path):
        return {"lines": []}
    with open(path, errors="replace") as f:
        data = f.readlines()
    return {"lines": [l.rstrip() for l in data[-lines:]]}

# ── Decision log ───────────────────────────────────────────────────────────────
@app.get("/ctrl/decisions")
async def get_decisions(sim: str = None, limit: int = 500):
    """Return decision log from SQLite. Filter by sim label (e.g. sim005) or return all."""
    if not os.path.exists(DECISIONS_DB):
        return {"decisions": [], "message": "No decisions recorded yet"}
    try:
        conn = sqlite3.connect(DECISIONS_DB)
        conn.row_factory = sqlite3.Row
        if sim:
            rows = conn.execute(
                "SELECT * FROM decisions WHERE sim=? ORDER BY time_sec LIMIT ?",
                (sim, limit)).fetchall()
        else:
            rows = conn.execute(
                "SELECT * FROM decisions ORDER BY sim, time_sec LIMIT ?",
                (limit,)).fetchall()
        conn.close()
        return {"decisions": [dict(r) for r in rows]}
    except Exception as e:
        return {"error": str(e)}
