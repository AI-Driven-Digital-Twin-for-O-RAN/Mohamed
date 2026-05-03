/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/* *
 * Copyright (c) 2024 Orange Innovation Poland
 * Copyright (c) 2024 Orange Innovation Egypt
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Andrea Lacava <thecave003@gmail.com>
 *          Michele Polese <michele.polese@gmail.com>
 *          Argha Sen <arghasen10@gmail.com>
 *          Kamil Kociszewski <kamil.kociszewski@orange.com>
 *          Mostafa Ashraf <mostafa.ashraf.ext@orange.com>
 *
 * GRU handover optimization xApp alignment (dense urban, Sunway City–style):
 *   Model expects 18 features: SessionID, ElapsedTime, Node, CellID, Level (RSRP), Qual (RSRQ),
 *   SNR, CQI, SecondCell_RSRP, SecondCell_SNR, NRxLev1, NQual1, Speed, DL_bitrate, UL_bitrate,
 *   BANDWIDTH, Latitude, Longitude. Scenario: 2 km², 7 gNBs, 20 UEs, mixed mobility (pedestrian/
 *   shuttle/BRT) and traffic (FTP/Video/HTTP). Output: kpm_handover_features.csv + lstm_features.csv.
 *   KPM_E2functionID=2, RC_E2functionID=3; handover mode NoAuto (قرار من xApp); E2 indication 0.1 s.
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

// ============================================
// Bounded UE Mobility — box with random reflection
// Default: x,y ∈ [500, 1500]. UE 3, 12, 18 use tighter box.
// On boundary hit, velocity reflects with random perturbation.
// ============================================
static const double UE_BOUND_MIN = 500.0;
static const double UE_BOUND_MAX = 1500.0;
// Tighter bounds for UE 3, 12, 18 (SessionID / 1-based: 3, 12, 18 → indices 2, 11, 17)
static const double UE_TIGHT_MIN_X = 800.0;
static const double UE_TIGHT_MAX_X = 1200.0;
static const double UE_TIGHT_MIN_Y = 700.0;
static const double UE_TIGHT_MAX_Y = 1300.0;
static const double BOUNDARY_CHECK_INTERVAL_S = 0.05;
static const double DIR_CHANGE_MIN_S = 4.0;
static const double DIR_CHANGE_MAX_S = 10.0;
static const double REFLECT_ANGLE_PERTURB_DEG = 35.0;   // ±35° random tilt after reflection

static const double MIN_SPEED = 15.0;   // fallback for randSpeed (unused for per-UE)
static const double MAX_SPEED = 135.0;

// Per-UE speed (m/s) — SessionID 1–20 → index 0–19
static const double g_ue_speed_mps[20] = {
    55.0,  // UE 1  (idx 0)
    30.0,  // UE 2  (idx 1)
    18.0,  // UE 3  (idx 2)
    110.0, // UE 4  (idx 3)
    38.0,  // UE 5  (idx 4)
    65.0,  // UE 6  (idx 5)
    60.0,  // UE 7  (idx 6)
    130.0, // UE 8  (idx 7)
    75.0,  // UE 9  (idx 8)
    135.0, // UE 10 (idx 9)
    90.0,  // UE 11 (idx 10)
    45.0,  // UE 12 (idx 11)
    22.0,  // UE 13 (idx 12)
    35.0,  // UE 14 (idx 13)
    120.0, // UE 15 (idx 14)
    60.0,  // UE 16 (idx 15)
    20.0,  // UE 17 (idx 16)
    15.0,  // UE 18 (idx 17)
    105.0, // UE 19 (idx 18)
    25.0   // UE 20 (idx 19)
};

// Reflect velocity at boundary and add random angle perturbation
static void BoundedMobilityEnforce(uint32_t ueIdx, Ptr<Node> node,
                                   Ptr<UniformRandomVariable> randAngle,
                                   Ptr<UniformRandomVariable> randSpeed)
{
  (void)ueIdx;
  Ptr<ConstantVelocityMobilityModel> mob = node->GetObject<ConstantVelocityMobilityModel>();
  if (!mob) return;

  Vector pos = mob->GetPosition();
  Vector vel = mob->GetVelocity();
  bool reflected = false;

  // Clamp and reflect X
  if (pos.x < UE_BOUND_MIN) {
    pos.x = UE_BOUND_MIN;
    vel.x = std::abs(vel.x) + 1.0;
    reflected = true;
  } else if (pos.x > UE_BOUND_MAX) {
    pos.x = UE_BOUND_MAX;
    vel.x = -(std::abs(vel.x) + 1.0);
    reflected = true;
  }
  // Clamp and reflect Y
  if (pos.y < UE_BOUND_MIN) {
    pos.y = UE_BOUND_MIN;
    vel.y = std::abs(vel.y) + 1.0;
    reflected = true;
  } else if (pos.y > UE_BOUND_MAX) {
    pos.y = UE_BOUND_MAX;
    vel.y = -(std::abs(vel.y) + 1.0);
    reflected = true;
  }

  if (reflected) {
    // Apply random angle perturbation so reflection is not perfectly straight
    double angle = std::atan2(vel.y, vel.x);
    double perturbRad = (randAngle->GetValue() - 0.5) * 2.0 * (REFLECT_ANGLE_PERTURB_DEG * M_PI / 180.0);
    angle += perturbRad;
    double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    vel.x = speed * std::cos(angle);
    vel.y = speed * std::sin(angle);
  }

  if (reflected || pos.x < UE_BOUND_MIN || pos.x > UE_BOUND_MAX ||
      pos.y < UE_BOUND_MIN || pos.y > UE_BOUND_MAX) {
    mob->SetPosition(pos);
    mob->SetVelocity(vel);
  }

  Simulator::Schedule(Seconds(BOUNDARY_CHECK_INTERVAL_S),
                      &BoundedMobilityEnforce, ueIdx, node, randAngle, randSpeed);
}

// Same as above but for tighter bounds (UE 3, 12, 18): x ∈ [800,1200], y ∈ [700,1300]
static void BoundedMobilityEnforceTight(uint32_t ueIdx, Ptr<Node> node,
                                        Ptr<UniformRandomVariable> randAngle,
                                        Ptr<UniformRandomVariable> randSpeed)
{
  (void)ueIdx;
  Ptr<ConstantVelocityMobilityModel> mob = node->GetObject<ConstantVelocityMobilityModel>();
  if (!mob) return;

  Vector pos = mob->GetPosition();
  Vector vel = mob->GetVelocity();
  bool reflected = false;

  if (pos.x < UE_TIGHT_MIN_X) {
    pos.x = UE_TIGHT_MIN_X;
    vel.x = std::abs(vel.x) + 1.0;
    reflected = true;
  } else if (pos.x > UE_TIGHT_MAX_X) {
    pos.x = UE_TIGHT_MAX_X;
    vel.x = -(std::abs(vel.x) + 1.0);
    reflected = true;
  }
  if (pos.y < UE_TIGHT_MIN_Y) {
    pos.y = UE_TIGHT_MIN_Y;
    vel.y = std::abs(vel.y) + 1.0;
    reflected = true;
  } else if (pos.y > UE_TIGHT_MAX_Y) {
    pos.y = UE_TIGHT_MAX_Y;
    vel.y = -(std::abs(vel.y) + 1.0);
    reflected = true;
  }

  if (reflected) {
    double angle = std::atan2(vel.y, vel.x);
    double perturbRad = (randAngle->GetValue() - 0.5) * 2.0 * (REFLECT_ANGLE_PERTURB_DEG * M_PI / 180.0);
    angle += perturbRad;
    double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    vel.x = speed * std::cos(angle);
    vel.y = speed * std::sin(angle);
  }

  if (reflected || pos.x < UE_TIGHT_MIN_X || pos.x > UE_TIGHT_MAX_X ||
      pos.y < UE_TIGHT_MIN_Y || pos.y > UE_TIGHT_MAX_Y) {
    mob->SetPosition(pos);
    mob->SetVelocity(vel);
  }

  Simulator::Schedule(Seconds(BOUNDARY_CHECK_INTERVAL_S),
                      &BoundedMobilityEnforceTight, ueIdx, node, randAngle, randSpeed);
}

// Periodic random direction change for complex, realistic paths
static void BoundedMobilityDirectionChange(uint32_t ueIdx, Ptr<Node> node,
                                           Ptr<UniformRandomVariable> randAngle,
                                           Ptr<UniformRandomVariable> randSpeed)
{
  (void)randSpeed;
  Ptr<ConstantVelocityMobilityModel> mob = node->GetObject<ConstantVelocityMobilityModel>();
  if (!mob) return;

  double angle = randAngle->GetValue() * 2.0 * M_PI;
  double speed = (ueIdx < 20u) ? g_ue_speed_mps[ueIdx] : 80.0;
  mob->SetVelocity(Vector(speed * std::cos(angle), speed * std::sin(angle), 0.0));

  double nextIn = DIR_CHANGE_MIN_S + (DIR_CHANGE_MAX_S - DIR_CHANGE_MIN_S) * randAngle->GetValue();
  Simulator::Schedule(Seconds(nextIn),
                      &BoundedMobilityDirectionChange, ueIdx, node, randAngle, randSpeed);
}

// Sliding window (1 s) for DL/UL bitrate [bits/s]
static const double LSTM_BITRATE_WINDOW_S = 1.0;
static std::map<uint64_t, std::deque<std::pair<double, uint32_t>>> g_dlBytesHist;
static std::map<uint64_t, std::deque<std::pair<double, uint32_t>>> g_ulBytesHist;

static void DlRxCb (uint64_t imsi, Ptr<const Packet> p, const Address& addr)
{
  (void)addr;
  double now = Simulator::Now ().GetSeconds ();
  auto& q = g_dlBytesHist[imsi];
  q.push_back ({now, p->GetSize ()});
  while (!q.empty () && (now - q.front ().first) > LSTM_BITRATE_WINDOW_S)
    q.pop_front ();
}

// UdpClient "Tx" trace passes only (Ptr<const Packet>), not (packet, address)
static void UlTxCb (uint64_t imsi, Ptr<const Packet> p)
{
  double now = Simulator::Now ().GetSeconds ();
  auto& q = g_ulBytesHist[imsi];
  q.push_back ({now, p->GetSize ()});
  while (!q.empty () && (now - q.front ().first) > LSTM_BITRATE_WINDOW_S)
    q.pop_front ();
}

static double GetDlBitrate (uint64_t imsi)
{
  double now = Simulator::Now ().GetSeconds ();
  auto it = g_dlBytesHist.find (imsi);
  if (it == g_dlBytesHist.end ()) return 0.0;
  uint64_t sum = 0;
  for (const auto& e : it->second)
    if ((now - e.first) <= LSTM_BITRATE_WINDOW_S)
      sum += e.second;
  return (sum * 8.0) / LSTM_BITRATE_WINDOW_S; // bits/s
}

static double GetUlBitrate (uint64_t imsi)
{
  double now = Simulator::Now ().GetSeconds ();
  auto it = g_ulBytesHist.find (imsi);
  if (it == g_ulBytesHist.end ()) return 0.0;
  uint64_t sum = 0;
  for (const auto& e : it->second)
    if ((now - e.first) <= LSTM_BITRATE_WINDOW_S)
      sum += e.second;
  return (sum * 8.0) / LSTM_BITRATE_WINDOW_S;
}

// LSTM 12-feature CSV: real SINR from eNB L3 report; RSRP/CQI derived; speed from mobility; DL/UL from app traces
static std::ofstream g_lstmCsv;
static void LstmCsvTraceCb (NodeContainer* ueNodes, double bandwidthHz, uint64_t imsi,
                            uint16_t servingCellId, double servingSinrDb,
                            uint16_t bestNeighCellId, double bestNeighSinrDb)
{
  if (!g_lstmCsv.is_open ())
    {
      const char* home = std::getenv ("HOME");
      std::string path = (home && home[0]) ? std::string(home) + "/lstm_features.csv" : "lstm_features.csv";
      g_lstmCsv.open (path, std::ios::out | std::ios::app);
      if (g_lstmCsv.is_open ())
        NS_LOG_UNCOND ("LSTM CSV appending: " << path);
    }
  double t = Simulator::Now ().GetSeconds ();
  double Level = -100.0 + servingSinrDb;
  double Qual = 0.0;
  double SNR = servingSinrDb;
  double CQI = (servingSinrDb < -6.0) ? 0.0 : ((servingSinrDb > 26.0) ? 15.0 : (servingSinrDb + 6.0) / 2.2);
  double SecondCell_RSRP = (bestNeighCellId == 0) ? 0.0 : (-100.0 + bestNeighSinrDb);
  double SecondCell_SNR = (bestNeighCellId == 0) ? -999.0 : bestNeighSinrDb;
  double NRxLev1 = SecondCell_RSRP;
  double NQual1 = 0.0;
  double speed = 0.0;
  if (ueNodes && imsi >= 1 && imsi <= ueNodes->GetN ())
    {
      Ptr<Node> node = ueNodes->Get (imsi - 1);
      Ptr<MobilityModel> mob = node->GetObject<MobilityModel> ();
      if (mob)
        {
          Vector v = mob->GetVelocity ();
          speed = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z);
        }
    }
  double DL_bitrate = GetDlBitrate (imsi);
  double UL_bitrate = GetUlBitrate (imsi);
  double BANDWIDTH = (bandwidthHz > 0) ? (bandwidthHz / 1e6) : 20.0;
  g_lstmCsv << t << "," << imsi << "," << Level << "," << Qual << "," << SNR << "," << CQI << ","
            << SecondCell_RSRP << "," << SecondCell_SNR << "," << NRxLev1 << "," << NQual1 << ","
            << speed << "," << DL_bitrate << "," << UL_bitrate << "," << BANDWIDTH << ","
            << servingCellId << "," << bestNeighCellId << "\n";
}

// GRU xApp: 18-feature KPM CSV (SessionID, ElapsedTime, Node, CellID, Level, Qual, SNR, CQI,
// SecondCell_RSRP, SecondCell_SNR, NRxLev1, NQual1, Speed, DL_bitrate, UL_bitrate, BANDWIDTH, Latitude, Longitude)
static std::ofstream g_kpmHandoverCsv;
static bool g_kpmHandoverHeaderWritten = false;

void
PrintKpmFeaturesForHandoverXapp (uint32_t enbNodeId, uint32_t sessionId, Ptr<Node> ueNode,
                                  uint16_t servingCellId, double rsrp_serving, double rsrq_serving,
                                  double snr_serving, int cqi, double rsrp_neighbor1, double snr_neighbor1,
                                  double rsrp_neighbor2, double rsrq_neighbor2, double speed,
                                  double dl_bitrate, double ul_bitrate, double bandwidthHz)
{
  if (!g_kpmHandoverCsv.is_open ())
    {
      const char* home = std::getenv ("HOME");
      std::string path = (home && home[0]) ? std::string (home) + "/kpm_handover_features.csv" : "kpm_handover_features.csv";
      g_kpmHandoverCsv.open (path, std::ios::out | std::ios::trunc);
      if (g_kpmHandoverCsv.is_open ())
        NS_LOG_UNCOND ("GRU KPM CSV: " << path);
    }
  if (!g_kpmHandoverHeaderWritten && g_kpmHandoverCsv.is_open ())
    {
      g_kpmHandoverCsv << "SessionID,ElapsedTime,Node,CellID,Level,Qual,SNR,CQI,"
                       << "SecondCell_RSRP,SecondCell_SNR,NRxLev1,NQual1,"
                       << "Speed,DL_bitrate,UL_bitrate,BANDWIDTH,Latitude,Longitude\n";
      g_kpmHandoverHeaderWritten = true;
    }
  if (!g_kpmHandoverCsv.is_open ()) return;

  uint64_t elapsed_ms = (uint64_t) Simulator::Now ().GetMilliSeconds ();
  double lat = 3.0695, lon = 101.5998;
  if (ueNode)
    {
      Ptr<MobilityModel> mob = ueNode->GetObject<MobilityModel> ();
      if (mob)
        {
          Vector pos = mob->GetPosition ();
          double base_lat = 3.0695, base_lon = 101.5998;
          lat = base_lat + (pos.y / 111320.0);
          lon = base_lon + (pos.x / (111320.0 * std::cos (base_lat * M_PI / 180.0)));
        }
    }
  double bwMhz = (bandwidthHz > 0) ? (bandwidthHz / 1e6) : 20.0;
  g_kpmHandoverCsv << sessionId << "," << elapsed_ms << "," << enbNodeId << "," << servingCellId << ","
                   << rsrp_serving << "," << rsrq_serving << "," << snr_serving << "," << cqi << ","
                   << rsrp_neighbor1 << "," << snr_neighbor1 << "," << rsrp_neighbor2 << "," << rsrq_neighbor2 << ","
                   << speed << "," << dl_bitrate << "," << ul_bitrate << "," << bwMhz << ","
                   << lat << "," << lon << "\n";
}

// RSRQ approximation 3GPP TS 36.214: 20 MHz = 100 RBs → 10*log10(100)=20 dB
static const double N_RBs_20MHz = 100.0;
static const double RSRQ_MIN_DB = -19.5;
static const double RSRQ_MAX_DB = -3.0;

// ============================================================
// Real RSRP and PRB maps — updated every indicationPeriodicity
// Key = IMSI (1-based), Value = real measured value
// ============================================================
// RSRP[imsi] = TxPower_dBm - PathLoss_dB  (3GPP TS 36.214)
// Computed from UE-gNB distance using ThreeGpp UMi path loss model
static std::map<uint64_t, double> g_real_rsrp_serving;   // dBm per UE
static std::map<uint64_t, double> g_real_rsrp_neighbor;  // dBm best neighbor
static std::map<uint64_t, uint16_t> g_real_neigh_cellid; // best neighbor cell

// PRB[cellId] = numConnectedUEs * PRBs_per_UE
// Real PRB usage: total = sum of allocated PRBs across all UEs in cell
// In ns3 mmWave: each UE gets equal share → PRBused = (NUE/TOTAL)*TOTAL_PRB
static std::map<uint16_t, int> g_real_prb_used;   // PRBs used per cell
static std::map<uint16_t, int> g_real_prb_total;  // Total PRBs per cell
static const int SCENARIO_TOTAL_PRB = 28;

// ============================================================
// UpdateRealRsrpPrb — called periodically to compute real values
// Uses: UE position, gNB position, TxPower, path loss model
// ============================================================
static NodeContainer* g_mmWaveEnbNodes_ptr = nullptr;
static NodeContainer* g_ueNodes_ptr        = nullptr;
static NetDeviceContainer* g_mmWaveEnbDevs_ptr = nullptr;
static std::map<uint16_t, int>    g_rnti_prb_used;
static std::map<uint16_t, uint64_t> g_rnti_to_imsi;
static std::map<uint32_t, int> g_imsi_prb_used;

extern int ue_assoc_list[32];
static void UpdateRealRsrpPrb ()
{
  if (!g_mmWaveEnbNodes_ptr || !g_ueNodes_ptr || !g_mmWaveEnbDevs_ptr) return;

  // Reset PRB counts
  g_real_prb_used.clear ();
  g_real_prb_total.clear ();

  // Count UEs per cell from ue_assoc_list
  std::map<uint16_t, int> ues_per_cell;
  for (uint32_t u = 0; u < g_ueNodes_ptr->GetN (); ++u) {
    uint16_t cid = (uint16_t)ue_assoc_list[u];
    if (cid > 0) ues_per_cell[cid]++;
  }

  // Update PRB per cell: equal share allocation

for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN(); ++d) {
    Ptr<MmWaveEnbNetDevice> mmdev =
        DynamicCast<MmWaveEnbNetDevice>(g_mmWaveEnbDevs_ptr->Get(d));
    if (!mmdev) continue;
    uint16_t cid = mmdev->GetCellId();
    int nue = ues_per_cell.count(cid) ? ues_per_cell[cid] : 0;
    g_real_prb_total[cid] = SCENARIO_TOTAL_PRB;

    // PRB حقيقي: كل UE بياخد حصته = TOTAL_PRB / NUE (equal share scheduler)
    // ده بيعكس الـ paper بالظبط: PRBs per UE = TOTAL / NUE
    // والقيمة بتتغير مع كل UE يدخل أو يخرج من الـ cell
    g_real_prb_used[cid] = (nue > 0) 
                           ? (SCENARIO_TOTAL_PRB / nue) * nue  // PRBs مستخدمة فعلاً
                           : 0;
    
    NS_LOG_UNCOND("[PRB] Cell " << cid 
                  << "  NUE=" << nue 
                  << "  PRB/UE=" << (nue > 0 ? SCENARIO_TOTAL_PRB/nue : 0)
                  << "  PRB_used=" << g_real_prb_used[cid]);
}


  // Compute real RSRP per UE per cell
  // RSRP (dBm) = TxPower (dBm) - PathLoss (dB)
  // PathLoss = 3GPP UMi LoS: 32.4 + 21*log10(d3D) + 20*log10(fc_GHz)
  // fc = 3.5 GHz (scenario CenterFrequency)
  double fc_ghz = 3.5;
  double tx_power_dbm = 30.0;  // MmWave gNB TxPower from scenario (30 dBm)

  for (uint32_t u = 0; u < g_ueNodes_ptr->GetN (); ++u) {
    uint64_t imsi = u + 1;
    Ptr<MobilityModel> ue_mob = g_ueNodes_ptr->Get (u)->GetObject<MobilityModel> ();
    if (!ue_mob) continue;
    Vector ue_pos = ue_mob->GetPosition ();

    double best_rsrp = -200.0;
    uint16_t best_cell = 0;
    double serving_rsrp = -200.0;
    uint16_t serving_cell = (uint16_t)ue_assoc_list[u];

    for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN (); ++d) {
      Ptr<MmWaveEnbNetDevice> mmdev =
        DynamicCast<MmWaveEnbNetDevice> (g_mmWaveEnbDevs_ptr->Get (d));
      if (!mmdev) continue;
      uint16_t cid = mmdev->GetCellId ();
      Ptr<MobilityModel> enb_mob =
        mmdev->GetNode ()->GetObject<MobilityModel> ();
      if (!enb_mob) continue;
      Vector enb_pos = enb_mob->GetPosition ();

      // 3D distance
      double dx = ue_pos.x - enb_pos.x;
      double dy = ue_pos.y - enb_pos.y;
      double dz = ue_pos.z - enb_pos.z;
      double d3d = std::sqrt (dx*dx + dy*dy + dz*dz);
      if (d3d < 1.0) d3d = 1.0;

      // 3GPP UMi LoS path loss (TR 38.901): PL = 32.4 + 21*log10(d) + 20*log10(fc)
      double path_loss = 32.4 + 21.0 * std::log10 (d3d) + 20.0 * std::log10 (fc_ghz);

      // RSRP (dBm) = TxPower - PathLoss
      double rsrp = tx_power_dbm - path_loss;

      if (cid == serving_cell) {
        serving_rsrp = rsrp;
      }
      if (rsrp > best_rsrp && cid != serving_cell) {
        best_rsrp  = rsrp;
        best_cell  = cid;
      }
    }

    g_real_rsrp_serving[imsi]   = serving_rsrp;
    g_real_rsrp_neighbor[imsi]  = (best_cell > 0) ? best_rsrp : -200.0;
    g_real_neigh_cellid[imsi]   = best_cell;
  }
}

static void SchedTraceCallback(Ptr<MmWaveEnbNetDevice> dev,
                                MmWaveEnbMac::MmWaveSchedTraceInfo schedInfo)
{
    if (!dev) return;
    uint16_t cellId = dev->GetCellId();
    auto ueMap = dev->GetUeMap();
    auto& slotAlloc = schedInfo.m_indParam.m_slotAllocInfo;

    for (auto& tti : slotAlloc.m_ttiAllocInfo)
    {
        uint16_t rnti   = tti.m_rnti;
        uint8_t  numSym = tti.m_dci.m_numSym;
        if (rnti == 0 || numSym == 0) continue;
        int realPrb = (int)std::round(((double)numSym / 14.0) * SCENARIO_TOTAL_PRB);
        if (realPrb > SCENARIO_TOTAL_PRB) realPrb = SCENARIO_TOTAL_PRB;

        // بندور على UE بالـ RNTI في نفس الـ gNB بس
        auto it = ueMap.find(rnti);
        if (it != ueMap.end()) {
            uint64_t imsi = it->second->GetImsi();
            uint32_t key = ((uint32_t)cellId << 16) | (uint32_t)imsi;
            g_imsi_prb_used[key] = realPrb;
            NS_LOG_UNCOND("[MAC-PRB] Cell=" << cellId
                          << "  RNTI=" << rnti
                          << "  IMSI=" << imsi
                          << "  numSym=" << (int)numSym
                          << "  realPRB=" << realPrb);
        }
    }
}



static std::map<uint64_t, uint64_t> g_prevDlBytes;
static std::map<uint64_t, uint64_t> g_prevUlBytes;
static std::map<uint64_t, double> g_prevBitrateTime;
static Ptr<Node> g_remoteHost = nullptr;  // Set during scenario setup
// UE index → remoteHost application index for UL PacketSink (when some UEs skipped, ueIndex != app index)
static std::vector<int> g_ueToUlSinkIndex;

void
KpmHandoverCsvCb (uint32_t enbNodeId, NodeContainer* ueNodes, double bandwidthHz, uint64_t imsi,
                   uint16_t servingCellId, double servingSinrDb, uint16_t bestNeighCellId, double bestNeighSinrDb)
{
  // ── REAL RSRP (dBm) from 3GPP UMi path loss model ─────────────────────────
  // g_real_rsrp_serving updated every indicationPeriodicity by UpdateRealRsrpPrb()
  double rsrp_serving = (g_real_rsrp_serving.count(imsi) && g_real_rsrp_serving[imsi] > -199.0)
                        ? g_real_rsrp_serving[imsi]
                        : (-100.0 + servingSinrDb);  // fallback: approx from SINR

  double snr_serving = servingSinrDb;

  // RSRQ (dB) = RSRP - (N_RB × RSSI_per_RB) ≈ RSRP - 10·log10(N_RB)  3GPP TS 36.214
  double rsrq_serving = rsrp_serving - 10.0 * std::log10 (N_RBs_20MHz);
  rsrq_serving = std::max (RSRQ_MIN_DB, std::min (RSRQ_MAX_DB, rsrq_serving));

  int cqi = (servingSinrDb < -6.0) ? 0 : ((servingSinrDb > 26.0) ? 15 : (int) ((servingSinrDb + 6.0) / 2.2));

  // ── REAL neighbor RSRP ─────────────────────────────────────────────────────
  double rsrp_neighbor1 = (g_real_rsrp_neighbor.count(imsi) && g_real_rsrp_neighbor[imsi] > -199.0)
                          ? g_real_rsrp_neighbor[imsi]
                          : ((bestNeighCellId == 0) ? -200.0 : (-100.0 + bestNeighSinrDb));

  double snr_neighbor1 = (bestNeighCellId == 0) ? -999.0 : bestNeighSinrDb;

  // ── REAL PRB from g_real_prb_used ─────────────────────────────────────────

// ── REAL PRB from MAC scheduler per UE ────────────────────────────────────
  if (g_mmWaveEnbDevs_ptr) {
      for (uint32_t d = 0; d < g_mmWaveEnbDevs_ptr->GetN(); ++d) {
          Ptr<MmWaveEnbNetDevice> dev =
              DynamicCast<MmWaveEnbNetDevice>(g_mmWaveEnbDevs_ptr->Get(d));
          if (!dev || dev->GetCellId() != servingCellId) continue;
          auto ueMap = dev->GetUeMap();
          for (auto& kv : ueMap)
              if (kv.second->GetImsi() == imsi)
                  g_rnti_to_imsi[kv.first] = imsi;
      }
  }
  uint16_t ue_rnti = 0;
  for (auto& kv : g_rnti_to_imsi)
      if (kv.second == imsi) { ue_rnti = kv.first; break; }
  int nue_in_cell = 0;
  if (g_ueNodes_ptr)
      for (uint32_t u = 0; u < g_ueNodes_ptr->GetN(); ++u)
          if ((uint16_t)ue_assoc_list[u] == servingCellId) nue_in_cell++;
uint32_t prb_key = ((uint32_t)servingCellId << 16) | (uint32_t)imsi;
int real_prb_used = (g_imsi_prb_used.count(prb_key))
                    ? g_imsi_prb_used[prb_key]

                      : ((nue_in_cell > 0) ? (SCENARIO_TOTAL_PRB / nue_in_cell) : 0);

  int real_prb_total = (g_real_prb_total.count(servingCellId)) ? g_real_prb_total[servingCellId] : SCENARIO_TOTAL_PRB;

  NS_LOG_UNCOND ("[KPM-REAL] UE " << imsi << " Cell " << servingCellId
                 << "  RSRP=" << rsrp_serving << " dBm"
                 << "  SINR=" << servingSinrDb << " dB"
                 << "  PRB=" << real_prb_used << "/" << real_prb_total);

  double rsrp_neighbor2 = rsrp_neighbor1;
  double rsrq_neighbor2 = rsrp_neighbor1 - 10.0 * std::log10 (N_RBs_20MHz);
  rsrq_neighbor2 = std::max (RSRQ_MIN_DB, std::min (RSRQ_MAX_DB, rsrq_neighbor2));

  double speed = 0.0;
  Ptr<Node> ueNode;
  if (ueNodes && imsi >= 1 && imsi <= ueNodes->GetN ())
    {
      ueNode = ueNodes->Get (imsi - 1);
      Ptr<MobilityModel> mob = ueNode->GetObject<MobilityModel> ();
      if (mob) { Vector v = mob->GetVelocity (); speed = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z); }
    }

  // ── Fix 1 & 2: Instantaneous bitrate from PacketSink delta (NOT cumulative) ────────
  double dl_bitrate = 0.0;
  double ul_bitrate = 0.0;
  double now = Simulator::Now ().GetSeconds ();
  double dt = 0.0;

  // Calculate time delta since last sample
  if (g_prevBitrateTime.find (imsi) != g_prevBitrateTime.end ()) {
      dt = now - g_prevBitrateTime[imsi];
  } else {
      // First call for this UE: initialize prev values but return 0 for this sample
      dt = 0.0;
  }

  // ── Fix 1: DL bitrate — instantaneous throughput per interval ───────────────────────
  // Initialize prev values even on first call (dt=0) so second call can calculate delta
  if (ueNodes && imsi >= 1 && imsi <= ueNodes->GetN ())
    {
      Ptr<Node> ue = ueNodes->Get (imsi - 1);
      bool foundSink = false;
      for (uint32_t appIdx = 0; appIdx < ue->GetNApplications (); ++appIdx)
        {
          Ptr<PacketSink> sink = DynamicCast<PacketSink> (ue->GetApplication (appIdx));
          if (sink)
            {
              foundSink = true;
              uint64_t curr_dl = sink->GetTotalRx ();
              // Calculate bitrate only if we have a previous value AND dt > 0.01
              if (g_prevDlBytes.find (imsi) != g_prevDlBytes.end () && dt > 0.01)
                {
                  // Instantaneous bitrate = delta_bytes * 8 / delta_time
                  dl_bitrate = ((curr_dl - g_prevDlBytes[imsi]) * 8.0) / dt;
                }
              // Always update prev value for next iteration (even on first call)
              g_prevDlBytes[imsi] = curr_dl;
              break;
            }
        }
      // Diagnostic: log if sink not found for UE 19/20
      if (!foundSink && (imsi == 19 || imsi == 20)) {
          NS_LOG_UNCOND("[DIAG] UE " << imsi << " has " << ue->GetNApplications() << " apps but no PacketSink found!");
      }
    }

  // ── Fix 2: UL bitrate — use mapping when some UEs were skipped (no UL sink for them) ─────
  // Initialize prev values even on first call (dt=0) so second call can calculate delta
  if (g_remoteHost && ueNodes && imsi >= 1 && imsi <= ueNodes->GetN ())
    {
      uint32_t ueIndex = (uint32_t) (imsi - 1);
      int appIdx = (ueIndex < g_ueToUlSinkIndex.size () && g_ueToUlSinkIndex[ueIndex] >= 0)
                   ? g_ueToUlSinkIndex[ueIndex] : (int) ueIndex;
      if (appIdx >= 0 && (uint32_t) appIdx < g_remoteHost->GetNApplications ())
        {
          Ptr<PacketSink> sink = DynamicCast<PacketSink> (g_remoteHost->GetApplication (appIdx));
          if (sink)
            {
              uint64_t curr_ul = sink->GetTotalRx ();
              // Calculate bitrate only if we have a previous value AND dt > 0.01
              if (g_prevUlBytes.find (imsi) != g_prevUlBytes.end () && dt > 0.01)
                {
                  // Instantaneous bitrate = delta_bytes * 8 / delta_time
                  ul_bitrate = ((curr_ul - g_prevUlBytes[imsi]) * 8.0) / dt;
                }
              // Always update prev value for next iteration (even on first call)
              g_prevUlBytes[imsi] = curr_ul;
            }
        }
    }

  // Update time for next sample (shared between DL and UL)
  g_prevBitrateTime[imsi] = now;

  PrintKpmFeaturesForHandoverXapp (enbNodeId, (uint32_t) imsi, ueNode, servingCellId,
                                    rsrp_serving, rsrq_serving, snr_serving, cqi,
                                    rsrp_neighbor1, snr_neighbor1, rsrp_neighbor2, rsrq_neighbor2,
                                    speed, dl_bitrate, ul_bitrate, bandwidthHz);
}

std::map<uint64_t, uint16_t> imsi_cellid;
std::map<uint16_t, std::set<uint64_t>> imsi_list;
std::map<uint16_t, Ptr < Node>>
    cellid_node;
std::map<uint32_t, uint16_t> ue_cellid_usinghandover;
std::map<uint64_t, uint32_t> ueimsi_nodeid;
int ue_assoc_list[32] = {0};  // support up to 32 UEs for GRU xApp
double maxXAxis;
double maxYAxis;
bool esON_list[32] = {0};
double totalnewEnergyConsumption_storage[32] = {0};
double totaloldEnergyConsumption_storage[32] = {0};
double current_energy_consumption[32] = {0};
double curr_total_energy_consumption = 0;
double max_energy_consumption = 0;
double sum_curr_total_energy_consumption = 0;
int num_of_mmdev = 0;

/**
 * Scenario Zero
 *
 */

