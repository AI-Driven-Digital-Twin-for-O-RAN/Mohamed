"""
Pytest fixtures + import path setup for unit tests.

The controller module lives at platform/controller/controller.py. We add that
directory to sys.path so tests can `from controller import _calc_pingpong`.
"""
from __future__ import annotations

import csv
import sys
from collections.abc import Iterable
from pathlib import Path

import pytest

# Make the controller importable.
ROOT = Path(__file__).resolve().parents[2]
CONTROLLER_DIR = ROOT / "platform" / "controller"
sys.path.insert(0, str(CONTROLLER_DIR))


def _write_csv(path: Path, rows: Iterable[dict]) -> Path:
    """Write a handover.csv-shaped file. Header matches the controller's reader."""
    fieldnames = ["time_sec", "ue_id", "from_cell", "to_cell", "event", "executed_ok"]
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            full = {k: "" for k in fieldnames}
            full.update(r)
            w.writerow(full)
    return path


@pytest.fixture
def write_csv(tmp_path):
    """Returns a callable: write_csv(rows) -> Path to a temp handover.csv."""
    counter = {"i": 0}

    def _make(rows: Iterable[dict]) -> Path:
        counter["i"] += 1
        return _write_csv(tmp_path / f"handover-{counter['i']}.csv", rows)

    return _make
