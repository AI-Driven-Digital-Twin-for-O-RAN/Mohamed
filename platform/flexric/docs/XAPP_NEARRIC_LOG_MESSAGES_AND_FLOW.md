# FlexRIC xApp & nearRT-RIC — Log Messages and Flow (Open RAN)

This document explains **every log line** you see when starting the xApp and nearRT-RIC, where it comes from in the codebase, and the **E2/Open RAN flow** behind it.

---

## Table of Contents

1. [Configuration & Paths](#1-configuration--paths)
2. [nearRT-RIC Startup Messages](#2-nearrt-ric-startup-messages)
3. [xApp Startup Messages](#3-xapp-startup-messages)
4. [Subscription Flow (KPM per E2 Node)](#4-subscription-flow-kpm-per-e2-node)
5. [Data Flow Diagram](#5-data-flow-diagram)
6. [Key File References](#6-key-file-references)
7. [Open RAN Concepts](#7-open-ran-concepts)

---

## 1. Configuration & Paths

### flexric.conf

Default path: **`/usr/local/etc/flexric/flexric.conf`**

Typical content:
```ini
[NEAR-RIC]
NEAR_RIC_IP = 127.0.0.1

[XAPP]
DB_DIR = /tmp/
```

| Key | Purpose |
|-----|---------|
| `NEAR_RIC_IP` | IP address the nearRT-RIC listens on for E2 agents and xApps. |
| `DB_DIR` | Directory where the xApp SQLite DB file is created. |

Override via CLI: `-c /path/to/flexric.conf`

---

### Shared Libraries (Service Models)

Default path: **`/usr/local/lib/flexric/`**

Contains `.so` files loaded at runtime:
- `librlc_sm.so` — RLC stats
- `libmac_sm.so` — MAC stats
- `libpdcp_sm.so` — PDCP stats
- `libgtp_sm.so` — GTP stats
- `libtc_sm.so` — TC (Traffic Control)
- `libslice_sm.so` — Slice stats
- `librc_sm.so` — RAN Control (E2SM-RC)
- `libkpm_sm.so` — KPM (E2SM-KPM)

Override via CLI: `-p /path/to/lib/flexric/`

---

### xApp DB File (`xapp_db_{timestamp}`)

**Format:** `{DB_DIR}xapp_db_{timestamp}`

Example: **`/tmp/xapp_db_1773461054079938`**

- **DB_DIR** comes from `[XAPP] DB_DIR` in `flexric.conf` (or default `/tmp/`).
- **timestamp** = `time_now_us()` (microseconds since epoch) **at xApp init** → one unique DB file per xApp run.

---

#### Role (تفصيل دور قاعدة البيانات)

| Aspect | Details |
|--------|---------|
| **Purpose** | Persist E2 indication data (MAC, RLC, PDCP, SLICE, GTP stats) in SQLite for offline analysis (ML/AI, debugging). |
| **When created** | At xApp startup, when `init_db_xapp()` is called in `init_e42_xapp()`. |
| **When written** | **On every RIC Indication** received for an active subscription. Flow: E2 → nearRT-RIC → xApp → `msg_handler_xapp.c` → `write_db_xapp()` → queue → DB worker thread → `write_db_sqlite3()` → INSERT into appropriate table. |
| **How to inspect** | `sqlite3 /tmp/xapp_db_1773461054079938` or DB Browser for SQLite. |

---

#### Tables (الجداول المحفوظة)

| Table | Source SM | Description |
|-------|-----------|-------------|
| **MAC_UE** | MAC_STATS_V0 | Per-UE MAC stats: frame, slot, dl/ul TBS, PRB, MCS, CQI, BLER, HARQ, SNR, etc. |
| **RLC_bearer** | RLC_STATS_V0 | Per-bearer RLC stats: tx/rx PDU/SDU counts, retransmissions, buffer occupancy. |
| **PDCP_bearer** | PDCP_STATS_V0 | Per-bearer PDCP stats: tx/rx PDU/SDU, SN, out-of-order, drop counts. |
| **SLICE** | SLICE_STATS_V0 | Slice config: sched_name, label, type, type_param0/1/2. |
| **UE_SLICE** | SLICE_STATS_V0 | UE-to-slice mapping: rnti, dl_id. |
| **GTP_NGUT** | GTP_STATS_V0 | GTP tunnel info: teidgnb, rnti, qfi, teidupf. |
| **KPM_MeasRecord** / **KPM_LabelInfo** | KPM_STATS_V3_0 | **Schema exists but write is NOT implemented** (code is commented out). |
| — | RAN_CTRL_STATS_V1_03 | **Not persisted**; prints `"RAN Control sqlite not implemented"`. |

Each row includes `tstamp`, `ngran_node`, `mcc`, `mnc`, `nb_id`, `cu_du_id` (E2 node identity) plus SM-specific columns.

---

#### Code Flow

| Step | File | Function |
|------|------|----------|
| 1. RIC indication received | `src/xApp/msg_handler_xapp.c` | `e2ap_handle_indication_xapp()` |
| 2. Push to DB queue | `src/xApp/db/db.c` | `write_db_xapp()` → `push_tsnq()` |
| 3. Worker consumes queue | `src/xApp/db/db.c` | `worker_thread()` → `write_db_gen()` |
| 4. Dispatch by type | `src/xApp/db/sqlite3/sqlite3_wrapper.c` | `write_db_sqlite3()` → `write_mac_stats()`, `write_rlc_stats()`, etc. |
| 5. Create tables | same | `init_db_sqlite3()` → `create_mac_ue_table()`, etc. |

---

## 2. nearRT-RIC Startup Messages

| Log Line | Meaning | Source File |
|----------|---------|-------------|
| `[UTIL]: Setting the config -c file to /usr/local/etc/flexric/flexric.conf` | Config file path used for RIC. | `src/util/conf_file.c` → `init_fr_args()` |
| `[UTIL]: Setting path -p for the shared libraries to /usr/local/lib/flexric/` | Path to SM `.so` libraries. | `src/util/conf_file.c` → `init_fr_args()` |
| `[NEAR-RIC]: nearRT-RIC IP Address = 127.0.0.1, PORT = 36421` | RIC binds on this IP:port for E2 agents (E2AP over SCTP). | `src/ric/near_ric.c` → `init_near_ric()` |
| `[NEAR-RIC]: Initializing` | RIC process start. | — |
| `[NEAR-RIC]: Loading SM ID = 143 with def = RLC_STATS_V0` | Loaded RLC service model. | `src/ric/plugin_ric.c` → `load_plugin_ric()` |
| `[NEAR-RIC]: Loading SM ID = 142 with def = MAC_STATS_V0` | Loaded MAC service model. | same |
| `[NEAR-RIC]: Loading SM ID = 144 with def = PDCP_STATS_V0` | Loaded PDCP service model. | same |
| `[NEAR-RIC]: Loading SM ID = 148 with def = GTP_STATS_V0` | Loaded GTP service model. | same |
| `[NEAR-RIC]: Loading SM ID = 146 with def = TC_STATS_V0` | Loaded TC service model. | same |
| `[NEAR-RIC]: Loading SM ID = 145 with def = SLICE_STATS_V0` | Loaded Slice service model. | same |
| `[NEAR-RIC]: Loading SM ID = 3 with def = ORAN-E2SM-RC` | Loaded O-RAN RAN Control (handover, etc.). | same |
| `[NEAR-RIC]: Loading SM ID = 2 with def = ORAN-E2SM-KPM` | Loaded O-RAN KPM (measurements). | same |
| `[iApp]: Initializing ...` | iApp (RIC internal xApp interface) starting. | — |
| `[iApp]: nearRT-RIC IP Address = 127.0.0.1, PORT = 36422` | iApp listens on 36422 for xApp connections. | `src/ric/iApp/e42_iapp.c` |
| `[NEAR-RIC]: Initializing Task Manager with 2 threads` | RIC thread pool ready. | `src/ric/near_ric.c` → `init_task_manager()` |

**Ports:**
- **36421** — E2 agents (RAN nodes, e.g. ns-3) connect here.
- **36422** — xApps connect here (E42 over SCTP).

---

## 3. xApp Startup Messages

| Log Line | Meaning | Source File |
|----------|---------|-------------|
| `[UTIL]: Setting the config -c file to ...` | Same config as RIC (reads NEAR_RIC_IP, DB_DIR). | `src/util/conf_file.c` |
| `[UTIL]: Setting path -p for the shared libraries to ...` | Same library path as RIC. | `src/util/conf_file.c` |
| `[xAapp]: Initializing ...` | xApp process start. *(Typo: "xAapp" in code)* | `src/xApp/e42_xapp.c` → `init_e42_xapp()` |
| `[xApp]: nearRT-RIC IP Address = 127.0.0.1, PORT = 36422` | xApp will connect to RIC at this address. | `src/xApp/e42_xapp.c` |
| `[E2 AGENT]: Opening plugin from path = .../librlc_sm.so` | xApp loads SM plugins (same set as RIC). | `src/xApp/plugin_agent.c` → `load_plugin_ag()` |
| *(similar for mac, pdcp, gtp, tc, slice, rc, kpm)* | Each SM plugin is `dlopen`ed. | same |
| `[NEAR-RIC]: Loading SM ID = ...` | xApp loads RIC-side SM definitions (for decoding). | `src/xApp/plugin_ric.c` |
| `[xApp]: DB filename = /tmp/xapp_db_1773461054079938` | SQLite DB path for this run. | `src/xApp/e42_xapp.c` |
| `[xApp]: E42 SETUP-REQUEST tx` | xApp sends E42 setup to RIC. | `src/xApp/msg_handler_xapp.c` → `e2ap_handle_e42_setup_request_xapp()` |
| `[xApp]: E42 SETUP-RESPONSE rx` | RIC accepted xApp. | `src/xApp/msg_handler_xapp.c` → `e2ap_handle_e42_setup_response_xapp()` |
| `[xApp]: xApp ID = 7` | RIC-assigned xApp ID (e.g. 7). | same |
| `[xApp]: Registered E2 Nodes = 8` | Number of E2 nodes (RAN cells) known by RIC. | same |

---

## 4. Subscription Flow (KPM per E2 Node)

After E42 setup, the xApp subscribes to **KPM (RAN_FUNC_ID 2)** for each E2 node.

| Log Line | Meaning | Source File |
|----------|---------|-------------|
| `[xApp]: E42 RIC SUBSCRIPTION REQUEST tx RAN_FUNC_ID 2 RIC_REQ_ID 1` | xApp sends subscription for KPM to node 0. | `src/xApp/msg_handler_xapp.c` |
| `[xApp]: SUBSCRIPTION RESPONSE rx` | RIC confirms subscription. | same |
| `[xApp]: Successfully subscribed to RAN_FUNC_ID 2` | xApp marks KPM subscription as active. | `src/xApp/e42_xapp.c` |
| `Subscribed KPM on node 0  NB_ID=939524096` | xApp log: node index 0, E2 node ID (NB_ID) 939524096. | `examples/xApp/c/orange/xapp_es_with_cell_util.c` |

**RIC_REQ_ID** — unique per subscription (1, 2, 3, …).
**NB_ID** — Node B ID from E2; maps to a gNB/cell in the ns-3 scenario.

This repeats for each E2 node (e.g. nodes 0–7 → 8 cells).

---

## 5. Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           FLEXRIC / OPEN RAN FLOW                            │
└─────────────────────────────────────────────────────────────────────────────┘

  ns-3 (E2 Agent)                    nearRT-RIC                      xApp
  ───────────────                   ───────────                     ────

  ┌─────────────┐                   ┌─────────────────┐            ┌─────────────┐
  │ E2 Agent    │  SCTP :36421      │ E2AP Server     │            │             │
  │ (gNBs)      │ ────────────────► │ - E2 SETUP      │            │             │
  │ 8 cells     │                   │ - RIC SUB REQ   │  SCTP      │  xApp       │
  │             │ ◄──────────────── │ - INDICATION    │ :36422     │  (HO, KPM)  │
  └─────────────┘   RIC SUB RESP    │ - RIC CONTROL   │ ◄─────────►│             │
        │                           └────────┬────────┘            └──────┬──────┘
        │                                    │                           │
        │ KPM (SINR, RSRP, PRB)              │ iApp                      │
        │ RIC Control (Handover)             │ E42 SETUP / SUB           │ SQLite
        ▼                                    ▼                           ▼
  ┌─────────────┐                   ┌─────────────────┐            ┌─────────────┐
  │ MAC/PHY     │                   │ plugin_ric.c    │            │ xapp_db_*   │
  │ scheduler   │                   │ - RLC, MAC,     │            │ /tmp/       │
  │ traces      │                   │   PDCP, GTP,    │            │             │
  └─────────────┘                   │   TC, SLICE,    │            └─────────────┘
                                    │   RC, KPM       │
                                    └─────────────────┘
```

**Sequence:**
1. **nearRT-RIC** starts, loads SMs, listens on 36421 (E2) and 36422 (xApp).
2. **ns-3** E2 agents connect to 36421, send E2 SETUP, receive RIC SUB REQ.
3. **xApp** connects to 36422, sends E42 SETUP, gets xApp ID and list of E2 nodes.
4. **xApp** sends RIC SUBSCRIPTION REQUEST (KPM) per E2 node.
5. **RIC** forwards subscriptions to E2 agents; agents send KPM indications periodically.
6. **RIC** forwards indications to xApp; xApp writes to DB and runs HO logic.

---

## 6. Key File References

| Component | File | Responsibility |
|-----------|------|----------------|
| Config parsing | `src/util/conf_file.c` | `flexric.conf`, NEAR_RIC_IP, DB_DIR |
| RIC init | `src/ric/near_ric.c` | Port 36421, plugin loading |
| RIC plugin load | `src/ric/plugin_ric.c` | `[NEAR-RIC]: Loading SM ID = …` |
| iApp | `src/ric/iApp/` | Port 36422, E42 SETUP, subscription routing |
| iApp SETUP handler | `src/ric/iApp/msg_handler_iapp.c` | `[iApp]: E42 SETUP-REQUEST rx/tx` |
| xApp init | `src/xApp/e42_xapp.c` | DB path, E42 connect |
| xApp plugin load | `src/xApp/plugin_agent.c` | `[E2 AGENT]: Opening plugin from path = …` |
| xApp plugin (RIC defs) | `src/xApp/plugin_ric.c` | `[NEAR-RIC]: Loading SM ID = …` |
| xApp msg handlers | `src/xApp/msg_handler_xapp.c` | E42 SETUP, SUB REQ/RESP |
| xApp DB | `src/xApp/db/db.c` | SQLite persistence |
| Config file | `/usr/local/etc/flexric/flexric.conf` | Or `-c` path |

---

## 7. Open RAN Concepts

| Term | Meaning |
|------|---------|
| **E2 (E2 Interface)** | Interface between nearRT-RIC and E2 nodes (gNB/CU/DU). |
| **E2AP** | E2 Application Protocol (E2 SETUP, RIC SUBSCRIPTION, INDICATION, CONTROL). |
| **E42** | Protocol between xApps and RIC (E42 SETUP, RIC SUBSCRIPTION over SCTP). |
| **Service Model (SM)** | Defines data/actions for a function (e.g. KPM = measurements, RC = control). |
| **E2SM-KPM** | O-RAN Key Performance Measurements (SINR, RSRP, PRB, throughput, etc.). |
| **E2SM-RC** | O-RAN RAN Control (e.g. handover commands). |
| **RAN Function ID** | 2 = KPM, 3 = RC; others for custom SMs (RLC, MAC, etc.). |
| **RIC_REQ_ID** | Subscription request identifier. |
| **NB_ID (Node B ID)** | Unique E2 node identifier (maps to cell in ns-3). |

---

*Generated for FlexRIC ns-O-RAN-flexric integration. Last updated: 2025.*