NS_LOG_COMPONENT_DEFINE ("ScenarioZero");

void
PrintGnuplottableUeListToFile(std::string filename) {
  std::ofstream outFile;
  outFile.open(filename.c_str(), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open()) {
      NS_LOG_ERROR("Can't open file " << filename);
      return;
    }
  for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
      Ptr <Node> node = *it;
      int nDevs = node->GetNDevices();
      for (int j = 0; j < nDevs; j++) {
          Ptr <LteUeNetDevice> uedev = node->GetDevice(j)->GetObject<LteUeNetDevice>();
          Ptr <MmWaveUeNetDevice> mmuedev = node->GetDevice(j)->GetObject<MmWaveUeNetDevice>();
          Ptr <McUeNetDevice> mcuedev = node->GetDevice(j)->GetObject<McUeNetDevice>();
          if (uedev) {
              Vector pos = node->GetObject<MobilityModel>()->GetPosition();
              outFile << "set label \"" << uedev->GetImsi() << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            } else if (mmuedev) {
              Vector pos = node->GetObject<MobilityModel>()->GetPosition();
              outFile << "set label \"" << mmuedev->GetImsi() << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            } else if (mcuedev) {
              Vector pos = node->GetObject<MobilityModel>()->GetPosition();
              outFile << "set label \"" << mcuedev->GetImsi() << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            }
        }
    }
}

