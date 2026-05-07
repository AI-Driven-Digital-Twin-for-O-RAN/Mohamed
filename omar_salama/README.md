# Q-Learning Based Vertical Handover Decision Algorithm (VHDA)

**Author** Mert - Electrical & Electronics Engineering Student

## Project Overview

This project implements a **Q-learning based Vertical Handover Decision Algorithm (VHDA)** designed for heterogeneous wireless networks such as **WiFi and 5G**, for heterogeneous wireless network users with mobility support.

The goal of the algorithm is to **intelligently select the optimal Radio Access Technology (RAT)** at each decision step by learning a policy that balances:

* **Quality of Service (QoS)** (throughput & latency)
* **Handover cost** (penalizing unnecessary handovers)

Unlike traditional rule-based or statistical handover mechanisms, this project adopts a **Reinforcement Learning (RL)** approach, enabling the system to *learn from experience* and adapt its decisions over time.

---

## Why Reinforcement Learning?

Vertical handover is inherently a **sequential decision-making problem**:

* Decisions affect future network conditions
* Frequent handovers degrade performance
* Optimal choices depend on both current and past states

Q-learning is particularly suitable because:

* It does **not require a labeled dataset**
* It learns an **optimal policy through interaction**
* It naturally models the trade-off between exploration and exploitation

This makes the approach robust and extensible to real-world deployments.

---

## System Architecture

The project is composed of three main components:

```
┌────────────┐      ┌────────────┐      ┌──────────────┐
│  Dataset   │ ---> │  RL Env    │ ---> │ Q-Learning   │
│ (CSV)      │      │ (VHDAEnv)  │      │   Agent      │
└────────────┘      └────────────┘      └──────────────┘
                            │
                            ▼
                   Optimal RAT Selection
```

* **Environment (VHDAEnv)**: Simulates network conditions using a normalized dataset
* **Agent (QLearningVHDA)**: Learns a handover policy via Q-learning
* **Runner (run.py)**: Trains the agent and visualizes learning progress

---

## State, Action, and Reward Design

### State Space (8 dimensions)

* RSSI_WIFI
* SINR_WIFI
* RSRP_5G
* SINR_5G
* Throughput_WIFI
* Latency_WIFI
* Throughput_5G
* Latency_5G

### Action Space

* `0` → Select **WiFi**
* `1` → Select **5G**

### Reward Function

The reward function is designed to:

* Encourage high throughput
* Penalize high latency
* Penalize unnecessary handovers
* Reward QoS improvement after a handover

This ensures that the agent learns **stable and efficient handover behavior** rather than aggressive switching.

---

## Real-World Interpretation

Although training is performed **offline** using historical or synthetic data, the learned policy can be deployed **online**:

* **Training**: Offline (hundreds of episodes over dataset)
* **Inference**: Online (milliseconds per decision)

**Important:**

> An episode does not correspond to a real-world mobility session.

A trained policy can be deployed during continuous operation of mobile users in heterogeneous wireless networks.

---

## Project Structure

```
src/
 ├── agent.py        # Q-learning agent
 ├── env.py          # VHDA Gym environment
 ├── run.py          # Training & evaluation
Datasets/
 └── vhda_synthetic_normalized.csv
```

---

## Requirements

Minimal dependencies:

```
numpy
pandas
matplotlib
gym==0.26.2
```

Install via:

```
pip install -r requirements.txt
```

---

## Key Takeaway

This project demonstrates how **Reinforcement Learning can be effectively applied to vertical handover problems**, providing:

* Adaptive decision-making
* Reduced unnecessary handovers
* Improved QoS stability

It serves as a strong foundation for **future extensions**, including deep RL, real-time deployment, and multi-RAT scenarios.

**How to Run**

This project requires Python 3.9+ and is recommended to be executed inside a virtual environment to ensure correct dependency resolution.

**Clone the repository:**
```
git clone https://github.com/mertiodas/vhda.git
cd vhda
```

**(Recommended) Create and activate a virtual environment:**
```
python -m venv venv
```

**Windows**
```
venv\Scripts\activate
```

**Linux / macOS**
```
source venv/bin/activate
```

**Install required dependencies:**
```
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

Note: Using python -m pip ensures that dependencies are installed for the same Python interpreter used to run the project.

**Run the training script:**
```
python src/run.py
```

The script will automatically start training the Q-learning based VHDA agent and display performance plots after training.
