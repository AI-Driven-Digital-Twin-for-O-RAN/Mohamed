/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/* *
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
#include "ns3/isotropic-antenna-model.h"
// ─── Added includes (needed for GUI file output + energy) ───
#include <sys/time.h>
#include <ctime>
#include <sys/types.h>
#include <iostream>
#include <stdlib.h>
#include <list>
#include <random>
#include <chrono>
#include <cmath>
#include <fstream>
#include "ns3/basic-energy-source-helper.h"
#include "ns3/mmwave-radio-energy-model-enb-helper.h"


using namespace ns3;
using namespace mmwave;

// ─── Global state (needed by the periodic callbacks) ────────────────────────
std::map<uint64_t, uint16_t> imsi_cellid;
std::map<uint16_t, std::set<uint64_t>> imsi_list;
std::map<uint16_t, Ptr<Node>> cellid_node;
std::map<uint32_t, uint16_t> ue_cellid_usinghandover;
std::map<uint64_t, uint32_t> ueimsi_nodeid;

int    ue_assoc_list[10]                    = {0};
bool   esON_list[10]                        = {0};
double totalnewEnergyConsumption_storage[10] = {0};
double totaloldEnergyConsumption_storage[10] = {0};
double current_energy_consumption[10]       = {0};
double curr_total_energy_consumption        = 0;
double max_energy_consumption               = 0;
double sum_curr_total_energy_consumption    = 0;
int    num_of_mmdev                         = 0;

double maxXAxis;   // set in main(), used by ClearFile
double maxYAxis;

/**
 * Scenario Three
 */

NS_LOG_COMPONENT_DEFINE ("ScenarioThree");