void
PrintGnuplottableEnbListToFile(uint64_t m_startTime) {

  //uint64_t m_startTime = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
  uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();
  //
  std::string filename1 = "enbs.txt";
  std::string filename2 = "gnbs.txt";
  //
  int mmnode_iterator = 0;
  curr_total_energy_consumption = 0;
  for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
      Ptr <Node> node = *it;
      int nDevs = node->GetNDevices();
      for (int j = 0; j < nDevs; j++) {
          Ptr <LteEnbNetDevice> enbdev = node->GetDevice(j)->GetObject<LteEnbNetDevice>();
          Ptr <MmWaveEnbNetDevice> mmdev = node->GetDevice(j)->GetObject<MmWaveEnbNetDevice>();
          if (enbdev) {
              Vector pos = node->GetObject<MobilityModel>()->GetPosition();
              std::ofstream outFile1;
              outFile1.open(filename1.c_str(), std::ios_base::out | std::ios_base::app);
              if (!outFile1.is_open()) {
                  NS_LOG_ERROR("Can't open file " << filename1);
                  return;
                }
                //outFile1 << timestamp << "," << enbdev->GetCellId() << "," << pos.x << "," << pos.y << pos.z << std::endl;
                outFile1 << timestamp << "," << enbdev->GetCellId() << "," << pos.x << "," << pos.y << ","
                         << m_startTime << "," << "0" << "," << "30" << std::endl;
                outFile1.close();
            } else if (mmdev) {
                Vector pos = node->GetObject<MobilityModel>()->GetPosition();
                std::ofstream outFile2;
                outFile2.open(filename2.c_str(), std::ios_base::out | std::ios_base::app);
                if (!outFile2.is_open()) {
                    NS_LOG_ERROR("Can't open file " << filename2);
                    return;
                }
                auto ueMap = mmdev->GetUeMap();
                Ptr<MmWaveEnbPhy> enbPhy = node->GetDevice(j)->GetObject<MmWaveEnbNetDevice>()->GetPhy();
                for (const auto &ue: ueMap) {
                    uint64_t imsi_assoc = ue.second->GetImsi();
                    //NS_LOG_UNCOND ("IMSI: " << imsi_assoc << " associated with cell: "  << mmdev->GetCellId ());
                    ue_assoc_list[imsi_assoc - 1] = mmdev->GetCellId();
                }
              uint16_t cell_id = mmdev->GetCellId();
              double es_power = enbPhy->GetTxPower();
              if (es_power == 0) {
                  esON_list[cell_id] = true;
                } else {
                    esON_list[cell_id] = false;
                }
              curr_total_energy_consumption =
                  curr_total_energy_consumption + current_energy_consumption[cell_id];
              //outFile2 << timestamp << "," << enbdev->GetCellId() << "," << pos.x << "," << pos.y << pos.z << std::endl;
              outFile2 << timestamp << "," << cell_id << "," << pos.x << "," << pos.y << ","
                       << m_startTime << "," << esON_list[cell_id] << ","
                       << current_energy_consumption[cell_id] << "," << max_energy_consumption
                       << "," << sum_curr_total_energy_consumption << std::endl;
              outFile2.close ();
            }
        }
    }
  if (mmnode_iterator == num_of_mmdev)
    {
      sum_curr_total_energy_consumption = curr_total_energy_consumption;
    }
  if (mmnode_iterator == num_of_mmdev && max_energy_consumption < curr_total_energy_consumption)
    {
      max_energy_consumption = curr_total_energy_consumption;
    }
}

