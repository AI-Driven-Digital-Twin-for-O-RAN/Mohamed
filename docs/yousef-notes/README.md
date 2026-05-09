# 🚀 O-RAN Digital Twin - Complete Installation Guide
![image URL](https://github.com/AI-Driven-Digital-Twin-for-O-RAN/yousef_fathy/blob/main/preview.png?raw=true)

<div align="center">

![O-RAN](https://img.shields.io/badge/O--RAN-Alliance-blue?style=for-the-badge)
![FlexRIC](https://img.shields.io/badge/FlexRIC-v1.0-green?style=for-the-badge)
![ns-3](https://img.shields.io/badge/ns--3-Simulator-orange?style=for-the-badge)
![License](https://img.shields.io/badge/License-Apache%202.0-red?style=for-the-badge)

**AI-Driven Digital Twin for Open Radio Access Network (O-RAN)**

[🔗 Repository](#) • [📖 Documentation](#documentation) • [🐛 Report Bug](#troubleshooting) • [✨ Request Feature](#)

</div>

---

## 📑 Table of Contents

- [🎯 Overview](#-overview)
- [✨ Key Features](#-key-features)
- [⚙️ System Requirements](#️-system-requirements)
- [📦 Installation](#-installation)
  - [Step 1: Clone Repository](#step-1-clone-the-repository)
  - [Step 2: Install Dependencies](#step-2-install-system-dependencies)
  - [Step 3: Build FlexRIC](#step-3-build-flexric)
  - [Step 4: Build ns-O-RAN](#step-4-build-ns-o-ran-and-e2-simulator)
  - [Step 5: GUI Setup (Optional)](#step-5-gui-deployment-optional)
- [🚀 Quick Start](#-quick-start)
- [📊 Running Scenarios](#-running-scenarios)
- [🔧 Troubleshooting](#-troubleshooting)
- [📁 Project Structure](#-project-structure)
- [📚 Documentation](#-documentation)
- [🤝 Contributing](#-contributing)
- [📄 License](#-license)

---

## 🎯 Overview

This project implements an **AI-Driven Digital Twin for Open Radio Access Network (O-RAN)**, providing a complete simulation environment for 5G networks with intelligent control capabilities.

### 🏗️ Architecture

```
┌─────────────────┐         E2 Interface        ┌─────────────────┐
│   nearRT-RIC    │◄──────────────────────────►│   ns-3 + E2Sim  │
│   (FlexRIC)     │                              │                 │
└────────┬────────┘                              └─────────────────┘
         │                                                ▲
         │ Control                                        │
         ▼                                                │ Reports
┌─────────────────┐                              ┌───────┴─────────┐
│  xApps          │                              │  RAN Nodes      │
│  • KPM Monitor  │                              │  • gNB          │
│  • RC Control   │                              │  • UEs          │
└─────────────────┘                              └─────────────────┘
```

### 🧩 Components

| Component | Description |
|-----------|-------------|
| **FlexRIC** | Near Real-Time RAN Intelligent Controller (RIC) implementing O-RAN E2 interface |
| **ns-O-RAN** | Network Simulator 3 (ns-3) with O-RAN extensions for realistic 5G simulation |
| **E2 Simulator** | E2AP v1.01 protocol implementation for RIC-RAN communication |
| **xApps** | Control and monitoring applications (KPM v3.00, RC for handover) |
| **SWIG** | Simplified Wrapper and Interface Generator for Python/C++ integration |
| **GUI Dashboard** | Web-based monitoring and control interface with InfluxDB backend |

---

## ✨ Key Features

<table>
<tr>
<td width="50%">

### 📊 Monitoring
- ✅ Real-time KPI collection
- ✅ Performance metrics visualization
- ✅ Network state monitoring
- ✅ InfluxDB integration
- ✅ Grafana dashboards

</td>
<td width="50%">

### 🎮 Control
- ✅ Handover control
- ✅ Resource allocation
- ✅ Mobility management
- ✅ QoS optimization
- ✅ AI-driven decisions

</td>
</tr>
</table>

---

## ⚙️ System Requirements

### 💻 Hardware Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **CPU** | Intel Core i5 (4 cores) | Intel Core i7 (8+ cores) |
| **RAM** | 8 GB | 16 GB |
| **Storage** | 20 GB free | 50 GB free |
| **Network** | Internet connection | High-speed broadband |

### 🖥️ Software Requirements

```bash
OS:       Ubuntu 20.04 LTS (recommended) or 22.04 LTS
Compiler: GCC 9+ or Clang 10+
Python:   Python 3.8+
CMake:    Version 3.16+
Git:      Latest version
```

> **⚠️ Important:** Ubuntu 20.04 LTS is the officially tested and recommended distribution.

---

## 📦 Installation

### Step 1: Clone the Repository

```bash
git clone git@github.com:AI-Driven-Digital-Twin-for-O-RAN/yousef_fathy.git
cd yousef_fathy
```

<details>
<summary>📂 <b>Expected Directory Structure</b></summary>

```
yousef_fathy/
├── flexric/              # RAN Intelligent Controller
├── ns-O-RAN-flexric/     # Network Simulator with O-RAN
├── swig/                 # Interface generator
└── README.md             # This file
```

</details>

---

### Step 2: Install System Dependencies

<details>
<summary>🔽 <b>Click to view installation commands</b></summary>

#### Update Package List
```bash
sudo apt-get update
```

#### Install E2Sim Requirements
```bash
sudo apt-get install -y \
  build-essential \
  git \
  cmake \
  libsctp-dev \
  autoconf \
  automake \
  libtool \
  bison \
  flex \
  libboost-all-dev
```

#### Install ns-3 Requirements
```bash
sudo apt-get install -y \
  g++ \
  python3 \
  python3-pip \
  libc6-dev
```

#### Install Optional Dependencies
```bash
# SQLite (for LENA comparison examples)
sudo apt-get install -y sqlite sqlite3 libsqlite3-dev

# Eigen3 (for MIMO features)
sudo apt-get install -y libeigen3-dev

# Python packages for GUI
pip3 install influxdb matplotlib numpy
```

</details>

> ✅ **Verification:** Run `cmake --version` and `gcc --version` to verify installations.

---

### Step 3: Build FlexRIC

FlexRIC is configured for **E2AP v1.01** and **KPM v3.00** to ensure compatibility with ns-O-RAN.

```bash
cd flexric

# Create build directory
mkdir build && cd build

# Configure with specific E2AP and KPM versions
cmake .. -DE2AP_VERSION=E2AP_V1 -DKPM_VERSION=KPM_V3_00

# Build (adjust -j8 based on your CPU cores)
make -j8

# Install Service Models
sudo make install

# Return to project root
cd ../..
```

<details>
<summary>📊 <b>Build Output Verification</b></summary>

You should see:
```
[100%] Built target flexric
Install the project...
-- Install configuration: "Release"
-- Installing: /usr/local/lib/libflexric.so
```

</details>

> ⏱️ **Expected Time:** 10-15 minutes depending on your system.

---

### Step 4: Build ns-O-RAN and E2 Simulator

#### 4.1 Build E2 Simulator

```bash
cd ns-O-RAN-flexric/e2sim-kpmv3/e2sim

# Create build directory
mkdir build

# Build with log level 2 (INFO)
sudo ./build_e2sim.sh 2

# Return to ns-O-RAN directory
cd ../..
cd mmwave-LENA-oran/
```

<details>
<summary>ℹ️ <b>Available Log Levels</b></summary>

| Level | Value | Description |
|-------|-------|-------------|
| `LOG_LEVEL_UNCOND` | 0 | Only unconditional logs |
| `LOG_LEVEL_ERROR` | 1 | Errors only |
| `LOG_LEVEL_INFO` | 2 | Info messages (recommended) |
| `LOG_LEVEL_DEBUG` | 3 | Full debug with ASN.1 printing |

</details>

#### 4.2 Build ns-3 Simulator

```bash
# Configure ns-3
./ns3 configure

# Build ns-3 (this may take 15-30 minutes)
./ns3 build
```

> ⚠️ **Note:** The ns-3 build process is resource-intensive. Ensure sufficient RAM is available.

---

### Step 5: GUI Deployment (Optional)

The GUI provides a web-based dashboard for real-time monitoring and control.

```bash
cd GUI

# Edit docker-compose.yml to set your machine's IP
nano docker-compose.yml
# Change: NS3_HOST=192.168.1.100 (replace with your actual IP)
# follow the steps in this link
- https://github.com/Orange-OpenSource/ns-O-RAN-flexric/issues/49

# Deploy GUI and InfluxDB using Docker Compose
docker-compose up --build -d

# Install Python InfluxDB client
pip3 install influxdb

# Return to project root
cd ../../..
```

**Access the GUI:**
- Local: http://127.0.0.1:8000
- Remote: http://YOUR_IP:8000

> 🐳 **Docker Required:** Install Docker and Docker Compose from [docker.com](https://docs.docker.com/compose/install/)

---

## 🚀 Quick Start

### Running Your First Simulation

#### 🖥️ Terminal 1: Start the RIC

```bash
cd flexric/build/examples/ric/
./nearRT-RIC
```

**Expected Output:**
```
Starting nearRT-RIC...
Listening on port 36421
```

---

#### 🖥️ Terminal 2: Start ns-3 Simulation (Scenario Zero)

```bash
cd ns-O-RAN-flexric/mmwave-LENA-oran/

./ns3 run "scratch/scenario-zero-with_parallel_loging.cc \
  --e2TermIp=127.0.0.1 \
  --hoSinrDifference=3 \
  --indicationPeriodicity=0.1 \
  --simTime=1000 \
  --KPM_E2functionID=2 \
  --RC_E2functionID=3 \
  --N_MmWaveEnbNodes=4 \
  --N_Ues=3 \
  --CenterFrequency=3.5e9 \
  --Bandwidth=20e6"
```

<details>
<summary>⚙️ <b>Scenario Parameters Explained</b></summary>

| Parameter | Description | Value |
|-----------|-------------|-------|
| `e2TermIp` | RIC IP address | `127.0.0.1` |
| `hoSinrDifference` | Handover SINR threshold (dB) | `3` |
| `indicationPeriodicity` | KPI report interval (seconds) | `0.1` |
| `simTime` | Simulation duration (seconds) | `1000` |
| `N_MmWaveEnbNodes` | Number of gNBs | `4` |
| `N_Ues` | Number of UEs | `3` |
| `CenterFrequency` | Carrier frequency (Hz) | `3.5 GHz` |
| `Bandwidth` | Channel bandwidth (Hz) | `20 MHz` |

</details>

---

#### 🖥️ Terminal 3: Run KPM xApp (Monitoring)

```bash
cd flexric/build/examples/xApp/c/kpm_rc/
./xapp_kpm_rc
```

**Expected Output:**
```
Connected to nearRT-RIC
Subscribed to KPM Service Model
Receiving KPI reports...
Name = DRB.UEThpDl, value = 125000
Name = DRB.UEThpUl, value = 98000
```




### Scenario Three: Handover Testing

**Description:** Advanced scenario for testing handover algorithms.

**Run Command:**
#### 🖥️ Terminal 1: Start the RIC

```bash
cd flexric/build/examples/ric/
./nearRT-RIC
```
#### 🖥️ Terminal 2 : Run Handover Scenario
```bash
cd ns-O-RAN-flexric
./ns3 run scratch/scenario-three.cc
```

---
---

#### 🖥️ Terminal 3 (Optional): Run Handover Control xApp

```bash
cd flexric/build/examples/xApp/c/ctrl/
./xapp_rc_handover_ctrl
```

> ✅ **Success Indicator:** You should see E2 messages being exchanged and KPIs being reported in all terminals.

---


---

## 🔧 Troubleshooting

### Common Issues and Solutions

<details>
<summary>❌ <b>Issue: CMake version too old</b></summary>

**Error:** `CMake 3.16 or higher is required`

**Solution:**
```bash
# Remove old CMake
sudo apt remove cmake

# Install newer version via snap
sudo snap install cmake --classic

# Verify
cmake --version
```

</details>

<details>
<summary>❌ <b>Issue: Missing SCTP library</b></summary>

**Error:** `libsctp-dev: not found`

**Solution:**
```bash
sudo apt-get update
sudo apt-get install -y libsctp-dev
```

</details>

<details>
<summary>❌ <b>Issue: Port 36421 already in use</b></summary>

**Error:** `bind: Address already in use`

**Solution:**
```bash
# Find process using port 36421
sudo lsof -i :36421

# Kill the process (replace <PID> with actual process ID)
sudo kill -9 <PID>

# Or restart your system
sudo reboot
```

</details>

<details>
<summary>❌ <b>Issue: E2 Setup fails</b></summary>

**Symptoms:** RIC doesn't show "E2 SETUP-REQUEST received"

**Solution:**
1. Verify RIC is running: `ps aux | grep nearRT-RIC`
2. Check RIC logs for errors
3. Ensure `e2TermIp` matches RIC IP (use `127.0.0.1` for local)
4. Verify firewall allows port 36421: `sudo ufw allow 36421`

</details>

<details>
<summary>❌ <b>Issue: ns-3 build fails</b></summary>

**Error:** Various compilation errors

**Solution:**
```bash
# Clean build
cd ns-O-RAN-flexric
./ns3 clean

# Reconfigure and rebuild
./ns3 configure --enable-examples --enable-tests
./ns3 build

# If still failing, check GCC version
gcc --version  # Should be 9 or higher
```

</details>

<details>
<summary>❌ <b>Issue: Python dependencies missing</b></summary>

**Error:** `ModuleNotFoundError: No module named 'influxdb'`

**Solution:**
```bash
pip3 install --upgrade pip
pip3 install influxdb matplotlib numpy pandas
```

</details>

---

### Getting Help

If you encounter issues not covered here:

1. 🐛 Check [GitHub Issues](https://github.com/AI-Driven-Digital-Twin-for-O-RAN/yousef_fathy/issues)
2. 📖 Review [FlexRIC Documentation](https://gitlab.eurecom.fr/mosaic5g/flexric)
3. 💬 Contact the team or open a new issue
4. 📧 Email: [youssefathy1@gmail.com]

---

## 📁 Project Structure (❗️that's not correct now, i will fix it later )

```
yousef_fathy/
│
├── flexric/                          # RAN Intelligent Controller
│   ├── src/                         # FlexRIC source code
│   │   ├── xApp/                   # xApp framework
│   │   ├── sm/                     # Service Models (KPM, RC)
│   │   └── ric/                    # RIC implementation
│   ├── examples/                    # Example applications
│   │   ├── ric/                    # nearRT-RIC executable
│   │   └── xApp/                   # xApp examples
│   │       ├── c/                  # C-based xApps
│   │       │   ├── kpm_rc/        # KPM+RC combined xApp
│   │       │   └── ctrl/          # Control xApps (handover)
│   │       └── python/            # Python-based xApps
│   └── build/                       # Build artifacts (created during build)
│
├── ns-O-RAN-flexric/                # Network Simulator with O-RAN
│   ├── scratch/                     # Simulation scenarios
│   │   ├── scenario-zero-with_parallel_loging.cc
│   │   ├── scenario-three.cc
│   │   └── [custom scenarios]
│   ├── src/                         # ns-3 modules
│   │   ├── lte/                    # LTE module
│   │   ├── mmwave/                 # mmWave/5G NR module
│   │   └── oran/                   # O-RAN E2 interface
│   ├── e2sim/                       # E2 Simulator
│   │   ├── src/                    # E2Sim source
│   │   ├── build/                  # Build directory (created)
│   │   └── build_e2sim.sh         # Build script
│   └── ns3                          # ns-3 build tool
│
├── swig/                            # Interface Generator
│   ├── Source/                     # SWIG source code
│   └── Lib/                        # SWIG library files
│
├── GUI/                             # Web Dashboard (optional)
│   ├── docker-compose.yml          # Docker configuration
│   ├── Dockerfile                  # Container definition
│   └── app/                        # GUI application
│
└── README.md                        # This file
```

---

## 📚 Documentation

### Official Resources

- 📘 [O-RAN Alliance Specifications](https://www.o-ran.org/)
- 📗 [FlexRIC GitLab](https://gitlab.eurecom.fr/mosaic5g/flexric)
- 📙 [ns-3 Documentation](https://www.nsnam.org/)
- 📕 [E2 Interface Specifications](https://www.o-ran.org/) (O-RAN WG3)


---

## 🛠️ Advanced Configuration

### Custom xApp Development

To develop your own xApps:

1. Navigate to `flexric/examples/xApp/c/`
2. Copy an existing xApp as a template
3. Modify according to your requirements
4. Rebuild FlexRIC: `cd flexric/build && make && sudo make install`

**Example xApp structure:**
```c
#include "e42_xapp_api.h"

void callback(e2ap_indication_t* ind) {
    // Process indication message
    // Your custom logic here
}

int main() {
    init_xapp_api(&args);
    subscribe_sm_xapp_api(&node_id, SM_KPM_ID, &sub_req, callback);
    // Keep running...
}
```

---

### Performance Tuning

#### Optimize ns-3 Build
```bash
cd ns-O-RAN-flexric

# Use all available cores
./ns3 build -j$(nproc)

# Enable compiler optimizations
CXXFLAGS="-O3 -march=native" ./ns3 configure
./ns3 build
```

#### Adjust KPI Reporting Frequency
Edit scenario parameters:
```bash
--indicationPeriodicity=0.5  # Report every 500ms (less frequent)
--indicationPeriodicity=0.01 # Report every 10ms (more frequent)
```

---

## 🚀 Quick Reference

### Essential Commands

| Task | Command |
|------|---------|
| **Start RIC** | `cd flexric/build/examples/ric/ && ./nearRT-RIC` |
| **Run Scenario Zero** | `cd ns-O-RAN-flexric && ./ns3 run scratch/scenario-zero-with_parallel_loging.cc` |
| **Run KPM xApp** | `cd flexric/build/examples/xApp/c/kpm_rc/ && ./xapp_kpm_rc` |
| **Run Handover xApp** | `cd flexric/build/examples/xApp/c/ctrl/ && ./xapp_rc_handover_ctrl` |
| **Start GUI** | `cd GUI && docker-compose up -d` |
| **Stop GUI** | `cd GUI && docker-compose down` |
| **Rebuild FlexRIC** | `cd flexric/build && make -j8 && sudo make install` |
| **Rebuild ns-3** | `cd ns-O-RAN-flexric && ./ns3 build` |
| **Clean ns-3** | `cd ns-O-RAN-flexric && ./ns3 clean` |

### Port Reference

| Service | Port | Protocol |
|---------|------|----------|
| nearRT-RIC E2 Interface | 36421 | SCTP |
| GUI Web Interface | 8000 | HTTP |
| InfluxDB | 8086 | HTTP |
| Grafana (if used) | 3000 | HTTP |

---

## 🤝 Contributing

We welcome contributions from the community! Here's how you can help:

### How to Contribute

1. **Fork the repository**
2. **Create a feature branch**
   ```bash
   git checkout -b feature/amazing-feature
   ```
3. **Commit your changes**
   ```bash
   git commit -m 'Add some amazing feature'
   ```
4. **Push to the branch**
   ```bash
   git push origin feature/amazing-feature
   ```
5. **Open a Pull Request**

### Contribution Guidelines

- Follow existing code style and conventions
- Add tests for new features
- Update documentation as needed
- Ensure all tests pass before submitting PR

---

## 📄 License

This project is licensed under the **Apache License 2.0** - see the [LICENSE](LICENSE) file for details.

---

## 👥 Authors

**Yousef Fathy**
- GitHub: [@Yousef-Fathy-1](https://github.com/Youssef-Fathy-1)
- Email: [youssefathy1@gmail.com]

**Alyaa Mohamed**
- GitHub: [@alyaamohamed251](https://github.com/alyaamohamed251)
- Email: [alyaamohammed251@gmai.com]


**AI-Driven Digital Twin for O-RAN Team**
- Organization: [AI-Driven-Digital-Twin-for-O-RAN](https://github.com/AI-Driven-Digital-Twin-for-O-RAN)

---

## 🙏 Acknowledgments

- [O-RAN Alliance](https://www.o-ran.org/) for O-RAN specifications
- [EURECOM](https://www.eurecom.fr/) for FlexRIC development
- [ns-3 Project](https://www.nsnam.org/) for network simulation framework
- All contributors and community members

---

## 📮 Contact & Support

<div align="center">

**Questions? Issues? Suggestions?**

[![GitHub Issues](https://img.shields.io/badge/GitHub-Issues-red?style=for-the-badge&logo=github)](https://github.com/AI-Driven-Digital-Twin-for-O-RAN/yousef_fathy/issues)
[![Email](https://img.shields.io/badge/Email-Contact-blue?style=for-the-badge&logo=gmail)](mailto:youssefathy1@gmail.com)

</div>

---

<div align="center">



**Made with ❤️ by the O-RAN Team**

[⬆ Back to Top](#-o-ran-digital-twin---complete-installation-guide)

</div>