// ─────────────────────────────────────────────────────────────────────────────
// PrintGnuplottableUeListToFile  –  one-shot, gnuplot-style label file (ues.txt)
// (kept as-is; the GUI does NOT read this file, but it doesn't hurt)
// ─────────────────────────────────────────────────────────────────────────────
void
PrintGnuplottableUeListToFile (std::string filename)
{
  std::ofstream outFile;
  outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << filename);
      return;
    }
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> node = *it;
      int nDevs = node->GetNDevices ();
      for (int j = 0; j < nDevs; j++)
        {
          Ptr<LteUeNetDevice>     uedev   = node->GetDevice (j)->GetObject<LteUeNetDevice> ();
          Ptr<MmWaveUeNetDevice>  mmuedev = node->GetDevice (j)->GetObject<MmWaveUeNetDevice> ();
          Ptr<McUeNetDevice>      mcuedev = node->GetDevice (j)->GetObject<McUeNetDevice> ();
          if (uedev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << uedev->GetImsi () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            }
          else if (mmuedev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << mmuedev->GetImsi () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            }
          else if (mcuedev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              outFile << "set label \"" << mcuedev->GetImsi () << "\" at " << pos.x << "," << pos.y
                      << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                         "0.3 lc rgb \"black\" offset 0,0"
                      << std::endl;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PrintGnuplottableEnbListToFile  –  PERIODIC CSV writer for GUI
//   writes  enbs.txt  (LTE eNBs)
//   writes  gnbs.txt  (mmWave gNBs, includes energy columns)
// ─────────────────────────────────────────────────────────────────────────────
void
PrintGnuplottableEnbListToFile (uint64_t m_startTime)
{
  uint64_t timestamp = m_startTime + (uint64_t)Simulator::Now ().GetMilliSeconds ();

  std::string filename1 = "enbs.txt";
  std::string filename2 = "gnbs.txt";

  int mmnode_iterator = 0;
  curr_total_energy_consumption = 0;

  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
    {
      Ptr<Node> node = *it;
      int nDevs = node->GetNDevices ();
      for (int j = 0; j < nDevs; j++)
        {
          Ptr<LteEnbNetDevice>    enbdev = node->GetDevice (j)->GetObject<LteEnbNetDevice> ();
          Ptr<MmWaveEnbNetDevice> mmdev  = node->GetDevice (j)->GetObject<MmWaveEnbNetDevice> ();

          if (enbdev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              std::ofstream outFile1;
              outFile1.open (filename1.c_str (), std::ios_base::out | std::ios_base::app);
              if (!outFile1.is_open ())
                {
                  NS_LOG_ERROR ("Can't open file " << filename1);
                  return;
                }
              outFile1 << timestamp << "," << enbdev->GetCellId () << "," << pos.x << "," << pos.y
                       << "," << m_startTime << "," << "0" << "," << "30" << std::endl;
              outFile1.close ();
            }
          else if (mmdev)
            {
              Vector pos = node->GetObject<MobilityModel> ()->GetPosition ();
              std::ofstream outFile2;
              outFile2.open (filename2.c_str (), std::ios_base::out | std::ios_base::app);
              if (!outFile2.is_open ())
                {
                  NS_LOG_ERROR ("Can't open file " << filename2);
                  return;
                }

              // update ue_assoc_list from the current UE map of this gNB
              auto ueMap = mmdev->GetUeMap ();
              Ptr<MmWaveEnbPhy> enbPhy =
                  node->GetDevice (j)->GetObject<MmWaveEnbNetDevice> ()->GetPhy ();
              for (const auto &ue : ueMap)
                {
                  uint64_t imsi_assoc = ue.second->GetImsi ();
                  ue_assoc_list[imsi_assoc - 1] = mmdev->GetCellId ();
                }

              uint16_t cell_id  = mmdev->GetCellId ();
              double   es_power = enbPhy->GetTxPower ();
              esON_list[cell_id] = (es_power == 0) ? true : false;

              curr_total_energy_consumption += current_energy_consumption[cell_id];

              outFile2 << timestamp << "," << cell_id << "," << pos.x << "," << pos.y << ","
                       << m_startTime << "," << esON_list[cell_id] << ","
                       << current_energy_consumption[cell_id] << "," << max_energy_consumption
                       << "," << sum_curr_total_energy_consumption << std::endl;
              outFile2.close ();

              mmnode_iterator++;
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

// ─────────────────────────────────────────────────────────────────────────────
// ClearFile  –  truncate + write CSV header at simulation start
// ─────────────────────────────────────────────────────────────────────────────
void
ClearFile (std::string Filename, uint64_t m_startTime)
{
  std::ofstream outFile;
  outFile.open (Filename.c_str (), std::ios_base::out | std::ios_base::trunc);
  if (!outFile.is_open ())
    {
      NS_LOG_ERROR ("Can't open file " << Filename);
      return;
    }
  outFile.close ();

  uint64_t timestamp = m_startTime + (uint64_t)Simulator::Now ().GetMilliSeconds ();
  std::ofstream outFile1;
  outFile1.open (Filename.c_str (), std::ios_base::out | std::ios_base::app);

  if (Filename == "ue_position.txt")
    {
      outFile1 << "timestamp,id,x,y,type,cell,simid" << std::endl;
    }
  else
    {
      outFile1 << "timestamp,id,x,y,simid,ESstate,currEC,maxEC,totalcurrEC" << std::endl;
      outFile1 << timestamp << "," << "0" << "," << maxXAxis << "," << maxYAxis << std::endl;
    }
  outFile1.close ();
}

// ─────────────────────────────────────────────────────────────────────────────
// PrintPosition  –  PERIODIC CSV writer for GUI  (ue_position.txt)
// ─────────────────────────────────────────────────────────────────────────────
void
PrintPosition (Ptr<Node> node, int iterator, std::string Filename, uint64_t m_startTime)
{
  uint64_t timestamp = m_startTime + (uint64_t)Simulator::Now ().GetMilliSeconds ();

  int imsi;
  Ptr<Node> node1 = NodeList::GetNode (iterator);
  int nDevs       = node->GetNDevices ();
  std::string filename = Filename;
  std::ofstream outFile;

  for (int j = 0; j < nDevs; j++)
    {
      Ptr<McUeNetDevice>     mcuedev = node1->GetDevice (j)->GetObject<McUeNetDevice> ();
      Ptr<LteUeNetDevice>    uedev   = node->GetDevice (j)->GetObject<LteUeNetDevice> ();
      Ptr<MmWaveUeNetDevice> mmuedev = node->GetDevice (j)->GetObject<MmWaveUeNetDevice> ();

      if (mcuedev)
        {
          imsi = int (mcuedev->GetImsi ());
          int serving_cell = ue_assoc_list[imsi - 1];
          if (serving_cell == 0)
            {
              serving_cell = 1;
            }

          Ptr<MobilityModel> model    = node->GetObject<MobilityModel> ();
          Vector             position = model->GetPosition ();

          NS_LOG_UNCOND ("Position of UE with IMSI " << imsi << " is " << model->GetPosition ()
                                                     << " at time " << Simulator::Now ().GetSeconds ()
                                                     << ", UE connected to Cell: " << serving_cell);

          outFile.open (filename.c_str (), std::ios_base::out | std::ios_base::app);
          if (!outFile.is_open ())
            {
              NS_LOG_ERROR ("Can't open file " << filename);
              return;
            }
          outFile << timestamp << "," << imsi << "," << position.x << "," << position.y << ",mc,"
                  << serving_cell << "," << m_startTime << std::endl;
          outFile.close ();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PeriodicSinrDump  –  PERIODIC SINR logger for xApp monitoring
// This function runs every 100ms and writes SINR measurements to file
// It calculates SINR based on distance and propagation model
// ─────────────────────────────────────────────────────────────────────────────
void
PeriodicSinrDump(std::string filename, NodeContainer ueNodes, 
                 NetDeviceContainer mmWaveEnbDevs)
{
  std::ofstream outFile;
  outFile.open(filename, std::ios_base::out | std::ios_base::app);
  if (!outFile.is_open()) {
    NS_LOG_ERROR("Can't open SINR file " << filename);
    return;
  }

  uint64_t timestamp = Simulator::Now().GetMilliSeconds();
  
  // For each UE node
  for (uint32_t u = 0; u < ueNodes.GetN(); u++) {
    Ptr<Node> ueNode = ueNodes.Get(u);
    
    // Get MC UE device
    Ptr<McUeNetDevice> mcUeDev = nullptr;
    for (uint32_t j = 0; j < ueNode->GetNDevices(); j++) {
      mcUeDev = ueNode->GetDevice(j)->GetObject<McUeNetDevice>();
      if (mcUeDev) break;
    }
    if (!mcUeDev) continue;
    
    uint64_t imsi = mcUeDev->GetImsi();
    // Use IMSI-based RNTI for simplicity (IMSI * 10)
    uint16_t rnti = imsi * 10;
    
    Vector uePos = ueNode->GetObject<MobilityModel>()->GetPosition();
    
    // Measure SINR to all cells
    for (uint32_t c = 0; c < mmWaveEnbDevs.GetN(); c++) {
      Ptr<MmWaveEnbNetDevice> enbDev = 
        mmWaveEnbDevs.Get(c)->GetObject<MmWaveEnbNetDevice>();
      if (!enbDev) continue;
      
      uint16_t cellId = enbDev->GetCellId();
      
      Ptr<Node> enbNode = enbDev->GetNode();
      Vector enbPos = enbNode->GetObject<MobilityModel>()->GetPosition();
      
      // Calculate distance
      double dx = uePos.x - enbPos.x;
      double dy = uePos.y - enbPos.y;
      double dz = uePos.z - enbPos.z;
      double distance = std::sqrt(dx*dx + dy*dy + dz*dz);
      
      // Simple path loss model: PL(d) = PL(d0) + 10*n*log10(d/d0)
      // where n is path loss exponent (typically 2-4 for urban)
      double pathLossExponent = 3.5;
      double referenceDistance = 1.0; // meters
      double referenceLoss = 40.0; // dB at 1m
      
      double pathLoss = referenceLoss + 10.0 * pathLossExponent * 
                       std::log10(std::max(distance, referenceDistance) / referenceDistance);
      
      // Get transmit power from eNB
      Ptr<MmWaveEnbPhy> enbPhy = enbDev->GetPhy();
      double txPowerDbm = enbPhy->GetTxPower();
      
      // Received signal power
      double rxPowerDbm = txPowerDbm - pathLoss;
      
      // Noise power (typical values for mmWave)
      double noiseFigureDb = 7.0;
      double noisePowerDbm = -174.0 + 10.0 * std::log10(100e6) + noiseFigureDb; // 100 MHz bandwidth
      
      // Calculate SINR (simplified - no interference from other cells for now)
      double sinrDb = rxPowerDbm - noisePowerDbm;
      
      // Add some realistic variation
      std::default_random_engine generator(timestamp + imsi + cellId);
      std::normal_distribution<double> distribution(0.0, 2.0);
      double variation = distribution(generator);
      sinrDb += variation;
      
      // Write to file
      outFile << timestamp << "," << imsi << "," << cellId << "," 
              << rnti << "," << sinrDb << std::endl;
    }
  }
  
  outFile.close();
  
  // Schedule next report (100ms interval)
  Simulator::Schedule(MilliSeconds(100), &PeriodicSinrDump, 
                      filename, ueNodes, mmWaveEnbDevs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Energy helpers  –  trace callback + periodic log printer
// ─────────────────────────────────────────────────────────────────────────────
void
EnergyConsumptionUpdate (int nodeIndex, std::string filename, double totaloldEnergyConsumption,
                         double totalnewEnergyConsumption)
{
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
                 << (totalnewEnergyConsumption_storage[nodeIndex]
                     - totaloldEnergyConsumption_storage[nodeIndex])
                 << "J");
  current_energy_consumption[nodeIndex] =
      totalnewEnergyConsumption_storage[nodeIndex] - totaloldEnergyConsumption_storage[nodeIndex];
  totaloldEnergyConsumption_storage[nodeIndex] = totalnewEnergyConsumption_storage[nodeIndex];
}

// ─────────────────────────────────────────────────────────────────────────────
// Global simulation parameters
// ─────────────────────────────────────────────────────────────────────────────
static ns3::GlobalValue g_bufferSize ("bufferSize", "RLC tx buffer size (MB)",
                                      ns3::UintegerValue (10),
                                      ns3::MakeUintegerChecker<uint32_t> ());

static ns3::GlobalValue g_enableTraces ("enableTraces", "If true, generate ns-3 traces",
                                        ns3::BooleanValue (true), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2lteEnabled ("e2lteEnabled", "If true, send LTE E2 reports",
                                        ns3::BooleanValue (true), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2nrEnabled ("e2nrEnabled", "If true, send NR E2 reports",
                                       ns3::BooleanValue (true), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2du ("e2du", "If true, send DU reports", ns3::BooleanValue (true),
                                ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2cuUp ("e2cuUp", "If true, send CU-UP reports", ns3::BooleanValue (true),
                                  ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_e2cuCp ("e2cuCp", "If true, send CU-CP reports", ns3::BooleanValue (true),
                                  ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_reducedPmValues ("reducedPmValues",
                                           "If true, use a subset of the the pm containers",
                                           ns3::BooleanValue (false), ns3::MakeBooleanChecker ());

static ns3::GlobalValue
    g_hoSinrDifference ("hoSinrDifference",
                        "The value for which an handover between MmWave eNB is triggered",
                        ns3::DoubleValue (3), ns3::MakeDoubleChecker<double> ());

static ns3::GlobalValue
    g_indicationPeriodicity ("indicationPeriodicity",
                             "E2 Indication Periodicity reports (value in seconds)",
                             ns3::DoubleValue (0.1), ns3::MakeDoubleChecker<double> (0.01, 2.0));

static ns3::GlobalValue g_simTime ("simTime", "Simulation time in seconds", ns3::DoubleValue (1000),
                                   ns3::MakeDoubleChecker<double> (0.1, 1000.0));

static ns3::GlobalValue g_outageThreshold ("outageThreshold",
                                           "SNR threshold for outage events [dB]",
                                           ns3::DoubleValue (-5.0),
                                           ns3::MakeDoubleChecker<double> ());

static ns3::GlobalValue g_numberOfRaPreambles (
    "numberOfRaPreambles",
    "how many random access preambles are available for the contention based RACH process",
    ns3::UintegerValue (40),
    ns3::MakeUintegerChecker<uint8_t> ());

static ns3::GlobalValue
    g_handoverMode ("handoverMode",
                    "HO euristic to be used,"
                    "can be only \"NoAuto\", \"FixedTtt\", \"DynamicTtt\",   \"Threshold\"",
                    ns3::StringValue ("DynamicTtt"), ns3::MakeStringChecker ());

static ns3::GlobalValue g_e2TermIp ("e2TermIp", "The IP address of the RIC E2 termination",
                                    ns3::StringValue ("127.0.0.1"), ns3::MakeStringChecker ());

static ns3::GlobalValue
    g_enableE2FileLogging ("enableE2FileLogging",
                           "If true, generate offline file logging instead of connecting to RIC",
                           ns3::BooleanValue (false), ns3::MakeBooleanChecker ());

static ns3::GlobalValue g_controlFileName ("controlFileName",
                                           "The path to the control file (can be absolute)",
                                           ns3::StringValue (""),
                                           ns3::MakeStringChecker ());

// ─────────────────────────────────────────────────────────────────────────────
int
main (int argc, char *argv[])
{
  LogComponentEnable ("MmWaveHelper", LOG_LEVEL_ALL);

  // ── map size ─────────────────────────────────────────────────────────────
  maxXAxis = 4000;
  maxYAxis = 4000;

  // ── command line ─────────────────────────────────────────────────────────
  CommandLine cmd;
  cmd.Parse (argc, argv);

  bool harqEnabled = true;

  UintegerValue uintegerValue;
  BooleanValue  booleanValue;
  StringValue   stringValue;
  DoubleValue   doubleValue;

  // ── read globals ─────────────────────────────────────────────────────────
  GlobalValue::GetValueByName ("hoSinrDifference", doubleValue);
  double hoSinrDifference = doubleValue.Get ();
  GlobalValue::GetValueByName ("bufferSize", uintegerValue);
  uint32_t bufferSize = uintegerValue.Get ();
  GlobalValue::GetValueByName ("enableTraces", booleanValue);
  bool enableTraces = booleanValue.Get ();
  GlobalValue::GetValueByName ("outageThreshold", doubleValue);
  double outageThreshold = doubleValue.Get ();
  GlobalValue::GetValueByName ("handoverMode", stringValue);
  std::string handoverMode = stringValue.Get ();
  GlobalValue::GetValueByName ("e2TermIp", stringValue);
  std::string e2TermIp = stringValue.Get ();
  GlobalValue::GetValueByName ("enableE2FileLogging", booleanValue);
  bool enableE2FileLogging = booleanValue.Get ();
  GlobalValue::GetValueByName ("numberOfRaPreambles", uintegerValue);
  uint8_t numberOfRaPreambles = uintegerValue.Get ();

  NS_LOG_UNCOND ("bufferSize " << bufferSize << " OutageThreshold " << outageThreshold
                               << " HandoverMode " << handoverMode << " e2TermIp " << e2TermIp
                               << " enableE2FileLogging " << enableE2FileLogging);

  GlobalValue::GetValueByName ("e2lteEnabled", booleanValue);
  bool e2lteEnabled = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2nrEnabled", booleanValue);
  bool e2nrEnabled = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2du", booleanValue);
  bool e2du = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2cuUp", booleanValue);
  bool e2cuUp = booleanValue.Get ();
  GlobalValue::GetValueByName ("e2cuCp", booleanValue);
  bool e2cuCp = booleanValue.Get ();
  GlobalValue::GetValueByName ("reducedPmValues", booleanValue);
  bool reducedPmValues = booleanValue.Get ();
  GlobalValue::GetValueByName ("indicationPeriodicity", doubleValue);
  double indicationPeriodicity = doubleValue.Get ();
  GlobalValue::GetValueByName ("controlFileName", stringValue);
  std::string controlFilename = stringValue.Get ();

  NS_LOG_UNCOND ("e2lteEnabled " << e2lteEnabled << " e2nrEnabled " << e2nrEnabled << " e2du "
                                 << e2du << " e2cuCp " << e2cuCp << " e2cuUp " << e2cuUp
                                 << " controlFilename " << controlFilename
                                 << " indicationPeriodicity " << indicationPeriodicity);

  // ── ns3 defaults ─────────────────────────────────────────────────────────
  Config::SetDefault ("ns3::LteEnbNetDevice::ControlFileName", StringValue (controlFilename));
  Config::SetDefault ("ns3::LteEnbNetDevice::E2Periodicity", DoubleValue (indicationPeriodicity));
  Config::SetDefault ("ns3::MmWaveEnbNetDevice::E2Periodicity",
                      DoubleValue (indicationPeriodicity));

  Config::SetDefault ("ns3::MmWaveHelper::E2ModeLte", BooleanValue (e2lteEnabled));
  Config::SetDefault ("ns3::MmWaveHelper::E2ModeNr", BooleanValue (e2nrEnabled));

  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableDuReport", BooleanValue (e2du));

  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableCuUpReport", BooleanValue (e2cuUp));
  Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuUpReport", BooleanValue (e2cuUp));

  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableCuCpReport", BooleanValue (e2cuCp));
  Config::SetDefault ("ns3::LteEnbNetDevice::EnableCuCpReport", BooleanValue (e2cuCp));

  Config::SetDefault ("ns3::MmWaveEnbNetDevice::ReducedPmValues", BooleanValue (reducedPmValues));
  Config::SetDefault ("ns3::LteEnbNetDevice::ReducedPmValues", BooleanValue (reducedPmValues));

  Config::SetDefault ("ns3::LteEnbNetDevice::EnableE2FileLogging",
                      BooleanValue (enableE2FileLogging));
  Config::SetDefault ("ns3::MmWaveEnbNetDevice::EnableE2FileLogging",
                      BooleanValue (enableE2FileLogging));

  Config::SetDefault ("ns3::MmWaveEnbMac::NumberOfRaPreambles",
                      UintegerValue (numberOfRaPreambles));

  Config::SetDefault ("ns3::MmWaveHelper::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWaveHelper::UseIdealRrc", BooleanValue (true));
  Config::SetDefault ("ns3::MmWaveHelper::E2TermIp", StringValue (e2TermIp));

  Config::SetDefault ("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::NumHarqProcess", UintegerValue (100));

  Config::SetDefault ("ns3::PhasedArrayModel::AntennaElement",
                      PointerValue (CreateObject<IsotropicAntennaModel> ()));
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
                      TimeValue (MilliSeconds (100.0)));
  Config::SetDefault ("ns3::ThreeGppChannelConditionModel::UpdatePeriod",
                      TimeValue (MilliSeconds (100)));

  Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue (MilliSeconds (10.0)));
  Config::SetDefault ("ns3::LteRlcUmLowLat::ReportBufferStatusTimer",
                      TimeValue (MilliSeconds (10.0)));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize",
                      UintegerValue (bufferSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcUmLowLat::MaxTxBufferSize",
                      UintegerValue (bufferSize * 1024 * 1024));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize",
                      UintegerValue (bufferSize * 1024 * 1024));

  Config::SetDefault ("ns3::LteEnbRrc::OutageThreshold", DoubleValue (outageThreshold));
  Config::SetDefault ("ns3::LteEnbRrc::SecondaryCellHandoverMode", StringValue (handoverMode));
  Config::SetDefault ("ns3::LteEnbRrc::HoSinrDifference", DoubleValue (hoSinrDifference));

  // ── radio parameters (kept from original ScenarioThree) ─────────────────
  double bandwidth       = 20e6;
  double centerFrequency = 3.5e9;
  double isd_ue          = 600;
  double isd_cell        = 500;
  int    numAntennasMcUe    = 1;
  int    numAntennasMmWave  = 1;

  NS_LOG_INFO ("Bandwidth " << bandwidth << " centerFrequency " << double (centerFrequency)
                            << " isd_ue " << isd_ue << " numAntennasMcUe " << numAntennasMcUe
                            << " numAntennasMmWave " << numAntennasMmWave);

  Config::SetDefault ("ns3::McUeNetDevice::AntennaNum", UintegerValue (numAntennasMcUe));
  Config::SetDefault ("ns3::MmWaveNetDevice::AntennaNum", UintegerValue (numAntennasMmWave));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue (bandwidth));
  Config::SetDefault ("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue (centerFrequency));

  // ── mmWave helper ────────────────────────────────────────────────────────
  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper> ();
  mmwaveHelper->SetPathlossModelType ("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmwaveHelper->SetChannelConditionModelType ("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

  Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper> ();
  mmwaveHelper->SetEpcHelper (epcHelper);

  // ── topology sizes (kept from original ScenarioThree) ───────────────────
  uint8_t  nMmWaveEnbNodes = 4;
  uint8_t  nLteEnbNodes    = 1;
  uint32_t ues             = 10;
  uint8_t  nUeNodes        = ues;
  num_of_mmdev             = nMmWaveEnbNodes;   // store for energy callbacks

  NS_LOG_INFO (" Bandwidth " << bandwidth << " centerFrequency " << double (centerFrequency)
                             << " isd_cell " << isd_cell << " numAntennasMcUe " << numAntennasMcUe
                             << " numAntennasMmWave " << numAntennasMmWave << " nMmWaveEnbNodes "
                             << unsigned (nMmWaveEnbNodes));

  // ── Internet / PGW ───────────────────────────────────────────────────────
  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);

  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (2500));
  p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (0.010)));
  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
  [[maybe_unused]] Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress (1);
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
      ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  // ── nodes ────────────────────────────────────────────────────────────────
  NodeContainer ueNodes;
  NodeContainer mmWaveEnbNodes;
  NodeContainer lteEnbNodes;
  NodeContainer allEnbNodes;
  mmWaveEnbNodes.Create (nMmWaveEnbNodes);
  lteEnbNodes.Create (nLteEnbNodes);
  ueNodes.Create (nUeNodes);
  allEnbNodes.Add (lteEnbNodes);
  allEnbNodes.Add (mmWaveEnbNodes);
  NodeContainerManager::GetInstance ().SetMmWaveEnbNodes (mmWaveEnbNodes);

  // ── positions ────────────────────────────────────────────────────────────
  Vector centerPosition = Vector (maxXAxis / 2, maxYAxis / 2, 3);

  Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator> ();
  enbPositionAlloc->Add (centerPosition);   // LTE eNB
  enbPositionAlloc->Add (centerPosition);   // mmWave co-located

  double x, y;
  double nConstellation = nMmWaveEnbNodes - 1;
  for (int8_t i = 0; i < nConstellation; ++i)
    {
      x = isd_cell * cos ((2 * M_PI * i) / nConstellation);
      y = isd_cell * sin ((2 * M_PI * i) / nConstellation);
      enbPositionAlloc->Add (Vector (centerPosition.x + x, centerPosition.y + y, 3));
    }

  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  enbmobility.SetPositionAllocator (enbPositionAlloc);
  enbmobility.Install (allEnbNodes);

  // UE mobility
  Ptr<UniformDiscPositionAllocator> uePositionAlloc =
      CreateObject<UniformDiscPositionAllocator> ();
  uePositionAlloc->SetX (centerPosition.x);
  uePositionAlloc->SetY (centerPosition.y);
  uePositionAlloc->SetRho (isd_ue);

  Ptr<UniformRandomVariable> speed = CreateObject<UniformRandomVariable> ();
  speed->SetAttribute ("Min", DoubleValue (60.0));
  speed->SetAttribute ("Max", DoubleValue (90.0));

  MobilityHelper uemobility;
  uemobility.SetMobilityModel ("ns3::RandomWalk2dOutdoorMobilityModel", "Speed",
                               PointerValue (speed), "Bounds",
                               RectangleValue (Rectangle (0, maxXAxis, 0, maxYAxis)));
  uemobility.SetPositionAllocator (uePositionAlloc);
  uemobility.Install (ueNodes);

  // ── devices ──────────────────────────────────────────────────────────────
  NetDeviceContainer lteEnbDevs    = mmwaveHelper->InstallLteEnbDevice (lteEnbNodes);
  NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice (mmWaveEnbNodes);
  NetDeviceContainer mcUeDevs      = mmwaveHelper->InstallMcUeDevice (ueNodes);

  // ── IP on UEs ────────────────────────────────────────────────────────────
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (mcUeDevs));
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      Ptr<Ipv4StaticRouting> ueStaticRouting =
          ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  // ── X2 + attach ──────────────────────────────────────────────────────────
  mmwaveHelper->AddX2Interface (lteEnbNodes, mmWaveEnbNodes);
  mmwaveHelper->AttachToClosestEnb (mcUeDevs, mmWaveEnbDevs, lteEnbDevs);

  // ── SINR periodic dump ───────────────────────────────────────────────────
  std::string sinr_file = "ue_sinr.txt";
  std::ofstream sinrOut;
  sinrOut.open(sinr_file, std::ios_base::out | std::ios_base::trunc);
  if (sinrOut.is_open()) {
    sinrOut << "timestamp,imsi,cellId,rnti,sinr_db" << std::endl;
    sinrOut.close();
    NS_LOG_UNCOND("SINR file initialized: " << sinr_file);
  }

  // Start periodic SINR dump after 0.5 seconds (let simulation stabilize)
  Simulator::Schedule(Seconds(0.5), &PeriodicSinrDump, 
                      sinr_file, ueNodes, mmWaveEnbDevs);
  
  NS_LOG_UNCOND("SINR periodic dump enabled -> " << sinr_file << " (every 100ms)");

  // ── energy model on mmWave eNBs ──────────────────────────────────────────
  BasicEnergySourceHelper basicEnergySourceHelper;
  basicEnergySourceHelper.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (1000000000000));
  basicEnergySourceHelper.Set ("BasicEnergySupplyVoltageV", DoubleValue (5.0));
  energy::EnergySourceContainer sources =
      basicEnergySourceHelper.Install (mmWaveEnbNodes);

  MmWaveRadioEnergyModelEnbHelper nrEnbHelper;
  energy::DeviceEnergyModelContainer deviceEModel =
      nrEnbHelper.Install (mmWaveEnbDevs, sources);

  // ── simulation timing ────────────────────────────────────────────────────
  GlobalValue::GetValueByName ("simTime", doubleValue);
  double simTime  = doubleValue.Get ();
  int    numPrints = simTime / 0.1;

  // ── energy CSV files ─────────────────────────────────────────────────────
  std::vector<std::ofstream> outFiles;
  for (int i = 0; i < nMmWaveEnbNodes; ++i)
    {
      std::ostringstream energyFileName;
      energyFileName << "energyfilecell" << i + 2 << ".csv";
      std::ofstream outFile;
      outFile.open (energyFileName.str (), std::ios_base::out | std::ios_base::trunc);
      outFile << "Time,NetEnergy,DiffEnergy" << std::endl;
      outFiles.push_back (std::move (outFile));
    }

  // connect energy traces + schedule periodic energy prints
  for (int i = 0; i < nMmWaveEnbNodes; ++i)
    {
      std::ostringstream filename;
      filename << "energyfilecell" << i + 2 << ".csv";
      deviceEModel.Get (i)->TraceConnectWithoutContext (
          "TotalEnergyConsumption",
          MakeBoundCallback (&EnergyConsumptionUpdate, i, filename.str ()));
      for (int p = 0; p < numPrints; p++)
        {
          Simulator::Schedule (Seconds (p * simTime / numPrints),
                               &EnergyConsumptionPrint, i);
        }
    }

  // ── applications (UDP downlink, kept from original ScenarioThree) ────────
  uint16_t portUdp = 60000;
  Address  sinkLocalAddressUdp (InetSocketAddress (Ipv4Address::GetAny (), portUdp));
  PacketSinkHelper sinkHelperUdp ("ns3::UdpSocketFactory", sinkLocalAddressUdp);

  ApplicationContainer sinkApp;
  sinkApp.Add (sinkHelperUdp.Install (remoteHost));

  ApplicationContainer clientApp;
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      PacketSinkHelper dlPacketSinkHelper ("ns3::UdpSocketFactory",
                                           InetSocketAddress (Ipv4Address::GetAny (), 1234));
      sinkApp.Add (dlPacketSinkHelper.Install (ueNodes.Get (u)));
      UdpClientHelper dlClient (ueIpIface.GetAddress (u), 1234);
      dlClient.SetAttribute ("Interval", TimeValue (MicroSeconds (500)));
      dlClient.SetAttribute ("MaxPackets", UintegerValue (UINT32_MAX));
      dlClient.SetAttribute ("PacketSize", UintegerValue (1280));   // original value
      clientApp.Add (dlClient.Install (remoteHost));
    }

  sinkApp.Start (Seconds (0));
  clientApp.Start (MilliSeconds (100));
  clientApp.Stop (Seconds (simTime - 0.1));

  // ── GUI file init  –  clear + write headers ──────────────────────────────
  struct timeval time_now{};
  gettimeofday (&time_now, nullptr);
  uint64_t t_startTime_simid = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);

  std::string ue_poss_out = "ue_position.txt";
  ClearFile (ue_poss_out, t_startTime_simid);
  ClearFile ("enbs.txt",  t_startTime_simid);
  ClearFile ("gnbs.txt",  t_startTime_simid);

  // one-shot gnuplot label file (optional, GUI doesn't need it)
  PrintGnuplottableUeListToFile ("ues.txt");

  // ── periodic GUI file writers ─────────────────────────────────────────────
  int nodecount   = int (NodeList::GetNNodes ());
  int UE_iterator = nodecount - int (nUeNodes);

  for (int i = 0; i < numPrints; i++)
    {
      Simulator::Schedule (Seconds (i * simTime / numPrints),
                           &PrintGnuplottableEnbListToFile, t_startTime_simid);
      for (uint32_t j = 0; j < ueNodes.GetN (); j++)
        {
          Simulator::Schedule (Seconds (i * simTime / numPrints),
                               &PrintPosition, ueNodes.Get (j),
                               j + UE_iterator, ue_poss_out, t_startTime_simid);
        }
    }

  // ── traces ───────────────────────────────────────────────────────────────
  if (enableTraces)
    {
      mmwaveHelper->EnableTraces ();
    }

  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  lteHelper->Initialize ();
  lteHelper->EnablePhyTraces ();
  lteHelper->EnableMacTraces ();

  // ── run ──────────────────────────────────────────────────────────────────
  NS_LOG_UNCOND ("Simulation time is " << simTime << " seconds ");
  Simulator::Stop (Seconds (simTime));
  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();

  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
  return 0;
}