void
ClearFile(std::string Filename, uint64_t m_startTime) {
    std::string filename = Filename;
    std::ofstream outFile;
    outFile.open(filename.c_str(), std::ios_base::out | std::ios_base::trunc);
    if (!outFile.is_open()) {
        NS_LOG_ERROR("Can't open file " << filename);
        return;
    }
    outFile.close();
    //  struct timeval time_now{};
    //  gettimeofday (&time_now, nullptr);
    //uint64_t m_startTime = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
    uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();
    std::ofstream outFile1;
    outFile1.open(filename.c_str(), std::ios_base::out | std::ios_base::app);

  if (Filename == "ue_position.txt") {
      outFile1 << "timestamp,id,x,y,type,cell,simid" << std::endl;
    }
  else if (Filename == "gnbs.txt") {
      outFile1 << "timestamp,id,x,y,simid,ESstate,currEC,maxEC,totalcurrEC" << std::endl;
      outFile1 << timestamp << "," << "0" << "," << maxXAxis << "," << maxYAxis << std::endl;
    }
  else if (Filename == "enbs.txt") {
      outFile1 << "timestamp,id,x,y,simid,ESstate,power" << std::endl;
    }
    outFile1.close();
}

void
PrintPosition(Ptr<Node> node, int iterator, std::string Filename, uint64_t m_startTime) {

  //uint64_t m_startTime = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
  uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();

  int imsi;
  Ptr <Node> node1 = NodeList::GetNode(iterator);
  int nDevs = node->GetNDevices();
  std::string filename = Filename;
  std::ofstream outFile;
  for (int j = 0; j < nDevs; j++) {
      Ptr <McUeNetDevice> mcuedev = node1->GetDevice(j)->GetObject<McUeNetDevice>();
      Ptr <LteUeNetDevice> uedev = node->GetDevice(j)->GetObject<LteUeNetDevice>();
      Ptr <MmWaveUeNetDevice> mmuedev = node->GetDevice(j)->GetObject<MmWaveUeNetDevice>();
      if (mcuedev) {
          imsi = int(mcuedev->GetImsi());
          int serving_cell = ue_assoc_list[imsi - 1];
          if (serving_cell==0){
              serving_cell=1;
            }
          Ptr<MobilityModel> model = node->GetObject<MobilityModel> ();
          Vector position = model->GetPosition ();
          NS_LOG_UNCOND ("Position of UE with IMSI " << imsi << " is " << model->GetPosition ()
                                                     << " at time "
                                                     << Simulator::Now ().GetSeconds ()
                                                     << ", UE connected to Cell: " << serving_cell);
          outFile.open(filename.c_str(), std::ios_base::out | std::ios_base::app);
          if (!outFile.is_open()) {
              NS_LOG_ERROR("Can't open file " << filename);
              return;
            }

          outFile << timestamp << "," << imsi << "," << position.x << "," << position.y << ",mc,"
                  << serving_cell << "," << m_startTime << std::endl;
          outFile.close ();
        }
      else
        {
          //
        }
    }
}

void
EnergyConsumptionUpdate (int nodeIndex, std::string filename, double totaloldEnergyConsumption,
                         double totalnewEnergyConsumption)
{
  //std::cout << "mmWave cell " << nodeIndex+2 << ": Total Energy Consumption " << totalnewEnergyConsumption << "J" << std::endl;
  Time currentTime = Simulator::Now ();
  std::ofstream outFile;
  outFile.open (filename, std::ios_base::out | std::ios_base::app);
  outFile << currentTime.GetSeconds () << "," << totalnewEnergyConsumption << ","
          << (totalnewEnergyConsumption - totaloldEnergyConsumption) << std::endl;
  totalnewEnergyConsumption_storage[nodeIndex] = totalnewEnergyConsumption;
}

void
EnergyConsumptionPrint (int nodeIndex)
{
  NS_LOG_UNCOND ("Total energy consumption for mmWave cell "
                 << nodeIndex + 2 << ": " << totalnewEnergyConsumption_storage[nodeIndex] << "J"
                 << " at time " << Simulator::Now ().GetSeconds ()
                 << ", diff from last measurement is: "
                 << (totalnewEnergyConsumption_storage[nodeIndex] -
                     totaloldEnergyConsumption_storage[nodeIndex])
                 << "J");
  totalnewEnergyConsumption_storage[nodeIndex] = totalnewEnergyConsumption_storage[nodeIndex];
  current_energy_consumption[nodeIndex] =
      totalnewEnergyConsumption_storage[nodeIndex] - totaloldEnergyConsumption_storage[nodeIndex];
  totaloldEnergyConsumption_storage[nodeIndex] = totalnewEnergyConsumption_storage[nodeIndex];
}

static ns3::GlobalValue g_bufferSize("bufferSize", "RLC tx buffer size (MB)",
                                      ns3::UintegerValue(10),
                                      ns3::MakeUintegerChecker<uint32_t>());

static ns3::GlobalValue g_enableTraces("enableTraces", "If true, generate ns-3 traces",
                                        ns3::BooleanValue(true), ns3::MakeBooleanChecker());

static ns3::GlobalValue g_e2lteEnabled("e2lteEnabled", "If true, send LTE E2 reports",
                                        ns3::BooleanValue(true), ns3::MakeBooleanChecker());

static ns3::GlobalValue g_e2nrEnabled("e2nrEnabled", "If true, send NR E2 reports",
                                       ns3::BooleanValue(true), ns3::MakeBooleanChecker());

static ns3::GlobalValue g_e2du("e2du", "If true, send DU reports", ns3::BooleanValue(true),
                                ns3::MakeBooleanChecker());

static ns3::GlobalValue g_e2cuUp("e2cuUp", "If true, send CU-UP reports", ns3::BooleanValue(true),
                                  ns3::MakeBooleanChecker());

static ns3::GlobalValue g_e2cuCp("e2cuCp", "If true, send CU-CP reports", ns3::BooleanValue(true),
                                  ns3::MakeBooleanChecker());

static ns3::GlobalValue g_reducedPmValues("reducedPmValues", "If true, use a subset of the the pm containers",
                                          ns3::BooleanValue(false), ns3::MakeBooleanChecker());

// ============================================
// LSTM model & xApp compatibility (12 features)
// KPM: L3servingSINR3gpp_cell_X_UEID_Y, L3neighSINRListOf_UEID_Y_of_Cell_X.
// xApp derives: Level/Qual/CQI from serving SINR; SecondCell_RSRP/SNR, NRxLev1 from neighbor SINR;
// Speed/DL/UL=0; BANDWIDTH=20 (must use --Bandwidth=20e6). RC executes LSTM handover decision.
// ============================================

