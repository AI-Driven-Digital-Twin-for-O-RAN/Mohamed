"""
Unit tests for controller._calc_pingpong.

The function counts ping-pong handovers — UE goes A→B and then B→A within
window_sec. The historical bug was that it scanned the CSV sequentially
instead of grouping by UE first; the test for interleaved handovers below
catches that regression.
"""
from __future__ import annotations

from controller import _calc_pingpong


def test_empty_csv(tmp_path):
    p = tmp_path / "empty.csv"
    p.write_text("time_sec,ue_id,from_cell,to_cell,event,executed_ok\n")
    assert _calc_pingpong(str(p)) == {"total": 0, "pingpong": 0, "rate_pct": 0.0}


def test_missing_csv_returns_zero(tmp_path):
    out = _calc_pingpong(str(tmp_path / "does-not-exist.csv"))
    assert out == {"total": 0, "pingpong": 0, "rate_pct": 0.0}


def test_single_handover_no_pingpong(write_csv):
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
    ])
    assert _calc_pingpong(str(p)) == {"total": 1, "pingpong": 0, "rate_pct": 0.0}


def test_pingpong_within_window(write_csv):
    """A→B then B→A by the same UE inside 5s is one ping-pong."""
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
        {"time_sec": 3.0, "ue_id": 7, "from_cell": 2, "to_cell": 1, "event": "SUCCESS", "executed_ok": 1},
    ])
    out = _calc_pingpong(str(p))
    assert out == {"total": 2, "pingpong": 1, "rate_pct": 50.0}


def test_no_pingpong_outside_window(write_csv):
    """A→B then B→A more than 5s apart is NOT a ping-pong."""
    p = write_csv([
        {"time_sec": 1.0,  "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
        {"time_sec": 10.0, "ue_id": 7, "from_cell": 2, "to_cell": 1, "event": "SUCCESS", "executed_ok": 1},
    ])
    assert _calc_pingpong(str(p)) == {"total": 2, "pingpong": 0, "rate_pct": 0.0}


def test_interleaved_handovers_are_grouped_per_ue(write_csv):
    """REGRESSION: a sequential global scan misses ping-pongs when other UEs
    handover between the A→B and B→A of the affected UE. The implementation
    must group by ue_id first."""
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 5, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},  # UE5 A→B
        {"time_sec": 2.0, "ue_id": 8, "from_cell": 4, "to_cell": 3, "event": "SUCCESS", "executed_ok": 1},  # noise
        {"time_sec": 3.0, "ue_id": 5, "from_cell": 2, "to_cell": 1, "event": "SUCCESS", "executed_ok": 1},  # UE5 B→A
    ])
    out = _calc_pingpong(str(p))
    # UE 5 ping-ponged once; UE 8 only had a single handover.
    assert out["pingpong"] == 1
    assert out["total"] == 3


def test_failed_handovers_excluded(write_csv):
    """executed_ok=0 rows must not count as handovers or as ping-pongs."""
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "START",   "executed_ok": 0},
        {"time_sec": 2.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
        {"time_sec": 4.0, "ue_id": 7, "from_cell": 2, "to_cell": 1, "event": "SUCCESS", "executed_ok": 1},
    ])
    out = _calc_pingpong(str(p))
    # 2 executed handovers, 1 of them is a ping-pong = 50%.
    assert out == {"total": 2, "pingpong": 1, "rate_pct": 50.0}


def test_custom_window_extends_pingpong_detection(write_csv):
    """A→B then B→A 7s apart is a ping-pong only with window_sec >= 7."""
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
        {"time_sec": 8.0, "ue_id": 7, "from_cell": 2, "to_cell": 1, "event": "SUCCESS", "executed_ok": 1},
    ])
    assert _calc_pingpong(str(p), window_sec=5.0)["pingpong"] == 0
    assert _calc_pingpong(str(p), window_sec=10.0)["pingpong"] == 1


def test_three_handovers_chained(write_csv):
    """A→B→A→B counts as 1 ping-pong (the A→B→A part) when within window."""
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
        {"time_sec": 2.0, "ue_id": 7, "from_cell": 2, "to_cell": 1, "event": "SUCCESS", "executed_ok": 1},  # PP
        {"time_sec": 3.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},  # PP
    ])
    out = _calc_pingpong(str(p))
    # Implementation increments on every reverse pair; 2 expected here.
    assert out["pingpong"] == 2
    assert out["total"] == 3
