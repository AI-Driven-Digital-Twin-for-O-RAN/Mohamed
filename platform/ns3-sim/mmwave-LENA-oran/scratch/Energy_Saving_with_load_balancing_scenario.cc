/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 Orange Innovation Poland
 * Copyright (c) 2024 Orange Innovation Egypt
 *
 * Load Balancing xApp Scenario — Dense Urban (Sunway City–style)
 * ═══════════════════════════════════════════════════════════════
 * Topology   : 1 LTE eNB (control, Cell 1) + 7 mmWave gNBs (Cells 2–8)
 *              Cell 2 = center (1000,1000) — INTENTIONALLY OVERLOADED (8 UEs)
 *              Cells 3–8 = hexagonal ring at ISD=300 m (2 UEs each)
 * UEs        : 20 total — mixed mobility (pedestrian / fast-walker / vehicle)
 *              mixed services (eMBB/FTP, Video, VoIP, HTTP) → realistic PRB spread
 * Metrics    : 5 KPIs logged to ~/lb_metrics.csv every indicationPeriodicity
 *              1. avg_prb_load     — PRB-based cell load [0..1]
 *              2. avg_ue_load      — #UE / capacity (UE-count load index)
 *              3. load_variance    — variance of PRB load across active cells
 *              4. avg_throughput   — mean DL+UL bitrate per UE [Mbps]
 *              5. cdr              — call-drop rate (RLF events / attached UEs)
 * E2/RIC     : KPM_E2functionID=2, RC_E2functionID=3; handoverMode=NoAuto
 *              indicationPeriodicity=0.1 s; connects to RIC at e2TermIp
 * Outputs    : ~/kpm_handover_features.csv  (18-feature GRU dataset)
 *              ~/lstm_features.csv           (16-feature LSTM dataset)
 *              ~/handover.csv                (START|SUCCESS|FAILURE log)
 *              ~/lb_metrics.csv              (5 load-balancing KPIs)
 *              ue_position.txt / gnbs.txt / enbs.txt  (GUI visualizer)
 *
 * Authors    : (original) Andrea Lacava, Michele Polese, Argha Sen,
 *              Kamil Kociszewski, Mostafa Ashraf
 *              (LB extension) Orange Innovation Egypt team
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include <ns3/lte-ue-net-device.h>
#include "ns3/mmwave-helper.h"
#include "ns3/epc-helper.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "../src/mmwave/model/node-container-manager.h"
#include "ns3/lte-helper.h"
#include <sys/time.h>
#include <ctime>
#include <sys/types.h>
#include <iostream>
#include <stdlib.h>
#include <list>
#include <vector>
#include <random>
#include <chrono>
#include <cmath>
#include <fstream>
#include "ns3/basic-energy-source-helper.h"
#include "ns3/mmwave-radio-energy-model-enb-helper.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/mmwave-enb-net-device.h"
#include "../src/mmwave/model/mmwave-enb-mac.h"
#include "ns3/mobility-model.h"
#include "ns3/packet-sink.h"
#include "ns3/udp-client.h"
#include "ns3/bulk-send-application.h"
#include "ns3/onoff-application.h"
#include <deque>
#include <unistd.h>
#include <cstdlib>

using namespace ns3;
using namespace mmwave;

// ╔══════════════════════════════════════════════════════════════════╗
// ║              SCENARIO CONSTANTS — DO NOT CHANGE                  ║
// ╚══════════════════════════════════════════════════════════════════╝

static const uint32_t NUM_UES          = 20;
static const uint8_t  NUM_MMWAVE_ENBS  = 7;   // Cells 2–8
static const uint8_t  NUM_LTE_ENBS     = 1;   // Cell  1 (control plane)
static const double   AREA_X           = 2000.0;
static const double   AREA_Y           = 2000.0;
static const double   ISD_CELLS        = 300.0;  // inter-site distance [m]
static const int      SCENARIO_TOTAL_PRB = 28;   // PRBs per cell (20 MHz mmWave)
static const double   FC_GHZ           = 3.5;    // carrier frequency [GHz]
static const double   TX_POWER_DBM     = 30.0;   // gNB Tx power [dBm]
static const double   N_RBS_20MHZ      = 28.0;   // actual allocated RBs (28 × 60 kHz, BW=20 MHz NR N2)
static const double   RSRQ_MIN_DB      = -19.5;
static const double   RSRQ_MAX_DB      = -3.0;

// ── Bounded mobility box ─────────────────────────────────────────────
static const double BOUND_MIN          = 500.0;  // [m]
static const double BOUND_MAX          = 1500.0;
static const double BOUNDARY_CHECK_S   = 0.05;   // boundary enforcement interval [s]
static const double DIR_CHANGE_MIN_S   = 5.0;    // random direction change interval [s]
static const double DIR_CHANGE_MAX_S   = 12.0;
static const double REFLECT_PERTURB_DEG = 25.0;  // ±25° random tilt on reflection

// ══════════════════════════════════════════════════════════════════════
// UE SPEED TABLE  — realistic mixed-mobility model
// ──────────────────────────────────────────────────────────────────────
//  Class          Speed (m/s)   km/h   Mobility type
//  ──────────────────────────────────────────────────
//  Pedestrian     1.2–1.5       4–5    indoor/outdoor walk
//  Fast walker    2.0–2.8       7–10   busy street
//  Cyclist        4.5–6.0       16–22  cycle lane
//  Car (local)    8.3–11.1      30–40  urban low
//  Car (arterial) 13.9–16.7     50–60  main road
// ══════════════════════════════════════════════════════════════════════
//
// Cell 2 (overloaded): 8 UEs — mix of pedestrian + car (realistic urban hot-spot)
// Cells 3–8 (light): 2 UEs each — varied mobility classes
//
// UE index  0..7  → Cell 2 (8 UEs)
// UE index  8..9  → Cell 5 (2 UEs)
// UE index 10..11 → Cell 6 (2 UEs)
// UE index 12..13 → Cell 7 (2 UEs)
// UE index 14..15 → Cell 8 (2 UEs)
// UE index 16..17 → Cell 3 (2 UEs)
// UE index 18..19 → Cell 4 (2 UEs)

static const double g_ue_speed_mps[NUM_UES] = {
    // ── Cell 2 (UEs 1–8, index 0–7) ── overloaded hot-spot
    1.4,    // UE  1 — pedestrian (shopping area)
    11.2,    // UE  2 — pedestrian (slow)
    12.5,    // UE  3 — fast walker
    12.8,    // UE  4 — fast walker (commuter)
    8.3,    // UE  5 — car (local road, 30 km/h)
    11.1,   // UE  6 — car (40 km/h)
    13.9,   // UE  7 — car (arterial, 50 km/h)
    4.5,    // UE  8 — cyclist
    // ── Cell 5 (UEs 9–10, index 8–9) ──
    1.3,    // UE  9 — pedestrian
    2.0,   // UE 10 — car (60 km/h)
    // ── Cell 6 (UEs 11–12, index 10–11) ──
    1.5,    // UE 11 — fast walker
    6.0,   // UE 12 — car (50 km/h)
    // ── Cell 7 (UEs 13–14, index 12–13) ──
    10.2,    // UE 13 — pedestrian                                     //1.2    2.5from 14 to end
    2.5,   // UE 14 — car (40 km/h)
    // ── Cell 8 (UEs 15–16, index 14–15) ──
    3.9,    // UE 15 — cyclist (fast)
    10.5,    // UE 16 — car (30 km/h)
    // ── Cell 3 (UEs 17–18, index 16–17) ──
    8.5,    // UE 17 — pedestrian
    6.5,   // UE 18 — car (50 km/h)
    // ── Cell 4 (UEs 19–20, index 18–19) ──
    9.5,    // UE 19 — fast walker
    12.6,   // UE 20 — car (60 km/h)
};

// ══════════════════════════════════════════════════════════════════════
// SERVICE TYPE per UE — drives PRB allocation differences
// ──────────────────────────────────────────────────────────────────────
// EMBB_FTP   → BulkSend TCP, high & continuous PRB demand
// VIDEO      → UDP CBR, medium-high steady PRB
// VOIP       → UDP small packets, low PRB (good for demonstrating PRB vs UE-count gap)
// HTTP       → TCP OnOff, bursty PRB
// ══════════════════════════════════════════════════════════════════════
enum ServiceType { EMBB_FTP=0, VIDEO=1, VOIP=2, HTTP=3 };

static const ServiceType g_ue_service[NUM_UES] = {
    // Cell 2 (8 UEs) — diverse services to show PRB spread
    EMBB_FTP,  // UE 1  — high PRB consumer
    EMBB_FTP,  // UE 2  — high PRB consumer
    VIDEO,     // UE 3  — medium-high CBR
    VIDEO,     // UE 4  — medium-high CBR
    HTTP,      // UE 5  — bursty
    HTTP,      // UE 6  — bursty
    VOIP,      // UE 7  — LOW PRB (pedestrian with VoIP — shows PRB ≠ UE-count)
    VOIP,      // UE 8  — LOW PRB (cyclist with VoIP)
    // Cell 5
    VIDEO,     // UE 9
    EMBB_FTP,  // UE 10
    // Cell 6
    VOIP,      // UE 11
    HTTP,      // UE 12
    // Cell 7
    VIDEO,     // UE 13
    EMBB_FTP,  // UE 14
    // Cell 8
    HTTP,      // UE 15
    VIDEO,     // UE 16
    // Cell 3
    EMBB_FTP,  // UE 17
    VOIP,      // UE 18
    // Cell 4
    HTTP,      // UE 19
    VIDEO,     // UE 20
};

// ══════════════════════════════════════════════════════════════════════
// INITIAL UE POSITIONS  (from user specification, height=1.5 m)
// ══════════════════════════════════════════════════════════════════════
static const double g_ue_init_pos[NUM_UES][2] = {
    // Cell 2 (center hot-spot, overloaded)
    {1021.0, 977.0},   // UE  1
    { 980.0, 978.0},   // UE  2
    { 950.0, 930.0},   // UE  3
    { 912.0,1000.0},   // UE  4
    {1088.0,1000.0},   // UE  5
    { 950.0,1070.0},   // UE  6
    {1050.0, 950.0},   // UE  7
    {1050.0,1070.0},   // UE  8
    // Cell 5 (850, 1259.8)
    { 1120.0,1220.0},   // UE  9                  //{ 823.0,1320.0},   // UE  9  i make him in cell 4
    { 800.0,1294.0},   // UE 10
    // Cell 6 (700, 1000)
    { 685.0,1008.0},   // UE 11
    { 675.0, 950.0},   // UE 12
    // Cell 7 (850, 740.2)
    { 833.0, 796.0},   // UE 13
    { 820.0, 670.0},   // UE 14
    // Cell 8 (1150, 740.2)
    { 1168.0,827.0},   // UE 15  (note: user gave "1168 827" → swapped to x,y)
    {1120.0, 690.0},   // UE 16
    // Cell 3 (1300, 1000)
    {1325.0,1018.0},   // UE 17
    {1256.0,1046.0},   // UE 18
    // Cell 4 (1150, 1259.8)
    {1230.0,1255.0},   // UE 19
    {1160.0,1350.0},   // UE 20

};


// ═══════════════════════════════════════════════════════════════════════
// GLOBAL STATE — preserved from original (RIC / GUI / energy interfaces)
// ═══════════════════════════════════════════════════════════════════════

NS_LOG_COMPONENT_DEFINE ("ScenarioZero");

std::map<uint64_t, uint16_t> imsi_cellid;
std::map<uint16_t, std::set<uint64_t>> imsi_list;
std::map<uint16_t, Ptr<Node>> cellid_node;
std::map<uint32_t, uint16_t> ue_cellid_usinghandover;
std::map<uint64_t, uint32_t> ueimsi_nodeid;
int ue_assoc_list[32] = {0};
double maxXAxis;
double maxYAxis;
bool esON_list[32]                       = {false};
double totalnewEnergyConsumption_storage[32] = {0};
double totaloldEnergyConsumption_storage[32] = {0};
double current_energy_consumption[32]        = {0};
double curr_total_energy_consumption         = 0;
double max_energy_consumption                = 0;
double sum_curr_total_energy_consumption     = 0;
int    num_of_mmdev                          = 0;