static ns3::GlobalValue
    g_hoSinrDifference("hoSinrDifference",
                        "SINR difference [dB] for HO trigger; dense urban: 1.5",
                        ns3::DoubleValue(1.5), ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue
    g_indicationPeriodicity("indicationPeriodicity",
                             "E2 KPM indication periodicity [s]; GRU xApp uses 0.1",
                             ns3::DoubleValue(0.1), ns3::MakeDoubleChecker<double>(0.01, 2.0));


static ns3::GlobalValue g_simTime("simTime", "Simulation time in seconds; 600 for GRU dataset",
                                   ns3::DoubleValue(600), ns3::MakeDoubleChecker<double>(0.1, 100000.0));

static ns3::GlobalValue g_outageThreshold("outageThreshold",
                                           "SNR/SINR threshold for outage [dB]; dense urban: -3.0",
                                           ns3::DoubleValue(-3.0),
                                           ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue g_numberOfRaPreambles(
    "numberOfRaPreambles",
    "how many random access preambles are available for the contention based RACH process",
    ns3::UintegerValue(40), // Indicated for TS use case, 52 is default
    ns3::MakeUintegerChecker<uint8_t>());

static ns3::GlobalValue
    g_handoverMode("handoverMode",
                    "NoAuto=انتظار قرار xApp، DynamicTtt/Threshold=قرار من السيناريو",
                    ns3::StringValue("NoAuto"), ns3::MakeStringChecker());

static ns3::GlobalValue g_e2TermIp("e2TermIp", "The IP address of the RIC E2 termination",
                                    ns3::StringValue("127.0.0.1"), ns3::MakeStringChecker());

static ns3::GlobalValue
        g_enableE2FileLogging("enableE2FileLogging",
                              "If true, offline file logging only (no RIC). If false, connect to RIC for xApp/KPM/RC (default false for xApp use). CSV ~/lstm_features.csv is filled in both cases.",
                              ns3::BooleanValue(false), ns3::MakeBooleanChecker());
static ns3::GlobalValue g_e2_func_id("KPM_E2functionID", "Function ID to subscribe",
                                      ns3::DoubleValue(2),
                                      ns3::MakeDoubleChecker<double>());
static ns3::GlobalValue g_rc_e2_func_id("RC_E2functionID", "Function ID to subscribe",
                                         ns3::DoubleValue(3),
                                         ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue g_controlFileName("controlFileName",
                                           "The path to the control file (can be absolute)",
                                           ns3::StringValue(""),
                                           ns3::MakeStringChecker());

// GRU xApp: dense urban 7 gNBs (1 center + 6 hex), UEs <= 20
static ns3::GlobalValue mmWave_nodes ("N_MmWaveEnbNodes", "Number of mmWave gNBs (dense urban: 7)",
                                      ns3::UintegerValue (7),
                                      ns3::MakeUintegerChecker<uint8_t> ());
// TODO: next step(make it in correct way, regarding to position)
// static ns3::GlobalValue lteEnb_nodes ("N_LteEnbNodes", "Number of LteEnbNodes",
//                                       ns3::UintegerValue (1),
//                                       ns3::MakeUintegerChecker<uint8_t> ());

static ns3::GlobalValue ue_s ("N_Ues", "Number of UEs (GRU xApp: 20)",
                              ns3::UintegerValue (20),
                              ns3::MakeUintegerChecker<uint32_t> ());

static ns3::GlobalValue center_freq ("CenterFrequency", "Center Frequency Value",
                                     ns3::DoubleValue (3.5e9),
                                     ns3::MakeDoubleChecker<double> ());

// Bandwidth [Hz]; 20e6 = 20 MHz -> LSTM feature BANDWIDTH=20
static ns3::GlobalValue bandwidth_value ("Bandwidth", "Channel bandwidth [Hz]; 20e6 = 20 MHz for LSTM BANDWIDTH",
                                         ns3::DoubleValue (20e6),
                                         ns3::MakeDoubleChecker<double> ());
// TODO: check for later
// static ns3::GlobalValue num_antennas_McUe ("N_AntennasMcUe", "Number of Antenna as McUe",
//                                       ns3::IntegerValue (1),
//                                       ns3::MakeIntegerChecker<int> ());

// static ns3::GlobalValue num_antennas_MmWave ("N_AntennasMmWave", "Number of Antenna as MmWave",
//                                       ns3::IntegerValue (1),
//                                       ns3::MakeIntegerChecker<int> ());

static ns3::GlobalValue interside_distance_value_ue ("IntersideDistanceUEs", "UE distribution radius [m]; dense urban: 800",
                                      ns3::DoubleValue (800),
                                      ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue interside_distance_value_cell ("IntersideDistanceCells", "ISD between gNBs [m]; dense urban: 300",
                                                  ns3::DoubleValue (300),
                                                  ns3::MakeDoubleChecker<double> ());



// ============================================
// Handover Tracking Functions
// ============================================

std::map<uint64_t, uint16_t> g_ue_target_cells;
std::map<uint64_t, uint16_t> g_ue_source_cells;  // from_cell at handover start (for SUCCESS row)
std::ofstream g_handover_log;

// handover.csv: time_sec,ue_id,from_cell,to_cell,event,executed_ok
// event=START|SUCCESS|FAILURE; executed_ok=1 only for SUCCESS (handover actually executed)
void OnHandoverStart(std::string context, uint64_t imsi, uint16_t sourceCellId, uint16_t rnti, uint16_t targetCellId)
{
    double time = Simulator::Now().GetSeconds();

    NS_LOG_UNCOND("[NS3-HO] " << time << "s - Handover START: UE " << imsi
                  << " (RNTI:" << rnti << ")"
                  << " from Cell " << sourceCellId
                  << " to Cell " << targetCellId);

    g_ue_target_cells[imsi] = targetCellId;
    g_ue_source_cells[imsi] = sourceCellId;

    if (g_handover_log.is_open()) {
        g_handover_log << time << "," << imsi << "," << sourceCellId << "," << targetCellId
                      << ",START,0" << std::endl;
    }
}

void OnHandoverSuccess(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    double time = Simulator::Now().GetSeconds();
    uint16_t fromCell = g_ue_source_cells[imsi];
    bool expected = (g_ue_target_cells[imsi] == cellId);

    NS_LOG_UNCOND("[NS3-HO] " << time << "s - ✅ Handover SUCCESS: UE " << imsi
                  << " (RNTI:" << rnti << ")"
                  << " from Cell " << fromCell << " to Cell " << cellId
                  << (expected ? " (EXPECTED)" : " (UNEXPECTED!)"));

    if (g_handover_log.is_open()) {
        g_handover_log << time << "," << imsi << "," << fromCell << "," << cellId
                      << ",SUCCESS,1" << std::endl;
    }
}

void OnHandoverFailure(std::string context, uint64_t imsi, uint16_t sourceCellId)
{
    double time = Simulator::Now().GetSeconds();

    NS_LOG_UNCOND("[NS3-HO] " << time << "s - ❌ Handover FAILED: UE " << imsi
                  << " still on Cell " << sourceCellId);

    if (g_handover_log.is_open()) {
        g_handover_log << time << "," << imsi << "," << sourceCellId << ","
                      << g_ue_target_cells[imsi] << ",FAILURE,0" << std::endl;
    }
}

int
main(int argc, char *argv[]) {
  LogComponentEnableAll(LOG_PREFIX_ALL);
  //  LogComponentEnable ("RicControlMessage", LOG_LEVEL_ALL);
  //  LogComponentEnable ("KpmIndication", LOG_LEVEL_DEBUG);
  LogComponentEnable("KpmIndication", LOG_LEVEL_INFO);

  // LogComponentEnable ("Asn1Types", LOG_LEVEL_LOGIC);
  //   LogComponentEnable ("E2Termination", LOG_LEVEL_LOGIC);
  //  LogComponentEnable ("E2Termination", LOG_LEVEL_DEBUG);

  // LogComponentEnable ("LteEnbNetDevice", LOG_LEVEL_ALL);
  //LogComponentEnable ("MmWaveEnbNetDevice", LOG_LEVEL_DEBUG);

  // 2 km² dense urban area (paper: Sunway City–style)
  maxXAxis = 2000;
  maxYAxis = 2000;

  // Command line arguments
  CommandLine cmd;
  cmd.Parse(argc, argv);

  bool harqEnabled = true;

  UintegerValue uintegerValue;
  BooleanValue booleanValue;
  StringValue stringValue;
  DoubleValue doubleValue;

  GlobalValue::GetValueByName("hoSinrDifference", doubleValue);
  double hoSinrDifference = doubleValue.Get();
  GlobalValue::GetValueByName("bufferSize", uintegerValue);
  uint32_t bufferSize = uintegerValue.Get();
  GlobalValue::GetValueByName("enableTraces", booleanValue);
  bool enableTraces = booleanValue.Get();
  GlobalValue::GetValueByName("outageThreshold", doubleValue);
  double outageThreshold = doubleValue.Get();
  GlobalValue::GetValueByName("handoverMode", stringValue);
  std::string handoverMode = stringValue.Get();
  GlobalValue::GetValueByName("e2TermIp", stringValue);
  std::string e2TermIp = stringValue.Get();
  GlobalValue::GetValueByName("enableE2FileLogging", booleanValue);
  bool enableE2FileLogging = booleanValue.Get();
  GlobalValue::GetValueByName("KPM_E2functionID", doubleValue);
  double g_e2_func_id = doubleValue.Get();
  GlobalValue::GetValueByName("RC_E2functionID", doubleValue);
  double g_rc_e2_func_id = doubleValue.Get();


  GlobalValue::GetValueByName("numberOfRaPreambles", uintegerValue);
  uint8_t numberOfRaPreambles = uintegerValue.Get();

  NS_LOG_UNCOND("bufferSize " << bufferSize << " OutageThreshold " << outageThreshold
                               << " HandoverMode " << handoverMode << " e2TermIp " << e2TermIp
                               << " enableE2FileLogging " << enableE2FileLogging
                               << " E2 Function ID " << g_e2_func_id);

  GlobalValue::GetValueByName("e2lteEnabled", booleanValue);
  bool e2lteEnabled = booleanValue.Get();
  GlobalValue::GetValueByName("e2nrEnabled", booleanValue);
  bool e2nrEnabled = booleanValue.Get();
  GlobalValue::GetValueByName("e2du", booleanValue);
  bool e2du = booleanValue.Get();
  GlobalValue::GetValueByName("e2cuUp", booleanValue);
  bool e2cuUp = booleanValue.Get();
  GlobalValue::GetValueByName("e2cuCp", booleanValue);
  bool e2cuCp = booleanValue.Get();

  GlobalValue::GetValueByName("reducedPmValues", booleanValue);
  bool reducedPmValues = booleanValue.Get();

  GlobalValue::GetValueByName("indicationPeriodicity", doubleValue);
  double indicationPeriodicity = doubleValue.Get();
  GlobalValue::GetValueByName("controlFileName", stringValue);
  std::string controlFilename = stringValue.Get();

  NS_LOG_UNCOND("e2lteEnabled " << e2lteEnabled << " e2nrEnabled " << e2nrEnabled << " e2du "
                                 << e2du << " e2cuCp " << e2cuCp << " e2cuUp " << e2cuUp
                                 << " controlFilename " << controlFilename
                                 << " indicationPeriodicity " << indicationPeriodicity);

  Config::SetDefault("ns3::LteEnbNetDevice::ControlFileName", StringValue(controlFilename));
  Config::SetDefault("ns3::LteEnbNetDevice::E2Periodicity", DoubleValue(indicationPeriodicity));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity",
                      DoubleValue(indicationPeriodicity));

  Config::SetDefault("ns3::MmWaveHelper::E2ModeLte", BooleanValue(e2lteEnabled));
  Config::SetDefault("ns3::MmWaveHelper::E2ModeNr", BooleanValue(e2nrEnabled));

  // The DU PM reports should come from both NR gNB as well as LTE eNB,
  // since in the RLC/MAC/PHY entities are present in BOTH NR gNB as well as LTE eNB.
  // DU reports from LTE eNB are not implemented in this release
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableDuReport", BooleanValue(e2du));

  // The CU-UP PM reports should only come from LTE eNB, since in the NS3 "EN-DC
  // simulation (Option 3A)", the PDCP is only in the LTE eNB and NOT in the NR gNB
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuUpReport", BooleanValue(e2cuUp));
  Config::SetDefault("ns3::LteEnbNetDevice::EnableCuUpReport", BooleanValue(e2cuUp));

  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuCpReport", BooleanValue(e2cuCp));
  Config::SetDefault("ns3::LteEnbNetDevice::EnableCuCpReport", BooleanValue(e2cuCp));

  Config::SetDefault("ns3::MmWaveEnbNetDevice::ReducedPmValues", BooleanValue(reducedPmValues));
  Config::SetDefault("ns3::LteEnbNetDevice::ReducedPmValues", BooleanValue(reducedPmValues));

  Config::SetDefault("ns3::LteEnbNetDevice::EnableE2FileLogging",
                      BooleanValue(enableE2FileLogging));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableE2FileLogging",
                      BooleanValue(enableE2FileLogging));


  Config::SetDefault("ns3::LteEnbNetDevice::KPM_E2functionID",
                      DoubleValue(g_e2_func_id));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::KPM_E2functionID",
                      DoubleValue(g_e2_func_id));

  Config::SetDefault("ns3::LteEnbNetDevice::RC_E2functionID",
                      DoubleValue(g_rc_e2_func_id));

  Config::SetDefault("ns3::MmWaveEnbMac::NumberOfRaPreambles",
                      UintegerValue(numberOfRaPreambles));

  Config::SetDefault("ns3::MmWaveHelper::HarqEnabled", BooleanValue(harqEnabled));
  Config::SetDefault("ns3::MmWaveHelper::UseIdealRrc", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp", StringValue(e2TermIp));

  Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled", BooleanValue(harqEnabled));
  Config::SetDefault("ns3::MmWavePhyMacCommon::NumHarqProcess", UintegerValue(100));
  //Config::SetDefault ("ns3::MmWaveBearerStatsCalculator::EpochDuration", TimeValue (MilliSeconds (10.0)));

  // set to false to use the 3GPP radiation pattern (proper configuration of the bearing and downtilt angles is needed)
  Config::SetDefault("ns3::PhasedArrayModel::AntennaElement",
    PointerValue(CreateObject<IsotropicAntennaModel>()));
Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue (MilliSeconds (100.0)));
Config::SetDefault ("ns3::ThreeGppChannelConditionModel::UpdatePeriod",
  TimeValue (MilliSeconds (100)));

  Config::SetDefault("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue(MilliSeconds(10.0)));
  Config::SetDefault("ns3::LteRlcUmLowLat::ReportBufferStatusTimer",
                      TimeValue(MilliSeconds(10.0)));
  Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));
  Config::SetDefault("ns3::LteRlcUmLowLat::MaxTxBufferSize",
                      UintegerValue(bufferSize * 1024 * 1024));
  Config::SetDefault("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));

  Config::SetDefault("ns3::LteEnbRrc::OutageThreshold", DoubleValue(outageThreshold));
  Config::SetDefault("ns3::LteEnbRrc::SecondaryCellHandoverMode", StringValue(handoverMode));
  Config::SetDefault("ns3::LteEnbRrc::HoSinrDifference", DoubleValue(hoSinrDifference));
 Config::SetDefault("ns3::ThreeGppPropagationLossModel::Frequency",DoubleValue(3.5e9));
  Config::SetDefault("ns3::ThreeGppPropagationLossModel::ShadowingEnabled",BooleanValue(false));
 
// ============================================
  // RRC Measurement Configuration for Handover
  // ============================================
 // Config::SetDefault("ns3::LteUeRrc::MeasurementReportEnabled", BooleanValue(true));
  
  // A3 Event Configuration
 // Config::SetDefault("ns3::LteEnbRrc::A3Offset", DoubleValue(0.5));      // 3 dB offset
 // Config::SetDefault("ns3::LteEnbRrc::Hysteresis", DoubleValue(0.0));    // 1 dB hysteresis
 // Config::SetDefault("ns3::LteEnbRrc::TimeToTrigger", TimeValue(MilliSeconds(160)));
 // Config::SetDefault("ns3::LteEnbRrc::ReportInterval", TimeValue(MilliSeconds(40)));
  
 // NS_LOG_UNCOND("RRC Measurement Configuration: A3Offset=0.5dB, Hysteresis=0dB, TTT=160ms");

  // Reproducibility for GRU dataset
  RngSeedManager::SetSeed(42);
  RngSeedManager::SetRun(1);

  // Carrier bandwidth in Hz (from GlobalValue; 20e6 -> BANDWIDTH=20)
  GlobalValue::GetValueByName("Bandwidth", doubleValue);
  double bandwidth = doubleValue.Get();
  GlobalValue::GetValueByName("CenterFrequency", doubleValue);
  double centerFrequency = doubleValue.Get();
  GlobalValue::GetValueByName("IntersideDistanceUEs", doubleValue);
  double isd_ue = doubleValue.Get();
  GlobalValue::GetValueByName("IntersideDistanceCells", doubleValue);
  double isd_cell = doubleValue.Get();

  // Number of antennas in each UE
  // GlobalValue::GetValueByName ("N_AntennasMcUe", uintegerValue);
  int numAntennasMcUe = 1; //uintegerValue.Get();
  // Number of antennas in each mmWave BS
  // GlobalValue::GetValueByName ("N_AntennasMmWave", uintegerValue);
  int numAntennasMmWave = 1; //uintegerValue.Get();

  NS_LOG_INFO("Bandwidth " << bandwidth << " centerFrequency " << double(centerFrequency)
                            << " isd_ue " << isd_ue << " numAntennasMcUe " << numAntennasMcUe
                            << " numAntennasMmWave " << numAntennasMmWave);

  // Set the number of antennas in the devices
  Config::SetDefault("ns3::McUeNetDevice::AntennaNum", UintegerValue(numAntennasMcUe));
  Config::SetDefault("ns3::MmWaveNetDevice::AntennaNum", UintegerValue(numAntennasMmWave));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(bandwidth));
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(centerFrequency));

  Ptr <MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
  mmwaveHelper->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmwaveHelper->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr <MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
  mmwaveHelper->SetEpcHelper(epcHelper);

  // GRU/dense-urban: قيم ثابتة في السيناريو — لا تعتمد على command line أو GUI
  const uint8_t nMmWaveEnbNodes = 7;
  const uint8_t nLteEnbNodes = 1;
  const uint32_t ues = 20;
  const uint32_t nUeNodes = ues;

  NS_LOG_INFO(" Bandwidth " << bandwidth << " centerFrequency " << double(centerFrequency)
                             << " isd_cell " << isd_cell << " numAntennasMcUe " << numAntennasMcUe
                             << " numAntennasMmWave " << numAntennasMmWave << " nMmWaveEnbNodes "
                             << unsigned(nMmWaveEnbNodes));

  // Get SGW/PGW and create a single RemoteHost
  Ptr <Node> pgw = epcHelper->GetPgwNode();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr <Node> remoteHost = remoteHostContainer.Get(0);
  g_remoteHost = remoteHost;  // Set global for bitrate sampling
  InternetStackHelper internet;
  internet.Install(remoteHostContainer);

  // Create the Internet by connecting remoteHost to pgw. Setup routing too
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
  p2ph.SetDeviceAttribute("Mtu", UintegerValue(2500));
  p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.010)));
  NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
  // interface 0 is localhost, 1 is the p2p device
  Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1);
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr <Ipv4StaticRouting> remoteHostStaticRouting =
      ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // create LTE, mmWave eNB nodes and UE node
  NodeContainer ueNodes;
  NodeContainer mmWaveEnbNodes;
  NodeContainer lteEnbNodes;
  NodeContainer allEnbNodes;
  mmWaveEnbNodes.Create(nMmWaveEnbNodes);
  lteEnbNodes.Create(nLteEnbNodes);
  ueNodes.Create(nUeNodes);
  allEnbNodes.Add(lteEnbNodes);
  allEnbNodes.Add(mmWaveEnbNodes);

  NodeContainerManager::GetInstance().SetMmWaveEnbNodes(mmWaveEnbNodes);

  // Position
  Vector centerPosition = Vector(maxXAxis / 2, maxYAxis / 2, 3);

  // Install Mobility Model
  Ptr <ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();

  // We want a center with one LTE enb and one mmWave co-located in the same place
  enbPositionAlloc->Add(centerPosition);
  enbPositionAlloc->Add(centerPosition);
  double x, y;
  double nConstellation = nMmWaveEnbNodes - 1;

  // This guarantee that each of the rest BSs is placed at the same distance from the two co-located in the center
  for (int8_t i = 0; i < nConstellation; ++i) {
      x = isd_cell * cos((2 * M_PI * i) / (nConstellation));
      y = isd_cell * sin((2 * M_PI * i) / (nConstellation));
      enbPositionAlloc->Add(Vector(centerPosition.x + x, centerPosition.y + y, 3));
    }

  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbmobility.SetPositionAllocator(enbPositionAlloc);
  enbmobility.Install(allEnbNodes);

  // ============================================
  // UE Mobility: Bounded box [500,1500]×[500,1500] with reflection + random turns
  // ============================================

  // ============================================================
  // [FIX #3 — توزيع الـ UEs: عشوائي كامل → ~3 UEs لكل cell]
  //
  // المشكلة الأصلية:
  //   الكود القديم كان بيوزّع الـ UEs العشوائية في النطاق [20%–80%] من الـ grid
  //   (يعني 400m–1600m في x و y) بدون أي اعتبار للـ cells.
  //   النتيجة: ممكن 15 UE يقعوا في 2 cells وباقي الـ 5 cells فاضية أو فيها 1 UE.
  //   ده بيمنع ظهور أثر Load Balancing لأن الـ load مش موزّع على الـ cells أصلاً.
  //
  // التحسين:
  //   بنوزّع الـ 20 UE صراحةً على الـ 7 cells (~3 UEs/cell تقريباً).
  //   كل UE بيتوضع بالقرب من الـ gNB الخاص بيها (ضمن radius ±100m).
  //   ده بيضمن:
  //     1. كل cell فيها load مبدئي متقارب (~3 UEs) من أول الـ simulation.
  //     2. الـ xApp يشوف Load Balancing situation حقيقية ويقدر يتصرف.
  //     3. مع حركة الـ UEs بمرور الوقت الـ load يتغير ويظهر أثر الـ xApp.
  //
  // التوزيع:
  //   Cells 1–6: 3 UEs لكل cell = 18 UEs
  //   Cell  7  : 2 UEs (المتبقيين)
  //   UE1 (idx 0) و UE8 (idx 7): محافظين على مواضعهم الثابتة (يُحسب cell من موضعهم).
  // ============================================================

  // حساب مواضع الـ gNBs (نفس الحساب المستخدم في enbPositionAlloc أعلاه)
  std::vector<Vector> mmWavePositions;
  mmWavePositions.push_back(centerPosition);  // Cell 1 (center, co-located with LTE)
  for (int8_t i = 0; i < nConstellation; ++i) {
      double angle = (2 * M_PI * i) / (nConstellation);
      double gnbX = centerPosition.x + isd_cell * cos(angle);
      double gnbY = centerPosition.y + isd_cell * sin(angle);
      mmWavePositions.push_back(Vector(gnbX, gnbY, 1.5));
  }

  // موضع Cell 7 بالتحديد (index 6) — مستخدم لـ UE 19, 20 في الكود الأصلي
  Vector cell7Position;
  if (nMmWaveEnbNodes >= 7 && mmWavePositions.size() >= 7) {
      cell7Position = mmWavePositions[6];
      NS_LOG_UNCOND("[FIX#3] Cell 7 position: (" << cell7Position.x << ", " << cell7Position.y << ")");
  } else {
      cell7Position = centerPosition;
  }

  // متغير عشوائي لإضافة offset صغير ±100m حول كل gNB
  // (مقلّص من ±200m الأصلي عشان نضمن كل UE قريبة من cell الخاصة بيها فعلاً)
  Ptr<UniformRandomVariable> nearGnbRand = CreateObject<UniformRandomVariable>();
  nearGnbRand->SetAttribute("Min", DoubleValue(-100.0));
  nearGnbRand->SetAttribute("Max", DoubleValue(100.0));

  // [FIX #3] بناء قائمة المواضع: كل cell تاخد ~3 UEs بدل التوزيع العشوائي
  // التعيين: UE index → cell index (0-based)
  //   Cells 0–5 (Cell IDs 1–6): 3 UEs لكل cell = 18 UEs (indices 0..17)
  //   Cell  6   (Cell ID 7)    : 2 UEs           (indices 18..19)
  //
  // UE1 (idx 0) و UE8 (idx 7) محافظين على مواضعهم الثابتة اللي اتحدّدت فوق.
  // باقي الـ UEs بيتوضعوا بالقرب من الـ gNB المقابلة للـ cell اللي اتعيّنوا فيها.

  Ptr<ListPositionAllocator> uePositionAlloc = CreateObject<ListPositionAllocator>();

  // UEs per cell assignment (20 UEs, 7 cells): cells 0-5 get 3 each, cell 6 gets 2
  // cell_for_ue[i] = index in mmWavePositions (0-based) for UE index i
  int cell_for_ue[20] = {
    0,  // UE 0  (SessionID 1)  → Cell 1 (center) — موضع ثابت (900,1300) محتفظ به
    0,  // UE 1  (SessionID 2)  → Cell 1
    0,  // UE 2  (SessionID 3)  → Cell 1 — RandomWaypoint (FIX#2 طبّقنا عليه)
    1,  // UE 3  (SessionID 4)  → Cell 2
    1,  // UE 4  (SessionID 5)  → Cell 2
    1,  // UE 5  (SessionID 6)  → Cell 2
    2,  // UE 6  (SessionID 7)  → Cell 3
    2,  // UE 7  (SessionID 8)  → Cell 3 — موضع ثابت (800,1190) محتفظ به
    2,  // UE 8  (SessionID 9)  → Cell 3 — RandomWaypoint (FIX#2 طبّقنا عليه)
    2,  // UE 9  (SessionID 10) → Cell 3 — RandomWaypoint (FIX#2 طبّقنا عليه)
    3,  // UE 10 (SessionID 11) → Cell 4
    3,  // UE 11 (SessionID 12) → Cell 4
    3,  // UE 12 (SessionID 13) → Cell 4
    4,  // UE 13 (SessionID 14) → Cell 5
    4,  // UE 14 (SessionID 15) → Cell 5
    4,  // UE 15 (SessionID 16) → Cell 5
    5,  // UE 16 (SessionID 17) → Cell 6
    5,  // UE 17 (SessionID 18) → Cell 6
    5,  // UE 18 (SessionID 19) → Cell 6
    6,  // UE 19 (SessionID 20) → Cell 7
  };

  for (uint32_t i = 0; i < nUeNodes; ++i) {
      int cellIdx = cell_for_ue[i];
      Vector gnbPos = mmWavePositions[cellIdx];

      if (i == 0) {
          // UE 1: within bounded box 500–1500
          Vector ue1Position = Vector(900.0, 1300.0, 1.5);
          uePositionAlloc->Add(ue1Position);
          NS_LOG_UNCOND("[BOUNDED] UE[" << i << "] (SessionID 1) init (900, 1300) → Cell " << (cellIdx+1));
      }
      else if (i == 7) {
          // UE 8: within bounded box 500–1500
          Vector ue8Position = Vector(800.0, 1190.0, 1.5);
          uePositionAlloc->Add(ue8Position);
          NS_LOG_UNCOND("[BOUNDED] UE[" << i << "] (SessionID 8) init (800, 1190) → Cell " << (cellIdx+1));
      }
      else {
          // [FIX #3] باقي الـ UEs: بالقرب من الـ gNB الخاصة بيهم (±100m)
          // بدل التوزيع العشوائي الكامل اللي كان ممكن يحط كل UEs في نفس الـ cell
          double offsetX = nearGnbRand->GetValue();
          double offsetY = nearGnbRand->GetValue();
          // [BOUNDED] Clamp to 500–1500 m — all UEs stay in this box
          double px = std::max(UE_BOUND_MIN, std::min(UE_BOUND_MAX, gnbPos.x + offsetX));
          double py = std::max(UE_BOUND_MIN, std::min(UE_BOUND_MAX, gnbPos.y + offsetY));
          Vector uePos = Vector(px, py, 1.5);
          uePositionAlloc->Add(uePos);
          NS_LOG_UNCOND("[FIX#3] UE[" << i << "] (SessionID " << (i+1)
                        << ") placed near Cell " << (cellIdx+1)
                        << " gNB at (" << gnbPos.x << "," << gnbPos.y << ")"
                        << " → actual pos (" << px << "," << py << ")");
      }
  }

  // ============================================
  // [BOUNDED] All UEs: ConstantVelocity + boundary reflection (500–1500 m box)
  // When a UE hits the boundary, it reflects with random angle perturbation.
  // Periodic direction changes add complex, realistic movement.
  // ============================================
  MobilityHelper boundedVelocityMobility;
  boundedVelocityMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  boundedVelocityMobility.SetPositionAllocator(uePositionAlloc);
  boundedVelocityMobility.Install(ueNodes);

  Ptr<UniformRandomVariable> randAngle = CreateObject<UniformRandomVariable>();
  randAngle->SetAttribute("Min", DoubleValue(0.0));
  randAngle->SetAttribute("Max", DoubleValue(1.0));

  Ptr<UniformRandomVariable> randSpeed = CreateObject<UniformRandomVariable>();
  randSpeed->SetAttribute("Min", DoubleValue(MIN_SPEED));
  randSpeed->SetAttribute("Max", DoubleValue(MAX_SPEED));

  // UE 3, 12, 18 (indices 2, 11, 17): tighter box x∈[800,1200], y∈[700,1300]
  static const uint32_t tightUeIndices[] = {2, 11, 17};
  static const size_t nTightUes = sizeof(tightUeIndices) / sizeof(tightUeIndices[0]);

  for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
      Ptr<ConstantVelocityMobilityModel> mob = ueNodes.Get(i)->GetObject<ConstantVelocityMobilityModel>();
      if (!mob) continue;

      bool isTightUe = false;
      for (size_t k = 0; k < nTightUes; ++k)
        if (tightUeIndices[k] == i) { isTightUe = true; break; }

      Vector pos = mob->GetPosition();
      if (isTightUe) {
          pos.x = std::max(UE_TIGHT_MIN_X, std::min(UE_TIGHT_MAX_X, pos.x));
          pos.y = std::max(UE_TIGHT_MIN_Y, std::min(UE_TIGHT_MAX_Y, pos.y));
      } else {
          pos.x = std::max(UE_BOUND_MIN, std::min(UE_BOUND_MAX, pos.x));
          pos.y = std::max(UE_BOUND_MIN, std::min(UE_BOUND_MAX, pos.y));
      }
      mob->SetPosition(pos);

      double angle = randAngle->GetValue() * 2.0 * M_PI;
      double speed = (i < 20u) ? g_ue_speed_mps[i] : 80.0;
      mob->SetVelocity(Vector(speed * std::cos(angle), speed * std::sin(angle), 0.0));

      if (isTightUe) {
          Simulator::Schedule(Seconds(BOUNDARY_CHECK_INTERVAL_S),
                              &BoundedMobilityEnforceTight, i, ueNodes.Get(i), randAngle, randSpeed);
      } else {
          Simulator::Schedule(Seconds(BOUNDARY_CHECK_INTERVAL_S),
                              &BoundedMobilityEnforce, i, ueNodes.Get(i), randAngle, randSpeed);
      }

      double firstChange = DIR_CHANGE_MIN_S + (DIR_CHANGE_MAX_S - DIR_CHANGE_MIN_S) * randAngle->GetValue();
      Simulator::Schedule(Seconds(firstChange),
                          &BoundedMobilityDirectionChange, i, ueNodes.Get(i), randAngle, randSpeed);
  }

  NS_LOG_UNCOND("[BOUNDED] All " << ueNodes.GetN()
                << " UEs: ConstantVelocity + boundary reflection, per-UE speeds. "
                << "UEs 1–2,4–11,13–17,19–20: x,y ∈ [" << UE_BOUND_MIN << "," << UE_BOUND_MAX << "] m. "
                << "UE 3,12,18: x ∈ [" << UE_TIGHT_MIN_X << "," << UE_TIGHT_MAX_X << "], y ∈ ["
                << UE_TIGHT_MIN_Y << "," << UE_TIGHT_MAX_Y << "] m");
  // ============================================

  // Install mmWave, lte, mc Devices to the nodesnumAntennasMmWave
  NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice(lteEnbNodes);
  NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice(mmWaveEnbNodes);
  NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice(ueNodes);

  // ── Set global pointers for UpdateRealRsrpPrb() ────────────────────────────
  g_mmWaveEnbNodes_ptr = &mmWaveEnbNodes;
  g_ueNodes_ptr        = &ueNodes;
  g_mmWaveEnbDevs_ptr  = &mmWaveEnbDevs;

  // Install the IP stack on the UEs
  internet.Install(ueNodes);

  // Add X2 interfaces and attach UEs *before* IP assignment so bearers exist for all UEs
  mmwaveHelper->AddX2Interface(lteEnbNodes, mmWaveEnbNodes);
  mmwaveHelper->AttachToClosestEnb(mcUeDevs, mmWaveEnbDevs, lteEnbDevs);

  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(mcUeDevs));

  // ── Fix A: DIAGNOSTIC: verify every UE got a valid IP ──────────────────────────
  NS_LOG_UNCOND("═══════════════════════════════════════════");
  NS_LOG_UNCOND("EPC IP Assignment: " << ueIpIface.GetN()
              << " addresses assigned for " << ueNodes.GetN() << " UEs");

  if (ueIpIface.GetN() != ueNodes.GetN()) {
      NS_LOG_UNCOND("[CRITICAL] IP MISMATCH — "
                    << (ueNodes.GetN() - ueIpIface.GetN())
                    << " UEs have NO IP address. These will have zero bitrate!");
  }

  for (uint32_t u = 0; u < ueIpIface.GetN(); ++u) {
      Ipv4Address addr = ueIpIface.GetAddress(u);
      NS_LOG_UNCOND("  UE[" << u << "] IP = " << addr);
      if (addr == Ipv4Address("0.0.0.0")) {
          NS_LOG_UNCOND("  [WARNING] UE[" << u << "] has invalid IP 0.0.0.0!");
      }
  }
  NS_LOG_UNCOND("═══════════════════════════════════════════");

  // Set default gateway for each UE
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
      Ptr <Node> ueNode = ueNodes.Get(u);
      Ptr <Ipv4StaticRouting> ueStaticRouting =
          ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
      ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

  BasicEnergySourceHelper basicEnergySourceHelper;
  basicEnergySourceHelper.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (1000000000000));
  basicEnergySourceHelper.Set ("BasicEnergySupplyVoltageV", DoubleValue (5.0));
  energy::EnergySourceContainer sources = basicEnergySourceHelper.Install (mmWaveEnbNodes);
  MmWaveRadioEnergyModelEnbHelper nrEnbHelper;

  energy::DeviceEnergyModelContainer deviceEModel = nrEnbHelper.Install (mmWaveEnbDevs, sources);

  GlobalValue::GetValueByName ("simTime", doubleValue);
  double simTime = doubleValue.Get ();
  int numPrints = simTime / 0.1;

  std::vector<std::ofstream> outFiles;
  for (int x = 0; x < nMmWaveEnbNodes; ++x)
    {
      std::ostringstream energyFileName;
      energyFileName << "energyfilecell" << x + 2 << ".csv";

      std::ofstream outFile;
      outFile.open (energyFileName.str (), std::ios_base::out | std::ios_base::trunc);
      outFile << "Time,NetEnergy,DiffEnergy" << std::endl;

      outFiles.push_back (std::move (outFile));
    }

  for (int x = 0; x < nMmWaveEnbNodes; ++x)
    {
      std::ostringstream filename;
      filename << "energyfilecell" << x + 2 << ".csv";
      deviceEModel.Get (x)->TraceConnectWithoutContext (
          "TotalEnergyConsumption",
          MakeBoundCallback (&EnergyConsumptionUpdate, x, filename.str ()));
      for (int i = 0; i < numPrints; i++)
        {
          Simulator::Schedule (Seconds (i * simTime / numPrints), &EnergyConsumptionPrint, x);
        }
    }

  // ── Fix B: Install and start applications – mixed session types (paper: FTP, Video, HTTP) ────
  NS_LOG_UNCOND("=== INSTALLING TRAFFIC APPLICATIONS ===");

  ApplicationContainer sinkApp;
  ApplicationContainer clientApp;

  uint16_t dlPort = 1234;
  uint16_t ulBasePort = 60000;

  // Mixed traffic: FTP (TCP), Video (UDP), HTTP (TCP OnOff)
  uint32_t nFtpUes = ues / 3;
  uint32_t nVideoUes = ues / 3;
  uint32_t nHttpUes = ues - nFtpUes - nVideoUes;

  g_ueToUlSinkIndex.resize(ueNodes.GetN(), -1);
  uint32_t remoteUlIdx = 0;

  for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {

      // ── Step 1: Always install DL sink on UE ──────────────────────────
      uint16_t sinkPort;
      std::string socketFactory;
      if (u < nFtpUes) {
          sinkPort = 20;  // FTP
          socketFactory = "ns3::TcpSocketFactory";
      } else if (u < nFtpUes + nVideoUes) {
          sinkPort = 1234;  // Video
          socketFactory = "ns3::UdpSocketFactory";
      } else {
          sinkPort = 80;  // HTTP
          socketFactory = "ns3::TcpSocketFactory";
      }

      PacketSinkHelper dlSinkHelper(socketFactory,
          InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
      sinkApp.Add(dlSinkHelper.Install(ueNodes.Get(u)));

      // ── Step 2: Validate IP before installing clients ──────────────────
      if (u >= ueIpIface.GetN()) {
          NS_LOG_UNCOND("  [SKIP] UE[" << u << "] — no IP interface entry");
          continue;
      }

      Ipv4Address ueAddr = ueIpIface.GetAddress(u);

      if (ueAddr == Ipv4Address("0.0.0.0") || ueAddr == Ipv4Address()) {
          NS_LOG_UNCOND("  [SKIP] UE[" << u << "] — IP is " << ueAddr << " (invalid)");
          continue;
      }

      NS_LOG_UNCOND("  [OK]   UE[" << u << "] IP=" << ueAddr
                    << " — installing DL + UL traffic");
      
      // ── Fix 3: Diagnostic log for UE 19 and UE 20 ────────────────────────────────
      if (u == 18 || u == 19) {  // UE index 18 = UE 19, index 19 = UE 20
          NS_LOG_UNCOND("  [DIAG] UE " << (u + 1) << " (index " << u << ") assigned IP: " << ueAddr);
          if (ueAddr == Ipv4Address("0.0.0.0")) {
              NS_LOG_UNCOND("  [CRITICAL] UE " << (u + 1) << " has invalid IP 0.0.0.0 — EPC bearer setup failed!");
          }
      }

      // ── Step 3: DL client on remoteHost → sends TO this UE ─────────────
      if (u < nFtpUes) {
          // FTP: TCP BulkSend — Fix 4: Reduced rate for realistic DL:UL ratio
          BulkSendHelper dlClient("ns3::TcpSocketFactory",
              InetSocketAddress(ueAddr, sinkPort));
          dlClient.SetAttribute("SendSize", UintegerValue(500));   // smaller packets
          dlClient.SetAttribute("MaxBytes", UintegerValue(0));
          clientApp.Add(dlClient.Install(remoteHost));
      } else if (u < nFtpUes + nVideoUes) {
          // Video: UDP CBR — Fix 4: Reduced rate for realistic DL:UL ratio
          UdpClientHelper dlClient(ueAddr, sinkPort);
          dlClient.SetAttribute("Interval", TimeValue(MicroSeconds(5000)));  // 5ms → lower rate
          dlClient.SetAttribute("PacketSize", UintegerValue(500));           // smaller packets
          dlClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
          clientApp.Add(dlClient.Install(remoteHost));
      } else {
          // HTTP: TCP OnOff — Fix 4: Reduced rate for realistic DL:UL ratio
          OnOffHelper dlClient("ns3::TcpSocketFactory",
              InetSocketAddress(ueAddr, sinkPort));
          dlClient.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.5]"));
          dlClient.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=2.0]"));
          dlClient.SetAttribute("DataRate", DataRateValue(DataRate("500Kbps")));  // reduced from 1Mbps
          dlClient.SetAttribute("PacketSize", UintegerValue(500));                 // smaller packets
          clientApp.Add(dlClient.Install(remoteHost));
      }

      // ── Step 4: UL sink on remoteHost (unique port per UE) ─────────────
      g_ueToUlSinkIndex[u] = (int) remoteUlIdx++;
      uint16_t ulPort = ulBasePort + u;
      PacketSinkHelper ulSinkHelper("ns3::UdpSocketFactory",
          InetSocketAddress(Ipv4Address::GetAny(), ulPort));
      sinkApp.Add(ulSinkHelper.Install(remoteHost));

      // ── Step 5: UL client on UE → sends TO remoteHost ──────────────────
      // Fix 4: Reduced UL rate for realistic DL:UL ratio
      UdpClientHelper ulClient(remoteHostAddr, ulPort);
      ulClient.SetAttribute("Interval", TimeValue(MicroSeconds(8000)));  // 8ms → lower rate
      ulClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
      ulClient.SetAttribute("PacketSize", UintegerValue(256));           // smaller packets
      clientApp.Add(ulClient.Install(ueNodes.Get(u)));
  }

  // KPM/GRU: trace DL (sink Rx) and UL (client Tx) for bitrate
  // DL sinks are in sinkApp[0..19] for UE[0..19]
  for (uint32_t u = 0; u < ueNodes.GetN () && u < sinkApp.GetN (); ++u)
    {
      Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApp.Get (u));
      if (sink)
        sink->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&DlRxCb, (uint64_t) (u + 1)));
    }
  
  // Clients: DL clients first (one per UE that wasn't skipped), then UL clients (one per UE that wasn't skipped)
  // We need to track which UE each client belongs to
  uint32_t clientIdx = 0;
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
      // Check if this UE got clients installed (had valid IP)
      if (u >= ueIpIface.GetN()) continue;
      Ipv4Address ueAddr = ueIpIface.GetAddress(u);
      if (ueAddr == Ipv4Address("0.0.0.0") || ueAddr == Ipv4Address()) continue;
      
      // DL client for UE u is at clientApp[clientIdx]
      if (clientIdx < clientApp.GetN()) {
          Ptr<UdpClient> udpClient = DynamicCast<UdpClient> (clientApp.Get (clientIdx));
          if (udpClient)
            udpClient->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t) (u + 1)));
          else
            {
              Ptr<BulkSendApplication> bulkClient = DynamicCast<BulkSendApplication> (clientApp.Get (clientIdx));
              if (bulkClient)
                bulkClient->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t) (u + 1)));
              else
                {
                  Ptr<OnOffApplication> onoffClient = DynamicCast<OnOffApplication> (clientApp.Get (clientIdx));
                  if (onoffClient)
                    onoffClient->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t) (u + 1)));
                }
            }
          clientIdx++;
      }
  }
  
  // UL clients follow DL clients in clientApp
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
      // Check if this UE got clients installed (had valid IP)
      if (u >= ueIpIface.GetN()) continue;
      Ipv4Address ueAddr = ueIpIface.GetAddress(u);
      if (ueAddr == Ipv4Address("0.0.0.0") || ueAddr == Ipv4Address()) continue;
      
      // UL client for UE u is at clientApp[clientIdx] (after all DL clients)
      if (clientIdx < clientApp.GetN()) {
          Ptr<UdpClient> ulClient = DynamicCast<UdpClient> (clientApp.Get (clientIdx));
          if (ulClient)
            ulClient->TraceConnectWithoutContext ("Tx", MakeBoundCallback (&UlTxCb, (uint64_t) (u + 1)));
          clientIdx++;
      }
  }

  // ── Fix C: Start applications with staggered timing ────────────────────────────
  // Sinks start immediately
  sinkApp.Start(Seconds(0));
  sinkApp.Stop(Seconds(simTime));

  // Clients start with 50ms stagger per UE to ensure RRC + EPC attachment completes
  for (uint32_t u = 0; u < clientApp.GetN(); ++u) {
      double startDelay = 0.1 + (u * 0.05);   // 100ms base + 50ms per UE index
      clientApp.Get(u)->SetStartTime(Seconds(startDelay));
      clientApp.Get(u)->SetStopTime(Seconds(simTime - 0.1));
  }

  struct timeval time_now{};
  gettimeofday(&time_now, nullptr);
  uint64_t t_startTime_simid = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
  std::string ue_poss_out = "ue_position.txt";
  ClearFile(ue_poss_out, t_startTime_simid);
  ClearFile("enbs.txt", t_startTime_simid);
  ClearFile("gnbs.txt", t_startTime_simid);
  // Since nodes are randomly allocated during each run we always need to print their positions
  PrintGnuplottableUeListToFile("ues.txt");

  int nodecount = int(NodeList::GetNNodes());
  int UE_iterator = nodecount - int (nUeNodes);

  NS_LOG_UNCOND("   GUI: http://127.0.0.1:8000/ (files in current dir: ue_position.txt, gnbs.txt, enbs.txt)");

  for (int i = 0; i < numPrints; i++) {
      Simulator::Schedule(Seconds(i * simTime / numPrints), &PrintGnuplottableEnbListToFile, t_startTime_simid);
      for (uint32_t j = 0; j < ueNodes.GetN(); j++) {
          Simulator::Schedule(Seconds(i * simTime / numPrints), &PrintPosition, ueNodes.Get(j),
                               j + UE_iterator, ue_poss_out, t_startTime_simid);
        }
    }

  if (enableTraces) {
      mmwaveHelper->EnableTraces();
    }

  // trick to enable PHY traces for the LTE stack
  Ptr <LteHelper> lteHelper = CreateObject<LteHelper>();
  lteHelper->Initialize();
  lteHelper->EnablePhyTraces();
  lteHelper->EnableMacTraces();

  // Since nodes are randomly allocated during each run we always need to print their positions
  //PrintGnuplottableUeListToFile ("ues.txt");
  // PrintGnuplottableEnbListToFile ("enbs.txt");

  bool run = true;

