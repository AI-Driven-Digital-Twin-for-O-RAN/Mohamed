#!/usr/bin/env python3
"""
Training script for DDQN-based RL handover model using VHDAEnv.
Trains on vhda_synthetic_normalized.csv (with column renaming).
Saves weights to omar_salama/models/rl_ns3_handover.pth
"""

import os
import sys
import numpy as np
import pandas as pd
import torch
from pathlib import Path

# Ensure src/ is on the path
BASE_DIR = Path(__file__).parent
sys.path.insert(0, str(BASE_DIR / "src"))

from agent import DDQNAgent
from env import VHDAEnv

# ── Column mapping ─────────────────────────────────────────────────────────────
# CSV columns: RSSI_WIFI, SINR_WIFI, throughput_WIFI, latency_WIFI,
#              RSRP_5G,   SINR_5G,   throughput_5G,   latency_5G
# env.py needs: Level, SNR, CQI, DL_bitrate, SecondCell_RSRP, SecondCell_SNR,
#               Speed, NRxLev1
COLUMN_MAP = {
    "RSRP_5G":       "Level",
    "SINR_5G":       "SNR",
    "throughput_5G": "CQI",
    "throughput_WIFI":"DL_bitrate",
    "RSSI_WIFI":     "SecondCell_RSRP",
    "SINR_WIFI":     "SecondCell_SNR",
    "latency_WIFI":  "Speed",
    "latency_5G":    "NRxLev1",
}

CSV_SRC  = BASE_DIR / "Datasets" / "vhda_synthetic_normalized.csv"
CSV_TMP  = Path("/tmp/rl_training_data.csv")
MODEL_DIR = BASE_DIR / "models"
MODEL_PATH = MODEL_DIR / "rl_ns3_handover.pth"

# ── Training hyperparameters ───────────────────────────────────────────────────
N_EPISODES = 300
MAX_STEPS  = 500
STATE_SIZE = 8 * 5    # 8 features x 5-step window
ACTION_SIZE = 2       # 0=stay, 1=handover
PRINT_EVERY = 50

def prepare_csv():
    """Rename CSV columns and save to /tmp for VHDAEnv."""
    df = pd.read_csv(CSV_SRC)
    df = df.rename(columns=COLUMN_MAP)
    df.to_csv(CSV_TMP, index=False)
    print(f"[train] CSV prepared: {CSV_TMP}  ({len(df)} rows, cols: {list(df.columns)})")

def train():
    prepare_csv()

    env   = VHDAEnv(csv_path=str(CSV_TMP), max_steps=MAX_STEPS)
    agent = DDQNAgent(
        state_size   = STATE_SIZE,
        action_size  = ACTION_SIZE,
        lr           = 1e-3,
        gamma        = 0.99,
        epsilon      = 1.0,
        epsilon_decay= 0.995,
        epsilon_min  = 0.02,
        batch_size   = 64,
        memory_size  = 10000,
        target_update_freq = 10,
    )

    rewards_history = []
    pp_history      = []

    print(f"\n[train] Starting DDQN training — {N_EPISODES} episodes, max_steps={MAX_STEPS}")
    print("-" * 60)

    for episode in range(1, N_EPISODES + 1):
        state, _ = env.reset()
        total_reward = 0.0
        done = False

        while not done:
            action = agent.choose_action(state)
            next_state, reward, done, _, _ = env.step(action)
            agent.remember(state, action, reward, next_state, float(done))
            agent.learn()
            state = next_state
            total_reward += reward

        agent.decay_epsilon()
        rewards_history.append(total_reward)

        stats = env.get_qos_stats()
        pp_history.append(stats["Ping Pong Count"])

        if episode % PRINT_EVERY == 0 or episode == 1:
            avg_reward = np.mean(rewards_history[-PRINT_EVERY:])
            avg_pp     = np.mean(pp_history[-PRINT_EVERY:])
            print(
                f"  Ep {episode:4d}/{N_EPISODES} | "
                f"Reward: {total_reward:7.2f} (avg {avg_reward:7.2f}) | "
                f"PP: {stats['Ping Pong Count']:3d} (avg {avg_pp:.1f}) | "
                f"Correct HO: {stats['Correct Handovers']:3d} | "
                f"Eps: {agent.epsilon:.4f}"
            )

    # ── Save model ─────────────────────────────────────────────────────────────
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    torch.save(agent.online_net.state_dict(), str(MODEL_PATH))
    print(f"\n[train] Model saved to {MODEL_PATH}")

    # ── Final stats ────────────────────────────────────────────────────────────
    final_stats = env.get_qos_stats()
    avg_reward_last50 = np.mean(rewards_history[-50:]) if len(rewards_history) >= 50 else np.mean(rewards_history)
    avg_pp_last50     = np.mean(pp_history[-50:])      if len(pp_history) >= 50      else np.mean(pp_history)
    avg_pp_first50    = np.mean(pp_history[:50])        if len(pp_history) >= 50      else np.mean(pp_history)

    print("\n" + "=" * 60)
    print("  TRAINING COMPLETE")
    print("=" * 60)
    print(f"  Episodes trained         : {N_EPISODES}")
    print(f"  Avg reward (last 50 eps) : {avg_reward_last50:.2f}")
    print(f"  Avg PP (first 50 eps)    : {avg_pp_first50:.2f}")
    print(f"  Avg PP (last  50 eps)    : {avg_pp_last50:.2f}")
    print(f"  PP improvement           : {avg_pp_first50 - avg_pp_last50:.2f}")
    print(f"  Final epsilon            : {agent.epsilon:.4f}")
    print(f"  Model path               : {MODEL_PATH}")
    print("=" * 60)

if __name__ == "__main__":
    train()