// RSRP / PRB real-measurement maps (updated every indicationPeriodicity)
static std::map<uint64_t, double>  g_real_rsrp_serving;
static std::map<uint64_t, double>  g_real_rsrp_neighbor;
static std::map<uint64_t, uint16_t> g_real_neigh_cellid;
static std::map<uint16_t, int>     g_real_prb_used;
static std::map<uint16_t, int>     g_real_prb_total;
// SINR real measurements من L3ReportUeSinr trace
static std::map<uint64_t, double>  g_real_sinr_serving;   // imsi → SINR serving (dB)
static std::map<uint64_t, double>  g_real_sinr_neighbor;  // imsi → SINR best neighbor (dB)
static std::map<uint64_t, uint16_t> g_real_sinr_neigh_cellid; // imsi → neighbor cell id
// كل الـ neighbors: key = (imsi << 8 | cell_id)
static std::map<uint64_t, std::map<uint16_t, double>> g_real_sinr_all_neighbors;
// Pointers set during main() after device creation
static NodeContainer*      g_mmWaveEnbNodes_ptr = nullptr;
static NodeContainer*      g_ueNodes_ptr        = nullptr;
static NetDeviceContainer* g_mmWaveEnbDevs_ptr  = nullptr;
static std::map<uint16_t, uint64_t> g_rnti_to_imsi;
static std::map<uint32_t, int>      g_imsi_prb_used;  // key = (cellId<<16)|imsi
static std::ofstream g_rsrp_shared_file;                                                        //rsrp
static std::mutex    g_rsrp_file_mutex;                                                         //rsrp
// Sliding-window bitrate accumulators
static const double BITRATE_WINDOW_S = 1.0;
static std::map<uint64_t, std::deque<std::pair<double,uint32_t>>> g_dlBytesHist;
static std::map<uint64_t, std::deque<std::pair<double,uint32_t>>> g_ulBytesHist;

// PacketSink delta-bitrate tracking
static std::map<uint64_t, uint64_t> g_prevDlBytes;
static std::map<uint64_t, uint64_t> g_prevUlBytes;
static std::map<uint64_t, double>   g_prevBitrateTime;
static Ptr<Node>                    g_remoteHost = nullptr;
static std::vector<int>             g_ueToUlSinkIndex;

// CSV output files
static std::ofstream g_lstmCsv;
static std::ofstream g_kpmHandoverCsv;
static bool          g_kpmHandoverHeaderWritten = false;
static std::ofstream g_lbMetricsCsv;
static bool          g_lbMetricsHeaderWritten   = false;
static std::ofstream g_handover_log;

// ═══════════════════════════════════════════════════════════════════════
// METRIC 5 — Call-Drop Rate (RLF / handover-failure tracking)
// ═══════════════════════════════════════════════════════════════════════
static std::map<uint64_t, uint32_t> g_rlf_count;   // imsi → RLF count
static uint32_t g_total_rlf_events = 0;

// Handover tracking
std::map<uint64_t, uint16_t> g_ue_target_cells;
std::map<uint64_t, uint16_t> g_ue_source_cells;

// ═══════════════════════════════════════════════════════════════════════
//  MOBILITY — Bounded box with reflection + random direction changes
// ═══════════════════════════════════════════════════════════════════════

static void BoundedMobilityEnforce (uint32_t ueIdx, Ptr<Node> node,
                                    Ptr<UniformRandomVariable> rng)
{
    Ptr<ConstantVelocityMobilityModel> mob =
        node->GetObject<ConstantVelocityMobilityModel> ();
    if (!mob) return;

    Vector pos = mob->GetPosition ();
    Vector vel = mob->GetVelocity ();
    bool reflected = false;

    auto reflect = [&] (double& p, double& v, double lo, double hi) {
        if (p < lo) { p = lo; v =  std::abs (v) + 0.1; reflected = true; }
        else if (p > hi) { p = hi; v = -(std::abs (v) + 0.1); reflected = true; }
    };

    reflect (pos.x, vel.x, BOUND_MIN, BOUND_MAX);
    reflect (pos.y, vel.y, BOUND_MIN, BOUND_MAX);

    if (reflected) {
        // Add random angular perturbation to avoid billiard-ball patterns
        double angle = std::atan2 (vel.y, vel.x);
        double perturb = (rng->GetValue () - 0.5) * 2.0
                         * (REFLECT_PERTURB_DEG * M_PI / 180.0);
        angle += perturb;
        double speed = std::sqrt (vel.x*vel.x + vel.y*vel.y);
        if (speed < 0.1) speed = (ueIdx < NUM_UES) ? g_ue_speed_mps[ueIdx] : 1.4;
        vel.x = speed * std::cos (angle);
        vel.y = speed * std::sin (angle);
        mob->SetPosition (pos);
        mob->SetVelocity (vel);
    }

    Simulator::Schedule (Seconds (BOUNDARY_CHECK_S),
                         &BoundedMobilityEnforce, ueIdx, node, rng);
}

static void WriteRsrpSharedFile()                                                                 //rsrp
{
    std::lock_guard<std::mutex> lock(g_rsrp_file_mutex);
    
    const char* home = std::getenv("HOME");
    std::string path = (home && home[0]) 
                       ? std::string(home) + "/rsrp_shared.csv" 
                       : "rsrp_shared.csv";
    
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return;
    
    // Header جديد: كل سطر = (imsi, serving_cell, rsrp_serving, neigh_cell, rsrp_neigh)
    // وممكن يكون في أكتر من سطر لنفس الـ imsi (واحد لكل neighbor)
    f << "imsi,serving_cell,rsrp_serving_dbm,neigh_cell,rsrp_neigh_dbm\n";
    
    for (uint32_t u = 0; u < NUM_UES; u++) {
        uint64_t imsi     = u + 1;
        uint16_t srv_cell = (uint16_t)ue_assoc_list[u];
        
        double rsrp_srv = (g_real_rsrp_serving.count(imsi) &&
                           g_real_rsrp_serving[imsi] > -199.0)
                          ? g_real_rsrp_serving[imsi] : -200.0;
        
        // احسب RSRP لكل الـ neighbors (مش أحسن واحد بس)
        if (!g_mmWaveEnbDevs_ptr || !g_ueNodes_ptr || u >= g_ueNodes_ptr->GetN()) {
            f << imsi << "," << srv_cell << "," << rsrp_srv << ",0,-200.0\n";
            continue;
        }
        
        Ptr<MobilityModel> ue_mob = g_ueNodes_ptr->Get(u)->GetObject<MobilityModel>();
        if (!ue_mob) {
            f << imsi << "," << srv_cell << "," << rsrp_srv << ",0,-200.0\n";
            continue;
        }
        Vector ue_pos = ue_mob->GetPosition();
        
        bool wrote_any_neighbor = false;
        for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN(); ++d) {
            Ptr<MmWaveEnbNetDevice> mmdev =
                DynamicCast<MmWaveEnbNetDevice>(g_mmWaveEnbDevs_ptr->Get(d));
            if (!mmdev) continue;
            uint16_t cid = mmdev->GetCellId();
            if (cid == srv_cell) continue;  // مش serving cell
            
            Ptr<MobilityModel> enb_mob = mmdev->GetNode()->GetObject<MobilityModel>();
            if (!enb_mob) continue;
            Vector enb_pos = enb_mob->GetPosition();
            
            double dx  = ue_pos.x - enb_pos.x;
            double dy  = ue_pos.y - enb_pos.y;
            double dz  = ue_pos.z - enb_pos.z;
            double d3d = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (d3d < 1.0) d3d = 1.0;
            
            double pl        = 32.4 + 21.0*std::log10(d3d) + 20.0*std::log10(FC_GHZ);
            double rsrp_neigh = TX_POWER_DBM - pl;
            
            // سطر لكل neighbor
            f << imsi       << ","
              << srv_cell   << ","
              << rsrp_srv   << ","
              << cid        << ","
              << rsrp_neigh << "\n";
            wrote_any_neighbor = true;
        }
        
        if (!wrote_any_neighbor) {
            f << imsi << "," << srv_cell << "," << rsrp_srv << ",0,-200.0\n";
        }
    }
    
    f.flush();
    f.close();
}

static void WriteSinrSharedFile()
{
    const char* home = std::getenv("HOME");
    std::string path = (home && home[0])
                       ? std::string(home) + "/sinr_shared.csv"
                       : "sinr_shared.csv";

    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return;

    f << "imsi,serving_cell,sinr_serving_db,neigh_cell,sinr_neigh_db\n";

    for (uint32_t u = 0; u < NUM_UES; u++) {
        uint64_t imsi     = u + 1;
        uint16_t srv_cell = (uint16_t)ue_assoc_list[u];

        double sinr_srv = (g_real_sinr_serving.count(imsi))
                          ? g_real_sinr_serving[imsi] : -200.0;

        // اكتب سطر لكل neighbor موجود في الـ map
        bool wrote_any = false;
        if (g_real_sinr_all_neighbors.count(imsi)) {
            for (auto& kv : g_real_sinr_all_neighbors[imsi]) {
                uint16_t ncell     = kv.first;
                double   sinr_neigh = kv.second;
                f << imsi     << ","
                  << srv_cell << ","
                  << sinr_srv << ","
                  << ncell    << ","
                  << sinr_neigh << "\n";
                wrote_any = true;
            }
        }
        if (!wrote_any) {
            f << imsi << "," << srv_cell << "," << sinr_srv << ",0,-200.0\n";
        }
    }

    f.flush();
    f.close();
}

static void BoundedMobilityDirectionChange (uint32_t ueIdx, Ptr<Node> node,
                                             Ptr<UniformRandomVariable> rng)
{
    Ptr<ConstantVelocityMobilityModel> mob =
        node->GetObject<ConstantVelocityMobilityModel> ();
    if (!mob) return;

    double angle = rng->GetValue () * 2.0 * M_PI;
    double speed = (ueIdx < NUM_UES) ? g_ue_speed_mps[ueIdx] : 1.4;
    mob->SetVelocity (Vector (speed * std::cos (angle), speed * std::sin (angle), 0.0));

    double nextIn = DIR_CHANGE_MIN_S
                    + (DIR_CHANGE_MAX_S - DIR_CHANGE_MIN_S) * rng->GetValue ();
    Simulator::Schedule (Seconds (nextIn),
                         &BoundedMobilityDirectionChange, ueIdx, node, rng);
}

// ═══════════════════════════════════════════════════════════════════════
//  BITRATE HELPERS  (sliding-window + PacketSink delta)
// ═══════════════════════════════════════════════════════════════════════

static void DlRxCb (uint64_t imsi, Ptr<const Packet> p, const Address&)
{
    double now = Simulator::Now ().GetSeconds ();
    auto& q = g_dlBytesHist[imsi];
    q.push_back ({now, p->GetSize ()});
    while (!q.empty () && (now - q.front ().first) > BITRATE_WINDOW_S)
        q.pop_front ();
}

static void UlTxCb (uint64_t imsi, Ptr<const Packet> p)
{
    double now = Simulator::Now ().GetSeconds ();
    auto& q = g_ulBytesHist[imsi];
    q.push_back ({now, p->GetSize ()});
    while (!q.empty () && (now - q.front ().first) > BITRATE_WINDOW_S)
        q.pop_front ();
}

static double GetDlBitrate (uint64_t imsi)
{
    double now = Simulator::Now ().GetSeconds ();
    auto it = g_dlBytesHist.find (imsi);
    if (it == g_dlBytesHist.end ()) return 0.0;
    uint64_t sum = 0;
    for (const auto& e : it->second)
        if ((now - e.first) <= BITRATE_WINDOW_S) sum += e.second;
    return (sum * 8.0) / BITRATE_WINDOW_S;
}

static double GetUlBitrate (uint64_t imsi)
{
    double now = Simulator::Now ().GetSeconds ();
    auto it = g_ulBytesHist.find (imsi);
    if (it == g_ulBytesHist.end ()) return 0.0;
    uint64_t sum = 0;
    for (const auto& e : it->second)
        if ((now - e.first) <= BITRATE_WINDOW_S) sum += e.second;
    return (sum * 8.0) / BITRATE_WINDOW_S;
}

// ═══════════════════════════════════════════════════════════════════════
//  UpdateRealRsrpPrb  — called every indicationPeriodicity
//  Computes RSRP from 3GPP UMi LoS path loss model and PRB load
// ═══════════════════════════════════════════════════════════════════════

extern int ue_assoc_list[32];