// ============================================
  // Open Handover Log File (handover.csv: ue_id, from_cell, to_cell, time, event, executed_ok)
  // ============================================
  {
    const char* home = std::getenv("HOME");
    std::string path = (home && home[0]) ? std::string(home) + "/handover.csv" : "handover.csv";
    g_handover_log.open(path, std::ios::out | std::ios::trunc);
    if (g_handover_log.is_open()) {
        g_handover_log << "time_sec,ue_id,from_cell,to_cell,event,executed_ok" << std::endl;
        NS_LOG_UNCOND("Handover log: " << path);
    }
  }

  // ============================================
  // Connect Handover Callbacks
  // ============================================
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                  MakeCallback(&OnHandoverStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                  MakeCallback(&OnHandoverSuccess));
  Config::ConnectFailSafe("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverFailure",
                          MakeCallback(&OnHandoverFailure));
  
  NS_LOG_UNCOND("Handover callbacks connected");

  // LSTM 12-feature CSV: create in HOME so it is always findable (ns3 may run from build/ so cwd differs)
  {
    const char* home = std::getenv ("HOME");
    std::string path = (home && home[0]) ? std::string(home) + "/lstm_features.csv" : "lstm_features.csv";
    std::ofstream initCsv (path, std::ios::out | std::ios::trunc);
    if (initCsv.is_open ())
      {
        initCsv << "Time,IMSI,Level,Qual,SNR,CQI,SecondCell_RSRP,SecondCell_SNR,NRxLev1,NQual1,"
                << "Speed,DL_bitrate,UL_bitrate,BANDWIDTH,serving_cell,best_neigh_cell\n";
        initCsv.close ();
        NS_LOG_UNCOND ("LSTM CSV: " << path << " (filled every 0.25s by eNB trace, with or without RIC)");
      }
    else
      NS_LOG_UNCOND ("LSTM CSV: could not create " << path);
  }
  for (uint32_t d = 0; d < mmWaveEnbDevs.GetN (); ++d)
    {
      Ptr<MmWaveEnbNetDevice> enbDev = DynamicCast<MmWaveEnbNetDevice> (mmWaveEnbDevs.Get (d));
      if (enbDev)
        {
          enbDev->TraceConnectWithoutContext (
              "L3ReportUeSinr",
              MakeBoundCallback (&LstmCsvTraceCb, &ueNodes, bandwidth));
          uint32_t enbNodeId = enbDev->GetNode ()->GetId ();
enbDev->TraceConnectWithoutContext (
              "L3ReportUeSinr",
              MakeBoundCallback (&KpmHandoverCsvCb, enbNodeId, &ueNodes, bandwidth));
        }
    }

  // ── SchedulingTraceEnb — PRB حقيقي لكل UE من الـ MAC ─────────────────────
  for (uint32_t d = 0; d < mmWaveEnbDevs.GetN(); ++d) {
      Ptr<MmWaveEnbNetDevice> enbDev2 =
          DynamicCast<MmWaveEnbNetDevice>(mmWaveEnbDevs.Get(d));
      if (enbDev2) {
enbDev2->GetMac()->TraceConnectWithoutContext(
    "SchedulingTraceEnb",
    MakeBoundCallback(&SchedTraceCallback, enbDev2));

      }
  }

  // ── Schedule UpdateRealRsrpPrb every indicationPeriodicity ─────────────────


  // Keeps g_real_rsrp_serving/neighbor and g_real_prb_used always fresh.
  // Must run BEFORE L3ReportUeSinr fires so KpmHandoverCsvCb sees real values.
  for (double t = indicationPeriodicity; t < simTime; t += indicationPeriodicity)
    {
      Simulator::Schedule (Seconds (t), &UpdateRealRsrpPrb);
    }
  Simulator::Schedule (Seconds (0.01), &UpdateRealRsrpPrb);
  NS_LOG_UNCOND ("[REAL-KPM] UpdateRealRsrpPrb scheduled every "
                 << indicationPeriodicity << "s — real RSRP+PRB active");

  if (run) {
      NS_LOG_UNCOND("Simulation time is " << simTime << " seconds ");
      Simulator::Stop(Seconds(simTime));
      NS_LOG_INFO("Run Simulation.");
      Simulator::Run();
    }

// Close handover log
  if (g_handover_log.is_open()) {
      g_handover_log.close();
      NS_LOG_UNCOND("Handover log closed (handover.csv)");
  }
  if (g_lstmCsv.is_open ()) {
      g_lstmCsv.close ();
      NS_LOG_UNCOND("LSTM features CSV closed: lstm_features.csv");
  }
  if (g_kpmHandoverCsv.is_open ()) {
      g_kpmHandoverCsv.close ();
      NS_LOG_UNCOND("GRU KPM features CSV closed: kpm_handover_features.csv");
  }

  NS_LOG_INFO(lteHelper);
  Simulator::Destroy();
  NS_LOG_INFO("Done.");
  return 0;
}
