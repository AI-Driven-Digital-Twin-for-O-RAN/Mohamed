import numpy as np
import pandas as pd
import gymnasium as gym
from gymnasium import spaces
import os

class VHDAEnv(gym.Env):
    def __init__(self,
                 csv_path=None,
                 handover_penalty=0.5,
                 max_steps=500,
                 mobility_group=None,
                 window_size=5):

        super(VHDAEnv, self).__init__()

        if csv_path is None:
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            csv_path = os.path.join(base_dir, "Datasets", "processed_dataset.csv")

        df = pd.read_csv(csv_path)

        if mobility_group is not None:
            df = df[df['Mobility'] == mobility_group].reset_index(drop=True)

        features = ['Level', 'SNR', 'CQI', 'DL_bitrate',
                    'SecondCell_RSRP', 'SecondCell_SNR', 'Speed', 'NRxLev1']

        df[features] = df[features].fillna(df[features].mean())
        df[features] = (df[features] - df[features].min()) / (df[features].max() - df[features].min())

        self.data = df[features].values[:max_steps]
        self.mobility_group = mobility_group
        self.window_size = window_size
        self.n_features = 8

        self.current_step = 0
        self.last_action = None
        self.prev_action = None
        self.handover_penalty = handover_penalty
        self.last_signal = 0
        self.steps_since_last_ho = 0

        self.observation_space = spaces.Box(
            low=0, high=1,
            shape=(self.n_features * self.window_size,),
            dtype=np.float32
        )
        self.action_space = spaces.Discrete(2)

        self.unnecessary_handover_count = 0
        self.correct_handover_count = 0
        self.ping_pong_count = 0
        self.signal_history = []

    def get_observation(self):
        start = max(0, self.current_step - self.window_size)
        window = self.data[start:self.current_step]
        if len(window) < self.window_size:
            padding = np.zeros((self.window_size - len(window), self.n_features))
            window = np.vstack([padding, window])
        return window.flatten().astype(np.float32)

    def reset(self, seed=None, options=None):
        self.current_step = 0
        self.last_action = None
        self.prev_action = None
        self.last_signal = 0
        self.steps_since_last_ho = 0
        self.unnecessary_handover_count = 0
        self.correct_handover_count = 0
        self.ping_pong_count = 0
        self.signal_history = []
        self.current_step = 1
        return self.get_observation(), {}

    def step(self, action):
        row = self.data[self.current_step - 1]

        current_signal = row[0]
        neighbor_signal = row[4]
        speed = row[6]

        reward = 0
        switched = self.last_action is not None and self.last_action != action

        if switched:
            is_ping_pong = (
                self.prev_action is not None and
                action == self.prev_action and
                self.steps_since_last_ho < 3
            )

            if is_ping_pong:
                reward -= self.handover_penalty * 3
                self.ping_pong_count += 1
                self.unnecessary_handover_count += 1

            elif neighbor_signal > current_signal:
                reward += (neighbor_signal - current_signal) * 2
                self.correct_handover_count += 1

            else:
                if speed < 0.1:
                    reward -= self.handover_penalty * 2
                else:
                    reward -= self.handover_penalty
                self.unnecessary_handover_count += 1

            self.prev_action = self.last_action
            self.steps_since_last_ho = 0

        else:
            reward += current_signal * 0.2
            self.steps_since_last_ho += 1

        self.last_signal = current_signal
        self.last_action = action
        self.signal_history.append(current_signal)
        self.current_step += 1

        done = self.current_step > len(self.data)
        obs = self.get_observation() if not done else np.zeros(self.n_features * self.window_size, dtype=np.float32)

        return obs, reward, done, False, {}

    def render(self):
        pass

    def get_qos_stats(self):
        total_ho = self.correct_handover_count + self.unnecessary_handover_count
        pp_rate = (self.ping_pong_count / total_ho * 100) if total_ho > 0 else 0
        return {
            "Correct Handovers": self.correct_handover_count,
            "Unnecessary Handovers": self.unnecessary_handover_count,
            "Ping Pong Count": self.ping_pong_count,
            "Ping Pong Rate": pp_rate,
            "Average Signal": np.mean(self.signal_history),
            "QoS History": self.signal_history
        }
