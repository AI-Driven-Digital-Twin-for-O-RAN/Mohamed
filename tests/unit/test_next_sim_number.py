"""
Unit tests for controller._next_sim_number.

The function counts existing simNNN_* directories under RESULTS_DIR and
returns the next sequential number. We monkeypatch the module-level
RESULTS_DIR so the test runs in isolation.
"""
from __future__ import annotations

import controller


def test_starts_at_1_when_dir_missing(tmp_path, monkeypatch):
    monkeypatch.setattr(controller, "RESULTS_DIR", str(tmp_path / "absent"))
    assert controller._next_sim_number() == 1


def test_starts_at_1_when_dir_empty(tmp_path, monkeypatch):
    (tmp_path / "results").mkdir()
    monkeypatch.setattr(controller, "RESULTS_DIR", str(tmp_path / "results"))
    assert controller._next_sim_number() == 1


def test_counts_existing_sim_dirs(tmp_path, monkeypatch):
    results = tmp_path / "results"
    results.mkdir()
    for name in ("sim001_a", "sim002_b", "sim003_c"):
        (results / name).mkdir()
    monkeypatch.setattr(controller, "RESULTS_DIR", str(results))
    assert controller._next_sim_number() == 4


def test_ignores_non_sim_dirs(tmp_path, monkeypatch):
    results = tmp_path / "results"
    results.mkdir()
    (results / "sim001_run").mkdir()
    (results / "logs").mkdir()                # not a sim
    (results / "sim_NaN").mkdir()             # name pattern doesn't match
    (results / "README.md").write_text("hi")  # plain file
    monkeypatch.setattr(controller, "RESULTS_DIR", str(results))
    assert controller._next_sim_number() == 2