static void UpdateRealRsrpPrb ()
{
    if (!g_mmWaveEnbNodes_ptr || !g_ueNodes_ptr || !g_mmWaveEnbDevs_ptr) return;

    g_real_prb_used.clear ();
    g_real_prb_total.clear ();

    // Count UEs per cell
    std::map<uint16_t, int> ues_per_cell;
    for (uint32_t u = 0; u < g_ueNodes_ptr->GetN (); ++u) {
        uint16_t cid = (uint16_t)ue_assoc_list[u];
        if (cid > 0) ues_per_cell[cid]++;
    }

    // PRB allocation (equal-share scheduler baseline)
    for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN (); ++d) {
        Ptr<MmWaveEnbNetDevice> mmdev =
            DynamicCast<MmWaveEnbNetDevice> (g_mmWaveEnbDevs_ptr->Get (d));
        if (!mmdev) continue;
        uint16_t cid = mmdev->GetCellId ();
        int nue = ues_per_cell.count (cid) ? ues_per_cell[cid] : 0;
        g_real_prb_total[cid] = SCENARIO_TOTAL_PRB;
        g_real_prb_used[cid]  = (nue > 0)
                                 ? (SCENARIO_TOTAL_PRB / nue) * nue
                                 : 0;
    }

    // Noise floor: kT=-174 dBm/Hz, BW=20 MHz, NF=5 dB → -96 dBm
    static const double NOISE_FLOOR_DBM = -96.0;
    const double noise_lin = std::pow (10.0, NOISE_FLOOR_DBM / 10.0);

    // RSRP per UE — 3GPP TR 38.901 UMi LoS: PL = 32.4 + 21·log10(d) + 20·log10(fc)
    for (uint32_t u = 0; u < g_ueNodes_ptr->GetN (); ++u) {
        uint64_t imsi = u + 1;
        Ptr<MobilityModel> ue_mob =
            g_ueNodes_ptr->Get (u)->GetObject<MobilityModel> ();
        if (!ue_mob) continue;
        Vector ue_pos = ue_mob->GetPosition ();

        uint16_t serving_cell = (uint16_t)ue_assoc_list[u];
        double serving_rsrp = -200.0;
        double best_rsrp    = -200.0;
        uint16_t best_cell  = 0;

        std::vector<std::pair<uint16_t, double>> all_cell_rsrp; // (cellId, rsrp_dBm)

        for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN (); ++d) {
            Ptr<MmWaveEnbNetDevice> mmdev =
                DynamicCast<MmWaveEnbNetDevice> (g_mmWaveEnbDevs_ptr->Get (d));
            if (!mmdev) continue;
            uint16_t cid = mmdev->GetCellId ();
            Ptr<MobilityModel> enb_mob =
                mmdev->GetNode ()->GetObject<MobilityModel> ();
            if (!enb_mob) continue;
            Vector enb_pos = enb_mob->GetPosition ();

            double dx  = ue_pos.x - enb_pos.x;
            double dy  = ue_pos.y - enb_pos.y;
            double dz  = ue_pos.z - enb_pos.z;
            double d3d = std::sqrt (dx*dx + dy*dy + dz*dz);
            if (d3d < 1.0) d3d = 1.0;

            double pl   = 32.4 + 21.0 * std::log10 (d3d) + 20.0 * std::log10 (FC_GHZ);
            double rsrp = TX_POWER_DBM - pl;

            all_cell_rsrp.push_back ({cid, rsrp});

            if (cid == serving_cell) {
                serving_rsrp = rsrp;
            }
            if (rsrp > best_rsrp && cid != serving_cell) {
                best_rsrp = rsrp;
                best_cell = cid;
            }
        }

        // Normalize wideband RSRP to per-RE NR-RSRP (3GPP TS 38.215):
        // subtract 10·log10(N_RBs × 12 subcarriers) to convert total BW power → per-RE power
        const double BW_NORM_DB = 10.0 * std::log10 (N_RBS_20MHZ * 12.0); // ≈ 25.26 dB
        g_real_rsrp_serving[imsi]  = serving_rsrp - BW_NORM_DB;
        g_real_rsrp_neighbor[imsi] = (best_cell > 0) ? (best_rsrp - BW_NORM_DB) : -200.0;
        g_real_neigh_cellid[imsi]  = best_cell;

        // Compute interference-limited SINR for the serving cell (uses wideband power — unchanged)
        if (serving_rsrp > -199.0) {
            double srv_lin = std::pow (10.0, serving_rsrp / 10.0);
            double inr_lin = 0.0;
            for (auto& kv : all_cell_rsrp) {
                if (kv.first != serving_cell)
                    inr_lin += std::pow (10.0, kv.second / 10.0);
            }
            g_real_sinr_serving[imsi] =
                10.0 * std::log10 (srv_lin / (noise_lin + inr_lin));
        }

        // Per-neighbor SINR: hypothetical SINR if UE were served by each neighbor
        g_real_sinr_all_neighbors[imsi].clear ();
        for (auto& kv : all_cell_rsrp) {
            if (kv.first == serving_cell) continue;
            double ncell_lin = std::pow (10.0, kv.second / 10.0);
            double other_inr = 0.0;
            for (auto& kv2 : all_cell_rsrp) {
                if (kv2.first != kv.first)
                    other_inr += std::pow (10.0, kv2.second / 10.0);
            }
            g_real_sinr_all_neighbors[imsi][kv.first] =
                10.0 * std::log10 (ncell_lin / (noise_lin + other_inr));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  SchedTraceCallback  — real PRB per UE from MAC scheduler
// ═══════════════════════════════════════════════════════════════════════

static void SchedTraceCallback (Ptr<MmWaveEnbNetDevice> dev,
                                MmWaveEnbMac::MmWaveSchedTraceInfo schedInfo)
{
    if (!dev) return;
    uint16_t cellId = dev->GetCellId ();
    auto ueMap = dev->GetUeMap ();
    auto& slotAlloc = schedInfo.m_indParam.m_slotAllocInfo;

    for (auto& tti : slotAlloc.m_ttiAllocInfo) {
        uint16_t rnti   = tti.m_rnti;
        uint8_t  numSym = tti.m_dci.m_numSym;
        if (rnti == 0 || numSym == 0) continue;

        int realPrb = (int)std::round (((double)numSym / 14.0) * SCENARIO_TOTAL_PRB);
        if (realPrb > SCENARIO_TOTAL_PRB) realPrb = SCENARIO_TOTAL_PRB;

        auto it = ueMap.find (rnti);
        if (it != ueMap.end ()) {
            uint64_t imsi = it->second->GetImsi ();
            uint32_t key  = ((uint32_t)cellId << 16) | (uint32_t)imsi;
            g_imsi_prb_used[key] = realPrb;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  ★ METRIC 1–5 : Load Balancing KPI Logger ★
//  Called every indicationPeriodicity; logs to ~/lb_metrics.csv
// ═══════════════════════════════════════════════════════════════════════
//
//  Metric 1 — avg_prb_load:
//    For each active cell: prb_load_i = prb_used_i / TOTAL_PRB
//    avg_prb_load = mean over all active cells
//    ► PRB load reflects actual resource consumption (VOIP ≠ FTP even with same UE count)
//
//  Metric 2 — avg_ue_load:
//    ue_load_i = ues_in_cell_i / MAX_UES_PER_CELL  (capacity index)
//    avg_ue_load = mean over active cells
//    ► Intentionally differs from Metric 1 when VOIP/FTP coexist in same cell
//
//  Metric 3 — load_variance:
//    variance of {prb_load_i} across active cells
//    ► Approaches 0 when xApp successfully balances load
//
//  Metric 4 — avg_throughput [Mbps]:
//    mean of (DL_bitrate_u + UL_bitrate_u) / 1e6  over all attached UEs
//    ► Should increase post-balancing as overloaded cell gets relief
//
//  Metric 5 — cdr (call-drop rate):
//    cdr = g_total_rlf_events / MAX(1, attached_ues)
//    ► Counts RLF + handover-failure events; should decrease after LB

static const double MAX_UES_PER_CELL = 10.0;  // capacity reference (normalization)

static void LogLbMetrics ()
{
    if (!g_lbMetricsCsv.is_open ()) return;

    double t = Simulator::Now ().GetSeconds ();

    // ─ Build per-cell UE count and PRB-load maps ────────────────────
    std::map<uint16_t, int>    ues_in_cell;
    std::map<uint16_t, double> prb_load;

    if (g_ueNodes_ptr && g_mmWaveEnbDevs_ptr) {
        // Count attached UEs per cell
        for (uint32_t u = 0; u < g_ueNodes_ptr->GetN (); ++u) {
            uint16_t cid = (uint16_t)ue_assoc_list[u];
            if (cid > 0) ues_in_cell[cid]++;
        }
        // Sum real PRB used per cell from MAC scheduler trace
        std::map<uint16_t, int> cell_prb_sum;
        for (auto& kv : g_imsi_prb_used) {
            uint16_t cid = (uint16_t)(kv.first >> 16);
            cell_prb_sum[cid] += kv.second;
        }
        for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN (); ++d) {
            Ptr<MmWaveEnbNetDevice> mmdev =
                DynamicCast<MmWaveEnbNetDevice> (g_mmWaveEnbDevs_ptr->Get (d));
            if (!mmdev) continue;
            uint16_t cid = mmdev->GetCellId ();
            int used = cell_prb_sum.count (cid) ? cell_prb_sum[cid] : 0;
            // Clamp to total PRBs (scheduler may report > total in aggregated TTIs)
            if (used > SCENARIO_TOTAL_PRB) used = SCENARIO_TOTAL_PRB;
            prb_load[cid] = (double)used / (double)SCENARIO_TOTAL_PRB;
        }
    }

    int active_cells = (int)prb_load.size ();
    if (active_cells == 0) {
        // Nothing to log yet — write zeros
        g_lbMetricsCsv << t << ",0,0,0,0,0\n";
        return;
    }

    // ─ Metric 1: avg PRB load ────────────────────────────────────────
    double sum_prb = 0.0;
    for (auto& kv : prb_load) sum_prb += kv.second;
    double avg_prb_load = sum_prb / active_cells;

    // ─ Metric 2: avg UE load ─────────────────────────────────────────
    double sum_ue = 0.0;
    int cell_count = 0;
    for (auto& kv : prb_load) {   // iterate over active cells
        uint16_t cid = kv.first;
        double ue_l = (ues_in_cell.count (cid) ? ues_in_cell[cid] : 0)
                      / MAX_UES_PER_CELL;
        sum_ue += ue_l;
        cell_count++;
    }
    double avg_ue_load = (cell_count > 0) ? sum_ue / cell_count : 0.0;

    // ─ Metric 3: load variance ───────────────────────────────────────
    double mean = avg_prb_load;
    double sq_sum = 0.0;
    for (auto& kv : prb_load) {
        double diff = kv.second - mean;
        sq_sum += diff * diff;
    }
    double load_variance = sq_sum / active_cells;

    // ─ Metric 4: avg throughput [Mbps] ───────────────────────────────
    double sum_tp = 0.0;
    int attached = 0;
    if (g_ueNodes_ptr) {
        for (uint32_t u = 0; u < g_ueNodes_ptr->GetN (); ++u) {
            if (ue_assoc_list[u] == 0) continue;
            uint64_t imsi = u + 1;
            double dl = GetDlBitrate (imsi) / 1e6;
            double ul = GetUlBitrate (imsi) / 1e6;
            sum_tp += dl + ul;
            attached++;
        }
    }
    double avg_throughput = (attached > 0) ? sum_tp / attached : 0.0;

    // ─ Metric 5: call-drop rate ──────────────────────────────────────
    double cdr = (attached > 0)
                 ? (double)g_total_rlf_events / (double)attached
                 : 0.0;

    g_lbMetricsCsv << t << ","
                   << avg_prb_load    << ","
                   << avg_ue_load     << ","
                   << load_variance   << ","
                   << avg_throughput  << ","
                   << cdr             << "\n";

    NS_LOG_UNCOND ("[LB-METRICS] t=" << t
                   << "s  PRB_load=" << avg_prb_load
                   << "  UE_load="   << avg_ue_load
                   << "  Var="       << load_variance
                   << "  TP="        << avg_throughput << " Mbps"
                   << "  CDR="       << cdr);
}

// ═══════════════════════════════════════════════════════════════════════
//  LSTM CSV callback — 16-feature dataset (original, unchanged)
// ═══════════════════════════════════════════════════════════════════════

static void LstmCsvTraceCb (NodeContainer* ueNodes, double bandwidthHz, uint64_t imsi,
                             uint16_t servingCellId, double servingSinrDb,
                             uint16_t bestNeighCellId, double bestNeighSinrDb)
{
    if (!g_lstmCsv.is_open ()) {
        const char* home = std::getenv ("HOME");
        std::string path = (home && home[0])
                           ? std::string (home) + "/lstm_features.csv"
                           : "lstm_features.csv";
        g_lstmCsv.open (path, std::ios::out | std::ios::app);
        if (g_lstmCsv.is_open ())
            NS_LOG_UNCOND ("LSTM CSV appending: " << path);
    }
    double t     = Simulator::Now ().GetSeconds ();
    double Level = -100.0 + servingSinrDb;
    double SNR   = servingSinrDb;
    double CQI   = (servingSinrDb < -6.0) ? 0.0
                   : ((servingSinrDb > 26.0) ? 15.0
                      : (servingSinrDb + 6.0) / 2.2);
    double SecondCell_RSRP = (bestNeighCellId == 0) ? 0.0 : (-100.0 + bestNeighSinrDb);
    double SecondCell_SNR  = (bestNeighCellId == 0) ? -999.0 : bestNeighSinrDb;
    double NRxLev1         = SecondCell_RSRP;
    double speed           = 0.0;
    if (ueNodes && imsi >= 1 && imsi <= ueNodes->GetN ()) {
        Ptr<MobilityModel> mob =
            ueNodes->Get (imsi - 1)->GetObject<MobilityModel> ();
        if (mob) { Vector v = mob->GetVelocity ();
                   speed = std::sqrt (v.x*v.x + v.y*v.y + v.z*v.z); }
    }
    double DL_bitrate = GetDlBitrate (imsi);
    double UL_bitrate = GetUlBitrate (imsi);
    double BANDWIDTH  = (bandwidthHz > 0) ? (bandwidthHz / 1e6) : 20.0;

    g_lstmCsv << t    << "," << imsi << ","
              << Level << "," << 0.0 << "," << SNR  << "," << CQI       << ","
              << SecondCell_RSRP << "," << SecondCell_SNR << ","
              << NRxLev1 << "," << 0.0 << ","
              << speed   << "," << DL_bitrate << "," << UL_bitrate << ","
              << BANDWIDTH << "," << servingCellId << "," << bestNeighCellId << "\n";
}

// ═══════════════════════════════════════════════════════════════════════
//  GRU xApp 18-feature KPM CSV  (original PrintKpmFeaturesForHandoverXapp)
// ═══════════════════════════════════════════════════════════════════════

void PrintKpmFeaturesForHandoverXapp (
    uint32_t enbNodeId, uint32_t sessionId, Ptr<Node> ueNode,
    uint16_t servingCellId, double rsrp_serving, double rsrq_serving,
    double snr_serving, int cqi, double rsrp_neighbor1, double snr_neighbor1,
    double rsrp_neighbor2, double rsrq_neighbor2, double speed,
    double dl_bitrate, double ul_bitrate, double bandwidthHz)
{
    if (!g_kpmHandoverCsv.is_open ()) {
        const char* home = std::getenv ("HOME");
        std::string path = (home && home[0])
                           ? std::string (home) + "/kpm_handover_features.csv"
                           : "kpm_handover_features.csv";
        g_kpmHandoverCsv.open (path, std::ios::out | std::ios::trunc);
        if (g_kpmHandoverCsv.is_open ())
            NS_LOG_UNCOND ("GRU KPM CSV: " << path);
    }
    if (!g_kpmHandoverHeaderWritten && g_kpmHandoverCsv.is_open ()) {
        g_kpmHandoverCsv
            << "SessionID,ElapsedTime,Node,CellID,Level,Qual,SNR,CQI,"
            << "SecondCell_RSRP,SecondCell_SNR,NRxLev1,NQual1,"
            << "Speed,DL_bitrate,UL_bitrate,BANDWIDTH,Latitude,Longitude\n";
        g_kpmHandoverHeaderWritten = true;
    }
    if (!g_kpmHandoverCsv.is_open ()) return;

    uint64_t elapsed_ms = (uint64_t)Simulator::Now ().GetMilliSeconds ();
    double lat = 3.0695, lon = 101.5998;
    if (ueNode) {
        Ptr<MobilityModel> mob = ueNode->GetObject<MobilityModel> ();
        if (mob) {
            Vector pos    = mob->GetPosition ();
            double base_lat = 3.0695, base_lon = 101.5998;
            lat = base_lat + (pos.y / 111320.0);
            lon = base_lon + (pos.x / (111320.0 * std::cos (base_lat * M_PI / 180.0)));
        }
    }
    double bwMhz = (bandwidthHz > 0) ? (bandwidthHz / 1e6) : 20.0;
    g_kpmHandoverCsv
        << sessionId    << "," << elapsed_ms    << "," << enbNodeId  << ","
        << servingCellId << "," << rsrp_serving  << "," << rsrq_serving << ","
        << snr_serving   << "," << cqi            << ","
        << rsrp_neighbor1 << "," << snr_neighbor1 << ","
        << rsrp_neighbor2 << "," << rsrq_neighbor2 << ","
        << speed         << "," << dl_bitrate     << "," << ul_bitrate << ","
        << bwMhz         << "," << lat            << "," << lon        << "\n";
}

// ═══════════════════════════════════════════════════════════════════════
//  KpmHandoverCsvCb  — called by L3ReportUeSinr trace (original, unchanged)
// ═══════════════════════════════════════════════════════════════════════

void KpmHandoverCsvCb (uint32_t enbNodeId, NodeContainer* ueNodes, double bandwidthHz,
                        uint64_t imsi, uint16_t servingCellId, double servingSinrDb,
                        uint16_t bestNeighCellId, double bestNeighSinrDb)
{
    // Real RSRP from 3GPP path loss model
    double rsrp_serving = (g_real_rsrp_serving.count (imsi) &&
                           g_real_rsrp_serving[imsi] > -199.0)
                          ? g_real_rsrp_serving[imsi]
                          : (-100.0 + servingSinrDb);
    double snr_serving  = servingSinrDb;

    double sinr_srv_db  = (g_real_sinr_serving.count (imsi) && g_real_sinr_serving[imsi] > -199.0)
                          ? g_real_sinr_serving[imsi] : servingSinrDb;
    double sinr_srv_lin = std::pow (10.0, sinr_srv_db / 10.0);
    double rsrq_serving = 10.0 * std::log10 (sinr_srv_lin / (sinr_srv_lin + 1.0));
    rsrq_serving = std::max (RSRQ_MIN_DB, std::min (RSRQ_MAX_DB, rsrq_serving));

    int cqi = (servingSinrDb < -6.0) ? 0
              : ((servingSinrDb > 26.0) ? 15
                 : (int)((servingSinrDb + 6.0) / 2.2));

    double rsrp_neighbor1 = (g_real_rsrp_neighbor.count (imsi) &&
                             g_real_rsrp_neighbor[imsi] > -199.0)
                            ? g_real_rsrp_neighbor[imsi]
                            : ((bestNeighCellId == 0) ? -200.0 : (-100.0 + bestNeighSinrDb));
    double snr_neighbor1  = (bestNeighCellId == 0) ? -999.0 : bestNeighSinrDb;

    // Update RNTI→IMSI map for this cell
    if (g_mmWaveEnbDevs_ptr) {
        for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN (); ++d) {
            Ptr<MmWaveEnbNetDevice> dev =
                DynamicCast<MmWaveEnbNetDevice> (g_mmWaveEnbDevs_ptr->Get (d));
            if (!dev || dev->GetCellId () != servingCellId) continue;
            for (auto& kv : dev->GetUeMap ())
                if (kv.second->GetImsi () == imsi)
                    g_rnti_to_imsi[kv.first] = imsi;
        }
    }

    int nue_in_cell = 0;
    if (g_ueNodes_ptr)
        for (uint32_t u = 0; u < g_ueNodes_ptr->GetN (); ++u)
            if ((uint16_t)ue_assoc_list[u] == servingCellId) nue_in_cell++;

    uint32_t prb_key   = ((uint32_t)servingCellId << 16) | (uint32_t)imsi;
    int real_prb_used  = g_imsi_prb_used.count (prb_key)
                         ? g_imsi_prb_used[prb_key]
                         : ((nue_in_cell > 0) ? (SCENARIO_TOTAL_PRB / nue_in_cell) : 0);
    int real_prb_total = g_real_prb_total.count (servingCellId)
                         ? g_real_prb_total[servingCellId]
                         : SCENARIO_TOTAL_PRB;

    // Use interference-limited SINR computed by UpdateRealRsrpPrb(); fall back to
    // the L3 trace value (SNR only) only if UpdateRealRsrpPrb has not run yet.
    double real_sinr_log = (g_real_sinr_serving.count (imsi) && g_real_sinr_serving[imsi] > -199.0)
                           ? g_real_sinr_serving[imsi] : servingSinrDb;
    NS_LOG_UNCOND ("[KPM-REAL] UE " << imsi << " Cell " << servingCellId
                   << "  RSRP=" << rsrp_serving << " dBm"
                   << "  SINR=" << real_sinr_log << " dB"
                   << "  PRB="  << real_prb_used << "/" << real_prb_total);

    double rsrp_neighbor2 = rsrp_neighbor1;
    double rsrq_neighbor2 = RSRQ_MIN_DB;
    if (bestNeighCellId != 0) {
        double neigh_sinr_db = (g_real_sinr_all_neighbors.count (imsi) &&
                                g_real_sinr_all_neighbors.at (imsi).count (bestNeighCellId))
                               ? g_real_sinr_all_neighbors.at (imsi).at (bestNeighCellId)
                               : bestNeighSinrDb;
        double sinr_n_lin = std::pow (10.0, neigh_sinr_db / 10.0);
        rsrq_neighbor2 = 10.0 * std::log10 (sinr_n_lin / (sinr_n_lin + 1.0));
        rsrq_neighbor2 = std::max (RSRQ_MIN_DB, std::min (RSRQ_MAX_DB, rsrq_neighbor2));
    }

    // Speed from mobility model
    double speed = 0.0;
    Ptr<Node> ueNode;
    if (ueNodes && imsi >= 1 && imsi <= ueNodes->GetN ()) {
        ueNode = ueNodes->Get (imsi - 1);
        Ptr<MobilityModel> mob = ueNode->GetObject<MobilityModel> ();
        if (mob) { Vector v = mob->GetVelocity ();
                   speed = std::sqrt (v.x*v.x + v.y*v.y + v.z*v.z); }
    }

    // Instantaneous bitrate (PacketSink delta)
    double dl_bitrate = 0.0, ul_bitrate = 0.0;
    double now = Simulator::Now ().GetSeconds ();
    double dt  = (g_prevBitrateTime.count (imsi))
                 ? now - g_prevBitrateTime[imsi] : 0.0;

    if (ueNodes && imsi >= 1 && imsi <= ueNodes->GetN ()) {
        Ptr<Node> ue = ueNodes->Get (imsi - 1);
        for (uint32_t a = 0; a < ue->GetNApplications (); ++a) {
            Ptr<PacketSink> sink = DynamicCast<PacketSink> (ue->GetApplication (a));
            if (sink) {
                uint64_t curr = sink->GetTotalRx ();
                if (g_prevDlBytes.count (imsi) && dt > 0.01)
                    dl_bitrate = ((curr - g_prevDlBytes[imsi]) * 8.0) / dt;
                g_prevDlBytes[imsi] = curr;
                break;
            }
        }
    }

    if (g_remoteHost) {
        uint32_t idx = (uint32_t)(imsi - 1);
        int appIdx   = (idx < g_ueToUlSinkIndex.size () && g_ueToUlSinkIndex[idx] >= 0)
                       ? g_ueToUlSinkIndex[idx] : (int)idx;
        if (appIdx >= 0 && (uint32_t)appIdx < g_remoteHost->GetNApplications ()) {
            Ptr<PacketSink> sink =
                DynamicCast<PacketSink> (g_remoteHost->GetApplication (appIdx));
            if (sink) {
                uint64_t curr = sink->GetTotalRx ();
                if (g_prevUlBytes.count (imsi) && dt > 0.01)
                    ul_bitrate = ((curr - g_prevUlBytes[imsi]) * 8.0) / dt;
                g_prevUlBytes[imsi] = curr;
            }
        }
    }
    g_prevBitrateTime[imsi] = now;
    // g_real_sinr_serving is populated by UpdateRealRsrpPrb() with proper SINR
    // (interference-limited). Store only the neighbor map entries not already set.
    g_real_sinr_neigh_cellid[imsi]   = bestNeighCellId;
    g_real_sinr_neighbor[imsi]       = bestNeighSinrDb;
    PrintKpmFeaturesForHandoverXapp (
        enbNodeId, (uint32_t)imsi, ueNode, servingCellId,
        rsrp_serving, rsrq_serving, snr_serving, cqi,
        rsrp_neighbor1, snr_neighbor1, rsrp_neighbor2, rsrq_neighbor2,
        speed, dl_bitrate, ul_bitrate, bandwidthHz);
}
static void AllNeighborSinrCb(uint64_t imsi,
                               uint16_t /*servingCellId*/,
                               uint16_t neighCellId,
                               double   neighSinrDb)
{
    // Only use the L3 trace SNR if UpdateRealRsrpPrb has not populated the entry yet
    if (neighCellId > 0 && neighSinrDb > -200.0 &&
        !g_real_sinr_all_neighbors[imsi].count (neighCellId))
        g_real_sinr_all_neighbors[imsi][neighCellId] = neighSinrDb;
}
// ═══════════════════════════════════════════════════════════════════════
//  HANDOVER CALLBACKS  (original, + CDR counter for Metric 5)
// ═══════════════════════════════════════════════════════════════════════

void OnHandoverStart (std::string context, uint64_t imsi,
                      uint16_t sourceCellId, uint16_t rnti, uint16_t targetCellId)
{
    double t = Simulator::Now ().GetSeconds ();
    NS_LOG_UNCOND ("[NS3-HO] " << t << "s  HO-START: UE " << imsi
                   << " Cell " << sourceCellId << " → " << targetCellId);
    g_ue_target_cells[imsi] = targetCellId;
    g_ue_source_cells[imsi] = sourceCellId;
    if (g_handover_log.is_open ())
        g_handover_log << t << "," << imsi << "," << sourceCellId << ","
                       << targetCellId << ",START,0\n";
}

void OnHandoverSuccess (std::string context, uint64_t imsi,
                        uint16_t cellId, uint16_t rnti)
{
    double   t        = Simulator::Now ().GetSeconds ();
    uint16_t fromCell = g_ue_source_cells[imsi];
    NS_LOG_UNCOND ("[NS3-HO] " << t << "s  ✅ HO-SUCCESS: UE " << imsi
                   << " Cell " << fromCell << " → " << cellId);
    if (g_handover_log.is_open ())
        g_handover_log << t << "," << imsi << "," << fromCell << ","
                       << cellId << ",SUCCESS,1\n";
}

void OnHandoverFailure (std::string context, uint64_t imsi,
                        uint16_t sourceCellId)
{
    double t = Simulator::Now ().GetSeconds ();
    NS_LOG_UNCOND ("[NS3-HO] " << t << "s  ❌ HO-FAILED: UE " << imsi
                   << " still on Cell " << sourceCellId);
    // Count as call-drop event for Metric 5
    g_rlf_count[imsi]++;
    g_total_rlf_events++;
    if (g_handover_log.is_open ())
        g_handover_log << t << "," << imsi << "," << sourceCellId << ","
                       << g_ue_target_cells[imsi] << ",FAILURE,0\n";
}

// ═══════════════════════════════════════════════════════════════════════
//  GUI / ENERGY  helper functions  (original, unchanged)
// ═══════════════════════════════════════════════════════════════════════

void PrintGnuplottableUeListToFile (std::string filename)
{
    std::ofstream outFile;
    outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
    if (!outFile.is_open ()) { NS_LOG_ERROR ("Can't open " << filename); return; }
    for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it) {
        Ptr<Node> node = *it;
        for (int j = 0; j < (int)node->GetNDevices (); j++) {
            Ptr<LteUeNetDevice>    ued = node->GetDevice (j)->GetObject<LteUeNetDevice> ();
            Ptr<MmWaveUeNetDevice> mmd = node->GetDevice (j)->GetObject<MmWaveUeNetDevice> ();
            Ptr<McUeNetDevice>     mcu = node->GetDevice (j)->GetObject<McUeNetDevice> ();
            Vector pos;
            uint64_t id = 0;
            if (ued)  { pos = node->GetObject<MobilityModel>()->GetPosition(); id = ued->GetImsi(); }
            else if (mmd) { pos = node->GetObject<MobilityModel>()->GetPosition(); id = mmd->GetImsi(); }
            else if (mcu) { pos = node->GetObject<MobilityModel>()->GetPosition(); id = mcu->GetImsi(); }
            else continue;
            outFile << "set label \"" << id << "\" at " << pos.x << "," << pos.y
                    << " left font \"Helvetica,8\" textcolor rgb \"black\" front "
                    << "point pt 1 ps 0.3 lc rgb \"black\" offset 0,0\n";
        }
    }
}

void PrintGnuplottableEnbListToFile (uint64_t m_startTime)
{
    uint64_t timestamp = m_startTime + (uint64_t)Simulator::Now ().GetMilliSeconds ();
    curr_total_energy_consumption = 0;
    int mmnode_iterator = 0;
    for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it) {
        Ptr<Node> node = *it;
        for (int j = 0; j < (int)node->GetNDevices (); j++) {
            Ptr<LteEnbNetDevice>    enbd = node->GetDevice (j)->GetObject<LteEnbNetDevice> ();
            Ptr<MmWaveEnbNetDevice> mmd  = node->GetDevice (j)->GetObject<MmWaveEnbNetDevice> ();
            if (enbd) {
                Vector pos = node->GetObject<MobilityModel>()->GetPosition ();
                std::ofstream f ("enbs.txt", std::ios_base::out | std::ios_base::app);
                f << timestamp << "," << enbd->GetCellId () << "," << pos.x << ","
                  << pos.y << "," << m_startTime << ",0,30\n";
            } else if (mmd) {
                Vector pos = node->GetObject<MobilityModel>()->GetPosition ();
                std::ofstream f ("gnbs.txt", std::ios_base::out | std::ios_base::app);
                auto ueMap = mmd->GetUeMap ();
                for (const auto& ue : ueMap)
                    ue_assoc_list[ue.second->GetImsi () - 1] = mmd->GetCellId ();
                uint16_t cid = mmd->GetCellId ();
                Ptr<MmWaveEnbPhy> phy = node->GetDevice (j)
                    ->GetObject<MmWaveEnbNetDevice>()->GetPhy ();
                esON_list[cid] = (phy->GetTxPower () == 0);
                curr_total_energy_consumption += current_energy_consumption[cid];
                f << timestamp << "," << cid << "," << pos.x << "," << pos.y << ","
                  << m_startTime << "," << esON_list[cid] << ","
                  << current_energy_consumption[cid] << ","
                  << max_energy_consumption << ","
                  << sum_curr_total_energy_consumption << "\n";
                mmnode_iterator++;
            }
        }
    }
    if (mmnode_iterator == num_of_mmdev)
        sum_curr_total_energy_consumption = curr_total_energy_consumption;
    if (mmnode_iterator == num_of_mmdev &&
        max_energy_consumption < curr_total_energy_consumption)
        max_energy_consumption = curr_total_energy_consumption;
}

