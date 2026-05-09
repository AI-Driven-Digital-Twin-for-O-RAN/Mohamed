#!/usr/bin/env python3
"""
train_rl_ns3_v2.py — Retrain DDQN on REAL NS-3 mmWave data.

Differences from train_rl_ns3.py:
  - Uses lstm_features.csv produced by the NS-3 / C-xApp pipeline
    (copied to Datasets/ns3_mmwave_dataset.csv before running)
  - No column renaming — columns already match VHDAEnv's expected names
  - Larger max_steps (2000) so each episode covers more real trajectories
  - 500 training episodes
  - Saves to models/rl_ns3_handover_v2.pth

Usage:
    # 1. Copy the NS-3 trace after a simulation run:
    #    cp ~/lstm_features.csv omar_salama/Datasets/ns3_mmwave_dataset.csv
    # 2. Run:
    #    cd /home/omar_farouk/open-ran-clean
    #    python3 omar_salama/train_rl_ns3_v2.py
"""

import os, sys
import numpy as np
import pandas as pd
import torch
from pathlib import Path

BASE_DIR   = Path(__file__).parent
sys.path.insert(0, str(BASE_DIR / "src"))

from agent import DDQNAgent
from env   import VHDAEnv

# ── Paths ──────────────────────────────────────────────────────────────────────
CSV_PATH   = BASE_DIR / "Datasets" / "ns3_mmwave_dataset.csv"
MODEL_DIR  = BASE_DIR / "models"
MODEL_V2   = MODEL_DIR / "rl_ns3_handover_v2.pth"

# ── Hyperparameters ────────────────────────────────────────────────────────────
N_EPISODES  = 500
MAX_STEPS   = 2000   # larger than v1 — covers more real mmWave trajectories
STATE_SIZE  = 8 * 5  # 8 features × 5-step window
ACTION_SIZE = 2
PRINT_EVERY = 50

FEATURES = ['Level', 'SNR', 'CQI', 'DL_bitrate',
            'SecondCell_RSRP', 'SecondCell_SNR', 'Speed', 'NRxLev1']

def prepare_dataset():
    """
    Load lstm_features.csv, sort by UE and time so temporal windows
    are coherent, clean nulls, and save as training CSV.
    """
    src = Path("/home/omar_farouk/lstm_features.csv")
    if not src.exists():
        raise FileNotFoundError(f"NS-3 trace not found: {src}\n"
                                "Run a simulation first, then retrain.")

    df = pd.read_csv(src)
    print(f"[data] Loaded {len(df)} rows from {src}")

    # Sort by UE (IMSI) then Time for temporal coherence
    if 'IMSI' in df.columns and 'Time' in df.columns:
        df = df.sort_values(['IMSI', 'Time']).reset_index(drop=True)

    # Keep only the 8 model features
    missing = [f for f in FEATURES if f not in df.columns]
    if missing:
        raise ValueError(f"Missing columns in NS-3 trace: {missing}")

    df = df[FEATURES].copy()
    df.fillna(df.mean(), inplace=True)

    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(CSV_PATH, index=False)

    print(f"[data] Saved training dataset: {CSV_PATH}  ({len(df)} rows)")
    print("[data] Feature ranges in real NS-3 data:")
    for f in FEATURES:
        print(f"         {f:20s}: [{df[f].min():.2f}, {df[f].max():.2f}]  mean={df[f].mean():.2f}")
    return len(df)

def train():
    n_rows = prepare_dataset()

    env = VHDAEnv(
        csv_path    = str(CSV_PATH),
        max_steps   = min(MAX_STEPS, n_rows - 1),
        handover_penalty = 0.5,
        window_size = 5,
    )

    agent = DDQNAgent(
        state_size        = STATE_SIZE,
        action_size       = ACTION_SIZE,
        lr                = 5e-4,        # lower LR for finer convergence
        gamma             = 0.99,
        epsilon           = 1.0,
        epsilon_decay     = 0.993,       # slower decay — more exploration on real data
        epsilon_min       = 0.02,
        batch_size        = 64,
        memory_size       = 20000,       # larger replay buffer
        target_update_freq= 10,
    )

    rewards_history = []
    pp_history      = []
    ho_history      = []

    print(f"\n[train] DDQN v2 training — {N_EPISODES} episodes, "
          f"max_steps={env.data.shape[0]}, dataset=real NS-3 mmWave")
    print("=" * 65)

    for ep in range(1, N_EPISODES + 1):
        state, _ = env.reset()
        total_reward = 0.0
        done = False

        while not done:
            action     = agent.choose_action(state)
            next_state, reward, done, _, _ = env.step(action)
            agent.remember(state, action, reward, next_state, float(done))
            agent.learn()
            state       = next_state
            total_reward += reward

        agent.decay_epsilon()
        stats = env.get_qos_stats()

        rewards_history.append(total_reward)
        pp_history.append(stats["Ping Pong Count"])
        ho_history.append(stats["Correct Handovers"])

        if ep % PRINT_EVERY == 0 or ep == 1:
            avg_r  = np.mean(rewards_history[-PRINT_EVERY:])
            avg_pp = np.mean(pp_history[-PRINT_EVERY:])
            avg_ho = np.mean(ho_history[-PRINT_EVERY:])
            print(f"  Ep {ep:4d}/{N_EPISODES} | "
                  f"Reward: {total_reward:8.2f} (avg {avg_r:8.2f}) | "
                  f"PP: {stats['Ping Pong Count']:3d} (avg {avg_pp:.1f}) | "
                  f"Correct HO: {stats['Correct Handovers']:3d} (avg {avg_ho:.1f}) | "
                  f"Eps: {agent.epsilon:.4f}")

    # ── Save v2 model ──────────────────────────────────────────────────────────
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    torch.save(agent.online_net.state_dict(), str(MODEL_V2))
    print(f"\n[train] Model v2 saved → {MODEL_V2}")

    # ── Summary ────────────────────────────────────────────────────────────────
    first50_pp = np.mean(pp_history[:50])  if len(pp_history) >= 50 else np.mean(pp_history)
    last50_pp  = np.mean(pp_history[-50:]) if len(pp_history) >= 50 else np.mean(pp_history)
    first50_r  = np.mean(rewards_history[:50])  if len(rewards_history) >= 50 else np.mean(rewards_history)
    last50_r   = np.mean(rewards_history[-50:]) if len(rewards_history) >= 50 else np.mean(rewards_history)

    print("\n" + "=" * 65)
    print("  TRAINING COMPLETE — v2 (real NS-3 mmWave data)")
    print("=" * 65)
    print(f"  Episodes          : {N_EPISODES}")
    print(f"  Dataset           : real mmWave NS-3 traces ({n_rows} rows)")
    print(f"  Avg reward  first 50 eps : {first50_r:.2f}")
    print(f"  Avg reward  last  50 eps : {last50_r:.2f}")
    print(f"  Avg PP      first 50 eps : {first50_pp:.2f}")
    print(f"  Avg PP      last  50 eps : {last50_pp:.2f}")
    print(f"  PP improvement           : {first50_pp - last50_pp:.2f}")
    print(f"  Final epsilon            : {agent.epsilon:.4f}")
    print(f"  Model saved to           : {MODEL_V2}")
    print("=" * 65)
    print()
    print("  Next steps:")
    print("  1. Update rl_xapp.py MODEL_PATH to point to v2.pth")
    print("  2. bash kill_sim.sh && bash rl.sh 120")
    print("  3. Compare PP and HO counts vs GRU simulation")
    print("=" * 65)

if __name__ == "__main__":
    train()
