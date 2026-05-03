#!/usr/bin/env python3
"""Live LB simulation stats — run anytime during or after simulation."""
import csv, os, sys, collections

def main():
    path = os.path.expanduser("~/alyaadone.csv")
    rows = []
    try:
        with open(path) as f:
            rows = list(csv.DictReader(f))
    except:
        print("alyaadone.csv not found — simulation not started yet"); return

    ho_cmds   = [r for r in rows if r.get("event") == "COMMAND_SENT"]
    completed = [r for r in rows if r.get("event") == "COMPLETED" and r.get("executed_ok") == "1"]
    failed    = [r for r in rows if r.get("event") == "COMPLETED" and r.get("executed_ok") == "0"]

    try:
        sim_now = float(rows[-1].get("sim_time_sec", 0))
    except:
        sim_now = 0.0

    # PP detection (8s window, 3-hop ring buffer — mirrors xApp v4)
    PP_WINDOW = 8.0
    RING = 3
    ring = {}
    pp_count  = 0
    pp_events = []

    for r in sorted(ho_cmds, key=lambda x: float(x.get("sim_time_sec", 0))):
        uid = r.get("ue_id")
        t   = float(r.get("sim_time_sec", 0))
        frm = r.get("from_cell")
        to  = r.get("to_cell")
        buf = ring.setdefault(uid, collections.deque(maxlen=RING))
        for hist_cell, hist_t in buf:
            if hist_cell == to and 0 < (t - hist_t) < PP_WINDOW:
                pp_count += 1
                pp_events.append((uid, frm, to, round(t - hist_t, 2)))
                break
        buf.append((frm, t))

    total_ho = len(ho_cmds)
    pp_rate  = 100.0 * pp_count / total_ho if total_ho > 0 else 0.0
    cdr      = len(failed) / total_ho if total_ho > 0 else 0.0

    load_reduced = sum(1 for r in ho_cmds if "LOAD_REDUCED" in r.get("load_balance_result",""))
    ncl_pass     = sum(1 for r in ho_cmds if "PASSED"       in r.get("NCL_gate_result",""))
    qos_blocked  = sum(1 for r in ho_cmds if "QoS-GATE"     in r.get("Algo1_HO_decision",""))
    rescue_ho    = sum(1 for r in ho_cmds if "RESCUE"       in r.get("Algo1_HO_decision",""))

    # Instantaneous cell load (most recent entry per UE from sinr_xapp.csv)
    ue_cell = {}
    sinr_path = os.path.expanduser("~/sinr_xapp.csv")
    try:
        with open(sinr_path) as f:
            for row in csv.DictReader(f):
                ue_cell[row.get("ue_id")] = row.get("cell_id","?")
    except:
        pass
    inst_cell_count = collections.Counter(ue_cell.values())

    print(f"\n{'═'*58}")
    print(f"  v4 LIVE STATS  ──  sim_time = {sim_now:.2f} / 60.0 sec  ({sim_now/60*100:.0f}%)")
    print(f"{'═'*58}")
    print(f"  HO commands    : {total_ho}")
    print(f"  Completed ✓    : {len(completed)}")
    print(f"  Failed ✗       : {len(failed)}   CDR = {cdr*100:.2f}%")
    print()
    print(f"  ── Ping-Pong (8 sim-sec window, 3-hop) ─────────")
    pp_color = "\033[92m" if pp_rate < 10 else "\033[91m"
    print(f"  PP events      : {pp_count} / {total_ho}")
    print(f"  PP rate        : {pp_color}{pp_rate:.1f}%\033[0m  (target: 5–10%)")
    if pp_events:
        for uid,frm,to,dt in pp_events[-5:]:
            print(f"    UE {uid}: …→{frm}→{to}  Δt={dt}s")
    print()
    print(f"  ── QoS & Load Balance ───────────────────────────")
    print(f"  NCL gate pass  : {ncl_pass}/{total_ho}")
    print(f"  Load-reduced   : {load_reduced}/{total_ho}  ({100*load_reduced/total_ho:.0f}%)" if total_ho else "  --")
    if rescue_ho:
        print(f"  Rescue HOs     : {rescue_ho}  (serving SINR < -3 dB → moved to better cell)")
    if qos_blocked:
        print(f"  QoS-blocked    : {qos_blocked}  (target SINR too poor — HO prevented)")
    print()
    if inst_cell_count:
        print(f"  ── Instantaneous Cell Load (latest SINR snapshot) ─")
        # Filter out header rows (non-numeric cell IDs like "cell_id")
        inst_cell_count = {k: v for k, v in inst_cell_count.items() if k.isdigit()}
        total_ues = sum(inst_cell_count.values())
        for cid in sorted(inst_cell_count, key=lambda x: int(x)):
            n = inst_cell_count[cid]
            bar = "█" * n + "░" * max(0, 8-n)
            pct = 100*n/total_ues if total_ues else 0
            print(f"    Cell {cid:>2}: {bar}  {n} UEs  ({pct:.0f}%)")
        nues = list(inst_cell_count.values())
        if len(nues) > 1:
            avg = sum(nues)/len(nues)
            var = sum((x-avg)**2 for x in nues)/len(nues)
            print(f"    Avg={avg:.1f}  Variance={var:.2f}")
    print(f"{'═'*58}\n")

if __name__ == "__main__":
    main()