void ClearFile (std::string Filename, uint64_t m_startTime)
{
    { std::ofstream f (Filename, std::ios_base::out | std::ios_base::trunc); }
    uint64_t ts = m_startTime + (uint64_t)Simulator::Now ().GetMilliSeconds ();
    std::ofstream f (Filename, std::ios_base::out | std::ios_base::app);
    if (Filename == "ue_position.txt")
        f << "timestamp,id,x,y,type,cell,simid\n";
    else if (Filename == "gnbs.txt") {
        f << "timestamp,id,x,y,simid,ESstate,currEC,maxEC,totalcurrEC\n";
        f << ts << ",0," << maxXAxis << "," << maxYAxis << "\n";
    } else if (Filename == "enbs.txt")
        f << "timestamp,id,x,y,simid,ESstate,power\n";
}

void PrintPosition (Ptr<Node> node, int iterator, std::string Filename, uint64_t m_startTime)
{
    uint64_t ts = m_startTime + (uint64_t)Simulator::Now ().GetMilliSeconds ();
    Ptr<Node> node1 = NodeList::GetNode (iterator);
    for (int j = 0; j < (int)node->GetNDevices (); j++) {
        Ptr<McUeNetDevice> mcu = node1->GetDevice (j)->GetObject<McUeNetDevice> ();
        if (mcu) {
            int imsi = (int)mcu->GetImsi ();
            int sc   = ue_assoc_list[imsi - 1];
            if (sc == 0) sc = 1;
            Vector pos = node->GetObject<MobilityModel>()->GetPosition ();
            NS_LOG_UNCOND ("UE " << imsi << " @ (" << pos.x << "," << pos.y
                           << ") t=" << Simulator::Now ().GetSeconds ()
                           << "s Cell=" << sc);
            std::ofstream f (Filename, std::ios_base::out | std::ios_base::app);
            f << ts << "," << imsi << "," << pos.x << "," << pos.y
              << ",mc," << sc << "," << m_startTime << "\n";
        }
    }
}

