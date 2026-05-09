"""
Unit tests for controller._build_decision_log.

The function:
  - reads the handover CSV,
  - filters out failed handovers (executed_ok != 1),
  - assigns a UUID to each remaining row,
  - flags ping-pong rows with is_correct=False, others True.
"""
from __future__ import annotations

import uuid

from controller import _build_decision_log


def test_empty_csv_returns_empty(tmp_path):
    p = tmp_path / "empty.csv"
    p.write_text("time_sec,ue_id,from_cell,to_cell,event,executed_ok\n")
    assert _build_decision_log(str(p), "sim999") == []


def test_failed_rows_filtered(write_csv):
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "START",   "executed_ok": 0},
        {"time_sec": 2.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
    ])
    decisions = _build_decision_log(str(p), "sim001")
    assert len(decisions) == 1
    assert decisions[0]["ue_id"] == 7
    assert decisions[0]["sim"]   == "sim001"


def test_pingpong_marked_incorrect(write_csv):
    """In an A→B then B→A pair within 5s, the FIRST decision (A→B) is the
    one that turned out to be wrong, so is_correct=False on it."""
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 7, "from_cell": 1, "to_cell": 2, "event": "SUCCESS", "executed_ok": 1},
        {"time_sec": 3.0, "ue_id": 7, "from_cell": 2, "to_cell": 1, "event": "SUCCESS", "executed_ok": 1},
    ])
    decisions = _build_decision_log(str(p), "sim001")
    assert len(decisions) == 2
    # Sorted by time_sec; first is the bad call.
    assert decisions[0]["is_correct"] is False
    assert decisions[1]["is_correct"] is True


def test_uuids_unique_and_well_formed(write_csv):
    p = write_csv([
        {"time_sec": float(i), "ue_id": i, "from_cell": 1, "to_cell": 2,
         "event": "SUCCESS", "executed_ok": 1} for i in range(5)
    ])
    decisions = _build_decision_log(str(p), "sim042")
    uuids = [d["uuid"] for d in decisions]
    assert len(uuids) == len(set(uuids)) == 5
    for u in uuids:
        # Will raise if not a valid UUID string.
        uuid.UUID(u)


def test_decisions_carry_sim_label(write_csv):
    p = write_csv([
        {"time_sec": 1.0, "ue_id": 1, "from_cell": 1, "to_cell": 2,
         "event": "SUCCESS", "executed_ok": 1},
    ])
    decisions = _build_decision_log(str(p), "sim123")
    assert decisions[0]["sim"] == "sim123"
