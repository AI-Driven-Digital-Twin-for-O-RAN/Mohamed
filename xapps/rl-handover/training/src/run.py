import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from env import VHDAEnv
from agent import DDQNAgent
import matplotlib.pyplot as plt
import numpy as np

def train_agent(mobility_group, episodes=1500):
    print(f"\n{'='*50}")
    print(f"Training Agent for: {mobility_group}")
    print(f"{'='*50}")

    env = VHDAEnv(max_steps=500, mobility_group=mobility_group)
    agent = DDQNAgent(state_size=8 * 5, action_size=2)

    episode_rewards = []
    unho_list = []
    correct_ho_list = []
    pp_list = []

    first_episode_stats = None

    for ep in range(episodes):
        state, _ = env.reset()
        done = False
        total_reward = 0

        while not done:
            action = agent.choose_action(state)
            next_state, reward, done, _, _ = env.step(action)
            agent.remember(state, action, reward, next_state, done)
            agent.learn()
            state = next_state
            total_reward += reward

        stats = env.get_qos_stats()
        episode_rewards.append(total_reward)
        unho_list.append(stats["Unnecessary Handovers"])
        correct_ho_list.append(stats["Correct Handovers"])
        pp_list.append(stats["Ping Pong Count"])

        if ep == 0:
            first_episode_stats = (
                total_reward,
                stats["Correct Handovers"],
                stats["Unnecessary Handovers"],
                stats["Ping Pong Count"]
            )

        if (ep + 1) % 50 == 0:
            print(f"Episode {ep+1}/{episodes} | "
                  f"Reward: {total_reward:.3f} | "
                  f"Correct HO: {stats['Correct Handovers']} | "
                  f"Unnecessary HO: {stats['Unnecessary Handovers']} | "
                  f"Ping-Pong: {stats['Ping Pong Count']} | "
                  f"Epsilon: {agent.epsilon:.3f}")

        agent.decay_epsilon()

    last_stats = (
        episode_rewards[-1],
        correct_ho_list[-1],
        unho_list[-1],
        pp_list[-1]
    )

    # حساب نسبة التحسين
    pp_reduction = ((first_episode_stats[3] - last_stats[3]) / first_episode_stats[3] * 100) if first_episode_stats[3] > 0 else 0
    ho_reduction = ((first_episode_stats[2] - last_stats[2]) / first_episode_stats[2] * 100) if first_episode_stats[2] > 0 else 0

    print(f"\n=== {mobility_group} FIRST vs LAST ===")
    print(f"Reward:         {first_episode_stats[0]:.3f}  ->  {last_stats[0]:.3f}")
    print(f"Correct HO:     {first_episode_stats[1]}  ->  {last_stats[1]}")
    print(f"Unnecessary HO: {first_episode_stats[2]}  ->  {last_stats[2]}")
    print(f"Ping-Pong:      {first_episode_stats[3]}  ->  {last_stats[3]}")
    print(f"HO Reduction:   {ho_reduction:.1f}%")
    print(f"PP Reduction:   {pp_reduction:.1f}%")

    return episode_rewards, unho_list, correct_ho_list, pp_list

def main():
    print("Training started with User Clustering + Ping-Pong Detection...")

    driving_rewards, driving_unho, driving_correct, driving_pp = train_agent("Driving")
    walking_rewards, walking_unho, walking_correct, walking_pp = train_agent("Walking")

    fig, axes = plt.subplots(4, 1, figsize=(12, 16))

    axes[0].plot(driving_rewards, label="Driving Agent", color="blue")
    axes[0].plot(walking_rewards, label="Walking Agent", color="orange")
    axes[0].set_title("Total Reward per Episode — Driving vs Walking")
    axes[0].set_xlabel("Episode")
    axes[0].set_ylabel("Total Reward")
    axes[0].legend()
    axes[0].grid(True)

    axes[1].plot(driving_correct, label="Driving Agent", color="blue")
    axes[1].plot(walking_correct, label="Walking Agent", color="orange")
    axes[1].set_title("Correct Handovers per Episode — Driving vs Walking")
    axes[1].set_xlabel("Episode")
    axes[1].set_ylabel("Count")
    axes[1].legend()
    axes[1].grid(True)

    axes[2].plot(driving_unho, label="Driving Agent", color="blue")
    axes[2].plot(walking_unho, label="Walking Agent", color="orange")
    axes[2].set_title("Unnecessary Handovers per Episode — Driving vs Walking")
    axes[2].set_xlabel("Episode")
    axes[2].set_ylabel("Count")
    axes[2].legend()
    axes[2].grid(True)

    axes[3].plot(driving_pp, label="Driving Agent", color="blue")
    axes[3].plot(walking_pp, label="Walking Agent", color="orange")
    axes[3].set_title("Ping-Pong Handovers per Episode — Driving vs Walking")
    axes[3].set_xlabel("Episode")
    axes[3].set_ylabel("Count")
    axes[3].legend()
    axes[3].grid(True)

    plt.tight_layout()
    results_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Results')
    os.makedirs(results_dir, exist_ok=True)
    plt.savefig(os.path.join(results_dir, "clustering_pp_results.png"))
    print("\nResults saved in Results/clustering_pp_results.png")
    plt.show()

main()