void EnergyConsumptionUpdate (int nodeIndex, std::string filename,
                               double totaloldEC, double totalnewEC)
{
    std::ofstream f (filename, std::ios_base::out | std::ios_base::app);
    f << Simulator::Now ().GetSeconds () << "," << totalnewEC << ","
      << (totalnewEC - totaloldEC) << "\n";
    totalnewEnergyConsumption_storage[nodeIndex] = totalnewEC;
}

void EnergyConsumptionPrint (int nodeIndex)
{
    NS_LOG_UNCOND ("Energy cell " << nodeIndex+2 << ": "
                   << totalnewEnergyConsumption_storage[nodeIndex] << " J at "
                   << Simulator::Now ().GetSeconds () << "s");
    current_energy_consumption[nodeIndex] =
        totalnewEnergyConsumption_storage[nodeIndex] -
        totaloldEnergyConsumption_storage[nodeIndex];
    totaloldEnergyConsumption_storage[nodeIndex] =
        totalnewEnergyConsumption_storage[nodeIndex];
}

// ═══════════════════════════════════════════════════════════════════════
//  GLOBAL VALUES  (original, unchanged — preserves RIC compatibility)
// ═══════════════════════════════════════════════════════════════════════

static ns3::GlobalValue g_bufferSize ("bufferSize", "RLC tx buffer size (MB)",
    ns3::UintegerValue (10), ns3::MakeUintegerChecker<uint32_t> ());
static ns3::GlobalValue g_enableTraces ("enableTraces", "Generate ns-3 traces",
    ns3::BooleanValue (true), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_e2lteEnabled ("e2lteEnabled", "Send LTE E2 reports",
    ns3::BooleanValue (true), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_e2nrEnabled ("e2nrEnabled", "Send NR E2 reports",
    ns3::BooleanValue (true), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_e2du ("e2du", "Send DU reports",
    ns3::BooleanValue (true), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_e2cuUp ("e2cuUp", "Send CU-UP reports",
    ns3::BooleanValue (true), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_e2cuCp ("e2cuCp", "Send CU-CP reports",
    ns3::BooleanValue (true), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_reducedPmValues ("reducedPmValues", "Subset of pm containers",
    ns3::BooleanValue (false), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_hoSinrDifference ("hoSinrDifference",
    "SINR diff [dB] for HO trigger; dense urban: 1.5",
    ns3::DoubleValue (1.5), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue g_indicationPeriodicity ("indicationPeriodicity",
    "E2 KPM indication periodicity [s]",
    ns3::DoubleValue (0.1), ns3::MakeDoubleChecker<double> (0.01, 2.0));
static ns3::GlobalValue g_simTime ("simTime", "Simulation time [s]",
    ns3::DoubleValue (600), ns3::MakeDoubleChecker<double> (0.1, 100000.0));
static ns3::GlobalValue g_outageThreshold ("outageThreshold",
    "SNR/SINR outage threshold [dB]",
    ns3::DoubleValue (-3.0), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue g_numberOfRaPreambles ("numberOfRaPreambles",
    "RA preambles for RACH contention",
    ns3::UintegerValue (40), ns3::MakeUintegerChecker<uint8_t> ());
static ns3::GlobalValue g_handoverMode ("handoverMode",
    "NoAuto=xApp decision; DynamicTtt/Threshold=scenario",
    ns3::StringValue ("NoAuto"), ns3::MakeStringChecker ());
static ns3::GlobalValue g_e2TermIp ("e2TermIp", "RIC E2 termination IP",
    ns3::StringValue ("127.0.0.1"), ns3::MakeStringChecker ());
static ns3::GlobalValue g_enableE2FileLogging ("enableE2FileLogging",
    "Offline CSV logging (no RIC)",
    ns3::BooleanValue (false), ns3::MakeBooleanChecker ());
static ns3::GlobalValue g_e2_func_id ("KPM_E2functionID", "KPM function ID",
    ns3::DoubleValue (2), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue g_rc_e2_func_id ("RC_E2functionID", "RC function ID",
    ns3::DoubleValue (3), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue g_controlFileName ("controlFileName", "Control file path",
    ns3::StringValue (""), ns3::MakeStringChecker ());
static ns3::GlobalValue mmWave_nodes ("N_MmWaveEnbNodes", "Number of mmWave gNBs",
    ns3::UintegerValue (NUM_MMWAVE_ENBS), ns3::MakeUintegerChecker<uint8_t> ());
static ns3::GlobalValue ue_s ("N_Ues", "Number of UEs",
    ns3::UintegerValue (NUM_UES), ns3::MakeUintegerChecker<uint32_t> ());
static ns3::GlobalValue center_freq ("CenterFrequency", "Center frequency [Hz]",
    ns3::DoubleValue (3.5e9), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue bandwidth_value ("Bandwidth", "Channel bandwidth [Hz]",
    ns3::DoubleValue (20e6), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue isd_ue_gv ("IntersideDistanceUEs",
    "UE distribution radius [m]",
    ns3::DoubleValue (800), ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue isd_cell_gv ("IntersideDistanceCells",
    "ISD between gNBs [m]",
    ns3::DoubleValue (ISD_CELLS), ns3::MakeDoubleChecker<double> ());

// ═══════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════

int main (int argc, char* argv[])
{
    LogComponentEnableAll (LOG_PREFIX_ALL);
    LogComponentEnable ("KpmIndication", LOG_LEVEL_INFO);

    maxXAxis = AREA_X;
    maxYAxis = AREA_Y;

    CommandLine cmd;
    cmd.Parse (argc, argv);

    bool harqEnabled = true;

    UintegerValue uv; BooleanValue bv; StringValue sv; DoubleValue dv;

    GlobalValue::GetValueByName ("hoSinrDifference",    dv);  double hoSinrDifference = dv.Get ();
    GlobalValue::GetValueByName ("bufferSize",          uv);  uint32_t bufferSize     = uv.Get ();
    GlobalValue::GetValueByName ("enableTraces",        bv);  bool enableTraces       = bv.Get ();
    GlobalValue::GetValueByName ("outageThreshold",     dv);  double outageThreshold  = dv.Get ();
    GlobalValue::GetValueByName ("handoverMode",        sv);  std::string handoverMode = sv.Get ();
    GlobalValue::GetValueByName ("e2TermIp",            sv);  std::string e2TermIp    = sv.Get ();
    GlobalValue::GetValueByName ("enableE2FileLogging", bv);  bool enableE2FileLogging = bv.Get ();
    GlobalValue::GetValueByName ("KPM_E2functionID",    dv);  double kpm_func_id      = dv.Get ();
    GlobalValue::GetValueByName ("RC_E2functionID",     dv);  double rc_func_id       = dv.Get ();
    GlobalValue::GetValueByName ("numberOfRaPreambles", uv);  uint8_t nRaPreambles    = uv.Get ();
    GlobalValue::GetValueByName ("e2lteEnabled",        bv);  bool e2lteEnabled       = bv.Get ();
    GlobalValue::GetValueByName ("e2nrEnabled",         bv);  bool e2nrEnabled        = bv.Get ();
    GlobalValue::GetValueByName ("e2du",                bv);  bool e2du               = bv.Get ();
    GlobalValue::GetValueByName ("e2cuUp",              bv);  bool e2cuUp             = bv.Get ();
    GlobalValue::GetValueByName ("e2cuCp",              bv);  bool e2cuCp             = bv.Get ();
    GlobalValue::GetValueByName ("reducedPmValues",     bv);  bool reducedPmValues    = bv.Get ();
    GlobalValue::GetValueByName ("indicationPeriodicity",dv); double indicationPeriodicity = dv.Get ();
    GlobalValue::GetValueByName ("controlFileName",     sv);  std::string controlFilename = sv.Get ();
    GlobalValue::GetValueByName ("simTime",             dv);  double simTime          = dv.Get ();
    GlobalValue::GetValueByName ("Bandwidth",           dv);  double bandwidth        = dv.Get ();
    GlobalValue::GetValueByName ("CenterFrequency",     dv);  double centerFrequency  = dv.Get ();

    NS_LOG_UNCOND ("=== Load Balancing xApp Scenario ===");
    NS_LOG_UNCOND ("  Topology : 1 LTE eNB (ctrl) + 7 mmWave gNBs | 20 UEs");
    NS_LOG_UNCOND ("  Hot-spot : Cell 2 (center) — 8 UEs — FTP+Video+VoIP+HTTP");
    NS_LOG_UNCOND ("  E2 IP    : " << e2TermIp << " | KPM=" << kpm_func_id
                   << " RC=" << rc_func_id);
    NS_LOG_UNCOND ("  simTime  : " << simTime << "s | period=" << indicationPeriodicity << "s");

    // ── ns-3 Config (all original settings preserved) ─────────────────
    Config::SetDefault ("ns3::LteEnbNetDevice::ControlFileName",  StringValue (controlFilename));
    Config::SetDefault ("ns3::LteEnbNetDevice::E2Periodicity",    DoubleValue (indicationPeriodicity));
    Config::SetDefault ("ns3::MmWaveEnbNetDevice::E2Periodicity", DoubleValue (indicationPeriodicity));
    Config::SetDefault ("ns3::MmWaveHelper::E2ModeLte",           BooleanValue (e2lteEnabled));
    Config::SetDefault ("ns3::MmWaveHelper::E2ModeNr",            BooleanValue (e2nrEnabled));
    Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableDuReport",  BooleanValue (e2du));
    Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableCuUpReport",BooleanValue (e2cuUp));
    Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuUpReport",   BooleanValue (e2cuUp));
    Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableCuCpReport",BooleanValue (e2cuCp));
    Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuCpReport",   BooleanValue (e2cuCp));
    Config::SetDefault ("ns3::MmWaveEnbNetDevice::ReducedPmValues", BooleanValue (reducedPmValues));
    Config::SetDefault ("ns3::LteEnbNetDevice::ReducedPmValues",    BooleanValue (reducedPmValues));
    Config::SetDefault ("ns3::LteEnbNetDevice::EnableE2FileLogging",   BooleanValue (enableE2FileLogging));
    Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableE2FileLogging",BooleanValue (enableE2FileLogging));
    Config::SetDefault ("ns3::LteEnbNetDevice::KPM_E2functionID",   DoubleValue (kpm_func_id));
    Config::SetDefault ("ns3::MmWaveEnbNetDevice::KPM_E2functionID",DoubleValue (kpm_func_id));
    Config::SetDefault ("ns3::LteEnbNetDevice::RC_E2functionID",    DoubleValue (rc_func_id));
    Config::SetDefault ("ns3::MmWaveEnbMac::NumberOfRaPreambles",   UintegerValue (nRaPreambles));
    Config::SetDefault ("ns3::MmWaveHelper::HarqEnabled",           BooleanValue (harqEnabled));
    Config::SetDefault ("ns3::MmWaveHelper::UseIdealRrc",           BooleanValue (true));
    Config::SetDefault ("ns3::MmWaveHelper::E2TermIp",              StringValue (e2TermIp));
    Config::SetDefault ("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled",BooleanValue (harqEnabled));
    Config::SetDefault ("ns3::MmWavePhyMacCommon::NumHarqProcess",  UintegerValue (100));
    Config::SetDefault ("ns3::PhasedArrayModel::AntennaElement",
        PointerValue (CreateObject<IsotropicAntennaModel> ()));
    Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
        TimeValue (MilliSeconds (100)));  // 100ms: balanced — fading varies between KPM periods
    Config::SetDefault ("ns3::ThreeGppChannelConditionModel::UpdatePeriod",
        TimeValue (MilliSeconds (100)));  // 100ms: LoS/NLoS re-evaluated each reporting period
    Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer",      TimeValue (MilliSeconds (10)));
    Config::SetDefault ("ns3::LteRlcUmLowLat::ReportBufferStatusTimer",TimeValue (MilliSeconds (10)));
    Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize",
        UintegerValue (bufferSize * 1024 * 1024));
    Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize",
        UintegerValue (bufferSize * 1024 * 1024));
    Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize",
        UintegerValue (bufferSize * 1024 * 1024));
    Config::SetDefault ("ns3::LteEnbRrc::OutageThreshold",        DoubleValue (outageThreshold));
    Config::SetDefault ("ns3::LteEnbRrc::SecondaryCellHandoverMode",StringValue (handoverMode));
    Config::SetDefault ("ns3::LteEnbRrc::HoSinrDifference",       DoubleValue (hoSinrDifference));
    // Delay initial mmWave cell selection until Kalman-filter measurements stabilise.
    // The PHY transient window is 320 ms (200 samples at 1.6 ms/sample); using 500 ms
    // ensures 112 post-transient filtered samples are in the SINR map before the
    // first TriggerUeAssociationUpdate fires, giving a stable, distance-correlated
    // cell assignment that matches the xApp's path-loss RSRP ranking.
    Config::SetDefault ("ns3::LteEnbRrc::InitialAssociationDelay",
                        TimeValue (MilliSeconds (500)));
    Config::SetDefault ("ns3::ThreeGppPropagationLossModel::Frequency",  DoubleValue (3.5e9));
    Config::SetDefault ("ns3::ThreeGppPropagationLossModel::ShadowingEnabled",BooleanValue (true)); // 3GPP log-normal shadow fading enabled

    // Antenna / PHY
    Config::SetDefault ("ns3::McUeNetDevice::AntennaNum",     UintegerValue (1));
    Config::SetDefault ("ns3::MmWaveNetDevice::AntennaNum",   UintegerValue (1));
    Config::SetDefault ("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue (bandwidth));
    Config::SetDefault ("ns3::MmWavePhyMacCommon::CenterFreq",DoubleValue (centerFrequency));
    // Realistic SINR measurement noise: adds Gaussian perturbation to reported SINR
    // so KPM values are not perfectly noiseless (mirrors real UE measurement errors)
    Config::SetDefault ("ns3::MmWaveEnbPhy::NoiseAndFilter",  BooleanValue (true));

    // Reproducible random seed (dataset generation)
    RngSeedManager::SetSeed (42);
    RngSeedManager::SetRun (1);

    // ── mmWave + EPC ──────────────────────────────────────────────────
    Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper> ();
    mmwaveHelper->SetPathlossModelType (
        "ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
    mmwaveHelper->SetChannelConditionModelType (
        "ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

    Ptr<MmWavePointToPointEpcHelper> epcHelper =
        CreateObject<MmWavePointToPointEpcHelper> ();
    mmwaveHelper->SetEpcHelper (epcHelper);

    // ── Remote host + P2P internet ────────────────────────────────────
    Ptr<Node> pgw = epcHelper->GetPgwNode ();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create (1);
    Ptr<Node> remoteHost = remoteHostContainer.Get (0);
    g_remoteHost = remoteHost;
    InternetStackHelper internet;
    internet.Install (remoteHostContainer);

    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
    p2ph.SetDeviceAttribute ("Mtu",      UintegerValue (2500));
    p2ph.SetChannelAttribute ("Delay",   TimeValue (Seconds (0.010)));
    NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
    Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress (1);

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
    remoteHostStaticRouting->AddNetworkRouteTo (
        Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

    // ── Node containers ───────────────────────────────────────────────
    NodeContainer ueNodes, mmWaveEnbNodes, lteEnbNodes, allEnbNodes;
    mmWaveEnbNodes.Create (NUM_MMWAVE_ENBS);
    lteEnbNodes.Create (NUM_LTE_ENBS);
    ueNodes.Create (NUM_UES);
    allEnbNodes.Add (lteEnbNodes);
    allEnbNodes.Add (mmWaveEnbNodes);
    NodeContainerManager::GetInstance ().SetMmWaveEnbNodes (mmWaveEnbNodes);

    // ── gNB placement (hexagonal topology) ────────────────────────────
    // allEnbNodes order: [LTE(0)] [mmWave(0..6)]
    //   LTE  = Cell 1  @ center (1000,1000)
    //   mmWave[0] = Cell 2 @ center (1000,1000) — co-located, OVERLOADED
    //   mmWave[1..6]= Cells 3–8 @ hex ring ISD=300m

    Vector centerPos (1000.0, 1000.0, 10.0);  // 10 m: 3GPP TR 38.901 UMi hBS minimum
    Ptr<ListPositionAllocator> enbAlloc = CreateObject<ListPositionAllocator> ();
    enbAlloc->Add (centerPos);   // LTE Cell 1
    enbAlloc->Add (centerPos);   // mmWave Cell 2 (co-located with LTE)

    // Cells 3–8: hexagonal ring, angles 0°,60°,120°,180°,240°,300°
    // Order matches user's cell numbering:
    //   angle 0°  → (1300,1000)  = Cell 3
    //   angle 60° → (1150,1259.8)= Cell 4
    //   angle 120°→ (850,1259.8) = Cell 5
    //   angle 180°→ (700,1000)   = Cell 6
    //   angle 240°→ (850,740.2)  = Cell 7
    //   angle 300°→ (1150,740.2) = Cell 8
    for (int i = 0; i < 6; ++i) {
        double angle = (2.0 * M_PI * i) / 6.0;
        enbAlloc->Add (Vector (
            centerPos.x + ISD_CELLS * std::cos (angle),
            centerPos.y + ISD_CELLS * std::sin (angle),
            10.0));  // 10 m: 3GPP TR 38.901 UMi hBS minimum
    }

    MobilityHelper enbMobility;
    enbMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    enbMobility.SetPositionAllocator (enbAlloc);
    enbMobility.Install (allEnbNodes);

    // ── UE placement from specification ───────────────────────────────
    Ptr<ListPositionAllocator> ueAlloc = CreateObject<ListPositionAllocator> ();
    for (uint32_t i = 0; i < NUM_UES; ++i) {
        // Clamp all positions to bounded box [500,1500]
        double px = std::max (BOUND_MIN, std::min (BOUND_MAX, g_ue_init_pos[i][0]));
        double py = std::max (BOUND_MIN, std::min (BOUND_MAX, g_ue_init_pos[i][1]));
        ueAlloc->Add (Vector (px, py, 1.5));
        NS_LOG_UNCOND ("[UE-INIT] UE " << (i+1) << " pos=(" << px << "," << py
                       << ") speed=" << g_ue_speed_mps[i] << " m/s"
                       << " service=" << (int)g_ue_service[i]);
    }

    // ── UE mobility: ConstantVelocity + boundary reflection + random turns ─
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel ("ns3::ConstantVelocityMobilityModel");
    ueMobility.SetPositionAllocator (ueAlloc);
    ueMobility.Install (ueNodes);

    // Shared RNG for angle perturbations (0..1 uniform)
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();
    rng->SetAttribute ("Min", DoubleValue (0.0));
    rng->SetAttribute ("Max", DoubleValue (1.0));

    for (uint32_t i = 0; i < ueNodes.GetN (); ++i) {
        Ptr<ConstantVelocityMobilityModel> mob =
            ueNodes.Get (i)->GetObject<ConstantVelocityMobilityModel> ();
        if (!mob) continue;

        // Set initial random direction at each UE's rated speed
        double angle = rng->GetValue () * 2.0 * M_PI;
        double speed = g_ue_speed_mps[i];
        mob->SetVelocity (Vector (speed * std::cos (angle),
                                  speed * std::sin (angle), 0.0));

        // Schedule boundary enforcement (every BOUNDARY_CHECK_S)
        Simulator::Schedule (Seconds (BOUNDARY_CHECK_S),
                             &BoundedMobilityEnforce, i, ueNodes.Get (i), rng);

        // Schedule first random direction change
        double firstChange = DIR_CHANGE_MIN_S +
                             (DIR_CHANGE_MAX_S - DIR_CHANGE_MIN_S) * rng->GetValue ();
        Simulator::Schedule (Seconds (firstChange),
                             &BoundedMobilityDirectionChange, i, ueNodes.Get (i), rng);
    }

    NS_LOG_UNCOND ("[MOBILITY] 20 UEs: ConstantVelocity + boundary reflection [500,1500]m");
    NS_LOG_UNCOND ("           Pedestrian (1.2–1.5 m/s) / Fast-walker (2–2.8) / "
                   "Cyclist (4.5–6) / Car (8.3–16.7 m/s)");

    // ── Install devices ───────────────────────────────────────────────
    NetDeviceContainer lteEnbDevs    = mmwaveHelper->InstallLteEnbDevice (lteEnbNodes);
    NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice (mmWaveEnbNodes);
    NetDeviceContainer mcUeDevs      = mmwaveHelper->InstallMcUeDevice (ueNodes);

    // Set global pointers for RSRP/PRB updater
    g_mmWaveEnbNodes_ptr = &mmWaveEnbNodes;
    g_ueNodes_ptr        = &ueNodes;
    g_mmWaveEnbDevs_ptr  = &mmWaveEnbDevs;

    // ── IP stack + X2 + attach ────────────────────────────────────────
    internet.Install (ueNodes);
    mmwaveHelper->AddX2Interface (lteEnbNodes, mmWaveEnbNodes);
    mmwaveHelper->AttachToClosestEnb (mcUeDevs, mmWaveEnbDevs, lteEnbDevs);

    Ipv4InterfaceContainer ueIpIface =
        epcHelper->AssignUeIpv4Address (NetDeviceContainer (mcUeDevs));

    NS_LOG_UNCOND ("EPC: " << ueIpIface.GetN () << " IPs for " << ueNodes.GetN () << " UEs");
    for (uint32_t u = 0; u < ueIpIface.GetN (); ++u)
        NS_LOG_UNCOND ("  UE[" << u << "] IP=" << ueIpIface.GetAddress (u));

    // Default gateway for each UE
    for (uint32_t u = 0; u < ueNodes.GetN (); ++u) {
        Ptr<Ipv4StaticRouting> sr =
            ipv4RoutingHelper.GetStaticRouting (
                ueNodes.Get (u)->GetObject<Ipv4> ());
        sr->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

    // ── Energy model ──────────────────────────────────────────────────
    BasicEnergySourceHelper energySrcHelper;
    energySrcHelper.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (1e12));
    energySrcHelper.Set ("BasicEnergySupplyVoltageV",       DoubleValue (5.0));
    energy::EnergySourceContainer sources =
        energySrcHelper.Install (mmWaveEnbNodes);
    MmWaveRadioEnergyModelEnbHelper nrEnbHelper;
    energy::DeviceEnergyModelContainer deviceEModel =
        nrEnbHelper.Install (mmWaveEnbDevs, sources);

    int numPrints = (int)(simTime / 0.1);

    for (int x = 0; x < NUM_MMWAVE_ENBS; ++x) {
        std::ostringstream fn;
        fn << "energyfilecell" << x + 2 << ".csv";
        { std::ofstream f (fn.str (), std::ios_base::out | std::ios_base::trunc);
          f << "Time,NetEnergy,DiffEnergy\n"; }
        deviceEModel.Get (x)->TraceConnectWithoutContext (
            "TotalEnergyConsumption",
            MakeBoundCallback (&EnergyConsumptionUpdate, x, fn.str ()));
        for (int i = 0; i < numPrints; i++)
            Simulator::Schedule (Seconds (i * simTime / numPrints),
                                 &EnergyConsumptionPrint, x);
    }

    // ═══════════════════════════════════════════════════════════════════
    //  TRAFFIC APPLICATIONS — mixed services for realistic PRB spread
    //  ─────────────────────────────────────────────────────────────────
    //  eMBB/FTP → BulkSend TCP, continuous high PRB
    //  Video    → UDP CBR 2 Mbps, steady medium PRB
    //  VoIP     → UDP 64 kbps G.711 codec model, very low PRB
    //             (shows PRB-load ≠ UE-count for Cell 2 with mixed services)
    //  HTTP     → TCP OnOff bursty, variable PRB
    // ═══════════════════════════════════════════════════════════════════
    NS_LOG_UNCOND ("=== INSTALLING MIXED-SERVICE TRAFFIC ===");

    ApplicationContainer sinkApp, clientApp;
    uint16_t dlPort_ftp   = 20;
    uint16_t dlPort_video = 1234;
    uint16_t dlPort_voip  = 5060;
    uint16_t dlPort_http  = 80;
    uint16_t ulBasePort   = 60000;

    g_ueToUlSinkIndex.resize (ueNodes.GetN (), -1);
    uint32_t remoteUlIdx = 0;

    for (uint32_t u = 0; u < ueNodes.GetN (); ++u) {
        ServiceType svc = g_ue_service[u];

        // ── DL sink on UE ─────────────────────────────────────────────
        uint16_t sinkPort;
        std::string sockFact;
        switch (svc) {
            case EMBB_FTP: sinkPort = dlPort_ftp;   sockFact = "ns3::TcpSocketFactory"; break;
            case VIDEO:    sinkPort = dlPort_video;  sockFact = "ns3::UdpSocketFactory"; break;
            case VOIP:     sinkPort = dlPort_voip;   sockFact = "ns3::UdpSocketFactory"; break;
            case HTTP:     sinkPort = dlPort_http;   sockFact = "ns3::TcpSocketFactory"; break;
        }
        PacketSinkHelper dlSink (sockFact,
            InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
        sinkApp.Add (dlSink.Install (ueNodes.Get (u)));

        // ── Validate IP ───────────────────────────────────────────────
        if (u >= ueIpIface.GetN ()) {
            NS_LOG_UNCOND ("  [SKIP] UE[" << u << "] — no IP");
            continue;
        }
        Ipv4Address ueAddr = ueIpIface.GetAddress (u);
        if (ueAddr == Ipv4Address ("0.0.0.0") || ueAddr == Ipv4Address ()) {
            NS_LOG_UNCOND ("  [SKIP] UE[" << u << "] — invalid IP");
            continue;
        }

        // ── DL client on remoteHost → UE ─────────────────────────────
        switch (svc) {
            case EMBB_FTP: {
                // FTP: large-file bulk transfer, high continuous PRB
                BulkSendHelper dl ("ns3::TcpSocketFactory",
                    InetSocketAddress (ueAddr, sinkPort));
                dl.SetAttribute ("SendSize",  UintegerValue (1460));  // full TCP segment
                dl.SetAttribute ("MaxBytes",  UintegerValue (0));     // unlimited
                clientApp.Add (dl.Install (remoteHost));
                break;
            }
            case VIDEO: {
                // Video streaming: UDP CBR 2 Mbps (1400B @ 714 µs interval)
                UdpClientHelper dl (ueAddr, sinkPort);
                dl.SetAttribute ("Interval",    TimeValue (MicroSeconds (714)));
                dl.SetAttribute ("PacketSize",  UintegerValue (1400));
                dl.SetAttribute ("MaxPackets",  UintegerValue (UINT32_MAX));
                clientApp.Add (dl.Install (remoteHost));
                break;
            }
            case VOIP: {
                // VoIP: G.711 model — 160B @ 20ms (64 kbps) — LOW PRB
                UdpClientHelper dl (ueAddr, sinkPort);
                dl.SetAttribute ("Interval",    TimeValue (MilliSeconds (20)));
                dl.SetAttribute ("PacketSize",  UintegerValue (160));
                dl.SetAttribute ("MaxPackets",  UintegerValue (UINT32_MAX));
                clientApp.Add (dl.Install (remoteHost));
                break;
            }
            case HTTP: {
                // HTTP: TCP OnOff — bursty page requests (mean On=0.3s, Off=2s)
                OnOffHelper dl ("ns3::TcpSocketFactory",
                    InetSocketAddress (ueAddr, sinkPort));
                dl.SetAttribute ("OnTime",   StringValue (
                    "ns3::ExponentialRandomVariable[Mean=0.3]"));
                dl.SetAttribute ("OffTime",  StringValue (
                    "ns3::ExponentialRandomVariable[Mean=2.0]"));
                dl.SetAttribute ("DataRate", DataRateValue (DataRate ("1Mbps")));
                dl.SetAttribute ("PacketSize", UintegerValue (1460));
                clientApp.Add (dl.Install (remoteHost));
                break;
            }
        }

        // ── UL: lightweight UDP uplink for all UE types ───────────────
        // Varied packet rates by service (reflects realistic uplink asymmetry)
        g_ueToUlSinkIndex[u] = (int)remoteUlIdx++;
        uint16_t ulPort = ulBasePort + u;
        PacketSinkHelper ulSink ("ns3::UdpSocketFactory",
            InetSocketAddress (Ipv4Address::GetAny (), ulPort));
        sinkApp.Add (ulSink.Install (remoteHost));

        UdpClientHelper ul (remoteHostAddr, ulPort);
        switch (svc) {
            case EMBB_FTP:
                ul.SetAttribute ("Interval",   TimeValue (MicroSeconds (2000)));  // 500 kbps UL ACKs approximation
                ul.SetAttribute ("PacketSize", UintegerValue (512));
                break;
            case VIDEO:
                ul.SetAttribute ("Interval",   TimeValue (MilliSeconds (100)));   // feedback / RTCP
                ul.SetAttribute ("PacketSize", UintegerValue (64));
                break;
            case VOIP:
                ul.SetAttribute ("Interval",   TimeValue (MilliSeconds (20)));    // symmetric VoIP
                ul.SetAttribute ("PacketSize", UintegerValue (160));
                break;
            case HTTP:
                ul.SetAttribute ("Interval",   TimeValue (MilliSeconds (500)));   // HTTP GET requests
                ul.SetAttribute ("PacketSize", UintegerValue (128));
                break;
        }
        ul.SetAttribute ("MaxPackets", UintegerValue (UINT32_MAX));
        clientApp.Add (ul.Install (ueNodes.Get (u)));

        NS_LOG_UNCOND ("  [OK] UE[" << u << "] svc=" << (int)svc
                       << " speed=" << g_ue_speed_mps[u] << " m/s"
                       << " IP=" << ueAddr);
    }

    // ── Always-on background DL stream (interference floor realism) ───────
    // Real 5G cells never go fully idle: SSB, CSI-RS, PDCCH overhead and
    // background UE activity ensure the scheduler always has data.  In ns-3
    // mmWave the DL-CTRL frame is excluded from interference accumulation, so
    // we add a small constant UDP CBR flow per UE (500 kbps) to guarantee
    // every gNB is always scheduling on at least a few PRBs.  This forces the
    // simulation into the interference-limited regime that real deployments
    // operate in, without changing any UE position or cell layout.
    {
        const uint16_t BG_PORT_BASE = 9000;
        const uint32_t BG_PKT_B    = 1280;                 // bytes per packet
        const uint32_t BG_ITV_US   = 20480;                // 1280 B @ ~500 kbps
        NS_LOG_UNCOND ("=== INSTALLING BACKGROUND INTERFERENCE STREAMS (500 kbps/UE) ===");
        for (uint32_t u = 0; u < ueNodes.GetN (); ++u) {
            if (u >= ueIpIface.GetN ()) continue;
            Ipv4Address ueAddr = ueIpIface.GetAddress (u);
            if (ueAddr == Ipv4Address ("0.0.0.0") || ueAddr == Ipv4Address ()) continue;

            uint16_t bgPort = BG_PORT_BASE + (uint16_t)u;

            PacketSinkHelper bgSink ("ns3::UdpSocketFactory",
                InetSocketAddress (Ipv4Address::GetAny (), bgPort));
            sinkApp.Add (bgSink.Install (ueNodes.Get (u)));

            UdpClientHelper bgDl (ueAddr, bgPort);
            bgDl.SetAttribute ("Interval",   TimeValue (MicroSeconds (BG_ITV_US)));
            bgDl.SetAttribute ("PacketSize", UintegerValue (BG_PKT_B));
            bgDl.SetAttribute ("MaxPackets", UintegerValue (UINT32_MAX));
            // Start after RRC attachment; stop with main traffic
            Ptr<Application> bgApp = bgDl.Install (remoteHost).Get (0);
            bgApp->SetStartTime (Seconds (0.5));
            bgApp->SetStopTime  (Seconds (simTime - 0.1));
        }
        NS_LOG_UNCOND ("  [BG] " << ueNodes.GetN ()
                       << " background streams installed (port "
                       << BG_PORT_BASE << "–"
                       << (BG_PORT_BASE + ueNodes.GetN () - 1) << ")");
    }

    // ── Trace DL/UL for sliding-window bitrate ────────────────────────
    for (uint32_t u = 0; u < ueNodes.GetN () && u < sinkApp.GetN (); ++u) {
        Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApp.Get (u));
        if (sink)
            sink->TraceConnectWithoutContext (
                "Rx", MakeBoundCallback (&DlRxCb, (uint64_t)(u + 1)));
    }

    // UL client Tx traces (clients follow DL then UL in clientApp)
    uint32_t cIdx = 0;
    for (uint32_t u = 0; u < ueNodes.GetN (); ++u) {
        if (u >= ueIpIface.GetN ()) continue;
        if (ueIpIface.GetAddress (u) == Ipv4Address ("0.0.0.0")) continue;
        if (cIdx < clientApp.GetN ()) {
            // DL client
            Ptr<UdpClient>         udp  = DynamicCast<UdpClient>         (clientApp.Get (cIdx));
            Ptr<BulkSendApplication> bk = DynamicCast<BulkSendApplication>(clientApp.Get (cIdx));
            Ptr<OnOffApplication>  oof  = DynamicCast<OnOffApplication>   (clientApp.Get (cIdx));
            if (udp) udp->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t)(u+1)));
            else if (bk) bk->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t)(u+1)));
            else if (oof) oof->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t)(u+1)));
            cIdx++;
        }
    }
    for (uint32_t u = 0; u < ueNodes.GetN (); ++u) {
        if (u >= ueIpIface.GetN ()) continue;
        if (ueIpIface.GetAddress (u) == Ipv4Address ("0.0.0.0")) continue;
        if (cIdx < clientApp.GetN ()) {
            Ptr<UdpClient> ul = DynamicCast<UdpClient> (clientApp.Get (cIdx));
            if (ul) ul->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t)(u+1)));
            cIdx++;
        }
    }

    // ── Application start / stop timing ──────────────────────────────
    sinkApp.Start (Seconds (0));
    sinkApp.Stop  (Seconds (simTime));
    for (uint32_t u = 0; u < clientApp.GetN (); ++u) {
        // Staggered start: 200ms base + 50ms/UE ensures RRC attachment completes
        double startDelay = 0.2 + (u * 0.05);
        clientApp.Get (u)->SetStartTime (Seconds (startDelay));
        clientApp.Get (u)->SetStopTime  (Seconds (simTime - 0.1));
    }

    // ── GUI visualizer files ──────────────────────────────────────────
    struct timeval time_now{};
    gettimeofday (&time_now, nullptr);
    uint64_t t_startTime = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
    ClearFile ("ue_position.txt", t_startTime);
    ClearFile ("enbs.txt",        t_startTime);
    ClearFile ("gnbs.txt",        t_startTime);
    PrintGnuplottableUeListToFile ("ues.txt");

    int nodecount    = (int)NodeList::GetNNodes ();
    int UE_iterator  = nodecount - (int)NUM_UES;

    NS_LOG_UNCOND ("GUI: http://127.0.0.1:8000/");

    for (int i = 0; i < numPrints; i++) {
        Simulator::Schedule (Seconds (i * simTime / numPrints),
                             &PrintGnuplottableEnbListToFile, t_startTime);
        for (uint32_t j = 0; j < ueNodes.GetN (); j++)
            Simulator::Schedule (Seconds (i * simTime / numPrints),
                                 &PrintPosition, ueNodes.Get (j),
                                 j + UE_iterator, "ue_position.txt", t_startTime);
    }

    if (enableTraces) mmwaveHelper->EnableTraces ();

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
    lteHelper->Initialize ();
    lteHelper->EnablePhyTraces ();
    lteHelper->EnableMacTraces ();

    // ── Open CSV files ────────────────────────────────────────────────
    {
        const char* home = std::getenv ("HOME");
        std::string base = (home && home[0]) ? std::string (home) + "/" : "";

        // Handover log
        g_handover_log.open (base + "handover.csv", std::ios::out | std::ios::trunc);
        if (g_handover_log.is_open ())
            g_handover_log << "time_sec,ue_id,from_cell,to_cell,event,executed_ok\n";

        // LSTM CSV header
        {
            std::ofstream initCsv (base + "lstm_features.csv",
                std::ios::out | std::ios::trunc);
            if (initCsv.is_open ())
                initCsv << "Time,IMSI,Level,Qual,SNR,CQI,SecondCell_RSRP,SecondCell_SNR,"
                        << "NRxLev1,NQual1,Speed,DL_bitrate,UL_bitrate,BANDWIDTH,"
                        << "serving_cell,best_neigh_cell\n";
        }

        // LB Metrics CSV  ─────────────────────────────────────────────
        g_lbMetricsCsv.open (base + "lb_metrics.csv", std::ios::out | std::ios::trunc);
        if (g_lbMetricsCsv.is_open ()) {
            // Metric definitions in header for self-documentation
            g_lbMetricsCsv
                << "# Load Balancing xApp — 5 KPI Metrics\n"
                << "# avg_prb_load  : mean PRB utilization across active cells [0..1]\n"
                << "#                 (service-aware: VoIP < Video < FTP)\n"
                << "# avg_ue_load   : mean (#UEs_in_cell / " << (int)MAX_UES_PER_CELL
                << ") across active cells\n"
                << "# load_variance : variance of PRB load across cells (→0 = balanced)\n"
                << "# avg_throughput: mean (DL+UL) throughput per attached UE [Mbps]\n"
                << "# cdr           : call-drop rate = RLF_events / attached_UEs\n"
                << "time_sec,avg_prb_load,avg_ue_load,load_variance,"
                << "avg_throughput_mbps,cdr\n";
            g_lbMetricsHeaderWritten = true;
            NS_LOG_UNCOND ("[LB-METRICS] CSV: " << base << "lb_metrics.csv");
        }
    }

    // ── Connect callbacks (original RIC traces preserved) ─────────────
    Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                     MakeCallback (&OnHandoverStart));
    Config::Connect ("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                     MakeCallback (&OnHandoverSuccess));
    Config::ConnectFailSafe ("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverFailure",
                             MakeCallback (&OnHandoverFailure));

    // L3 SINR report → LSTM + KPM CSVs (original unchanged)
    for (uint32_t d = 0; d < mmWaveEnbDevs.GetN (); ++d) {
        Ptr<MmWaveEnbNetDevice> enbDev =
            DynamicCast<MmWaveEnbNetDevice> (mmWaveEnbDevs.Get (d));
        if (!enbDev) continue;
        enbDev->TraceConnectWithoutContext (
            "L3ReportUeSinr",
            MakeBoundCallback (&LstmCsvTraceCb, &ueNodes, bandwidth));
        uint32_t enbNodeId = enbDev->GetNode ()->GetId ();
        enbDev->TraceConnectWithoutContext (
            "L3ReportUeSinr",
            MakeBoundCallback (&KpmHandoverCsvCb, enbNodeId, &ueNodes, bandwidth));
    }
    for (uint32_t d = 0; d < mmWaveEnbDevs.GetN(); ++d) {
        Ptr<MmWaveEnbNetDevice> enbDev =
            DynamicCast<MmWaveEnbNetDevice>(mmWaveEnbDevs.Get(d));
        if (!enbDev) continue;
        enbDev->TraceConnectWithoutContext(
            "L3ReportNeighSinr",
            MakeCallback(&AllNeighborSinrCb));
    }
    // MAC scheduler PRB trace
    for (uint32_t d = 0; d < mmWaveEnbDevs.GetN (); ++d) {
        Ptr<MmWaveEnbNetDevice> enbDev =
            DynamicCast<MmWaveEnbNetDevice> (mmWaveEnbDevs.Get (d));
        if (!enbDev) continue;
        enbDev->GetMac ()->TraceConnectWithoutContext (
            "SchedulingTraceEnb",
            MakeBoundCallback (&SchedTraceCallback, enbDev));
    }

    // ── Periodic RSRP/PRB update ──────────────────────────────────────                                                            //rsrp
    auto UpdateAndWrite = []() {
    UpdateRealRsrpPrb();
    WriteRsrpSharedFile();
    WriteSinrSharedFile();
};

    Simulator::Schedule(Seconds(0.01), UpdateAndWrite);                                  //rsrp
    for (double t = indicationPeriodicity; t < simTime; t += indicationPeriodicity)
    Simulator::Schedule(Seconds(t), UpdateAndWrite);
    NS_LOG_UNCOND ("[REAL-KPM] UpdateRealRsrpPrb scheduled every "
                   << indicationPeriodicity << "s");

    // ── Periodic LB metrics logging ───────────────────────────────────
    // Log at same rate as indicationPeriodicity so xApp and metrics are aligned
    for (double t = indicationPeriodicity; t < simTime; t += indicationPeriodicity)
        Simulator::Schedule (Seconds (t), &LogLbMetrics);
    NS_LOG_UNCOND ("[LB-METRICS] Logging every " << indicationPeriodicity
                   << "s — 5 metrics: PRB_load, UE_load, Variance, Throughput, CDR");

    // ── Run ───────────────────────────────────────────────────────────
    NS_LOG_UNCOND ("Starting simulation (" << simTime << "s) ...");
    Simulator::Stop (Seconds (simTime));
    Simulator::Run ();

    // ── Cleanup ───────────────────────────────────────────────────────
    if (g_handover_log.is_open ())   g_handover_log.close ();
    if (g_lstmCsv.is_open ())        g_lstmCsv.close ();
    if (g_kpmHandoverCsv.is_open ()) g_kpmHandoverCsv.close ();
    if (g_lbMetricsCsv.is_open ())   g_lbMetricsCsv.close ();

    NS_LOG_UNCOND ("=== Simulation complete ===");
    NS_LOG_UNCOND ("Outputs:");
    NS_LOG_UNCOND ("  ~/kpm_handover_features.csv  (18-feature GRU dataset)");
    NS_LOG_UNCOND ("  ~/lstm_features.csv           (16-feature LSTM dataset)");
    NS_LOG_UNCOND ("  ~/handover.csv                (handover events log)");
    NS_LOG_UNCOND ("  ~/lb_metrics.csv              (5 LB KPIs — ready for plotting)");
    NS_LOG_UNCOND ("  ue_position.txt / gnbs.txt / enbs.txt (GUI visualizer)");

    Simulator::Destroy ();
    return 0;
}

