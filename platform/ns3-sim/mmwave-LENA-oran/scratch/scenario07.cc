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
 * MODIFIED: Extended to demonstrate inter-gNB handover
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
#include <random>
#include <chrono>
#include <cmath>
#include <fstream>
#include "ns3/basic-energy-source-helper.h"
#include "ns3/mmwave-radio-energy-model-enb-helper.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/netanim-module.h"

using namespace ns3;
using namespace mmwave;

std::map<uint64_t, uint16_t> imsi_cellid;
std::map<uint16_t, std::set<uint64_t>> imsi_list;
std::map<uint16_t, Ptr < Node>>
cellid_node;
std::map<uint32_t, uint16_t> ue_cellid_usinghandover;
std::map<uint64_t, uint32_t> ueimsi_nodeid;
int ue_assoc_list[10] = {0};
double maxXAxis;
double maxYAxis;
bool esON_list[10] = {0};
double totalnewEnergyConsumption_storage[10] = {0};
double totaloldEnergyConsumption_storage[10] = {0};
double current_energy_consumption[10] = {0};
double curr_total_energy_consumption = 0;
double max_energy_consumption = 0;
double sum_curr_total_energy_consumption = 0;
int num_of_mmdev = 0;

/**
 * Scenario Zero - Extended for Inter-gNB Handover Demonstration
 *
 * Key Changes for Handover:
 * 1. Simplified to 1 LTE eNB + 2 mmWave gNBs + 1 UE
 * 2. gNBs positioned 400m apart (sufficient coverage overlap)
 * 3. UE uses ConstantVelocityMobilityModel with high speed (30 m/s)
 * 4. UE moves in straight line from gNB1 to gNB2
 * 5. Aggressive handover thresholds to ensure HO triggers
 * 6. Enhanced RRC logging to verify handover
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

  uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();
  std::string filename1 = "enbs.txt";
  std::string filename2 = "gnbs.txt";
  
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
    uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();
    std::ofstream outFile1;
    outFile1.open(filename.c_str(), std::ios_base::out | std::ios_base::app);

  if (Filename == "ue_position.txt") {
      outFile1 << "timestamp,id,x,y,type,cell,simid" << std::endl;
    }
  else
    {
      outFile1 << "timestamp,id,x,y,simid,ESstate,currEC,maxEC,totalcurrEC" << std::endl;
      outFile1 << timestamp << "," << "0" << "," << maxXAxis << "," << maxYAxis << std::endl;
    }
    outFile1.close();
}

void
PrintPosition(Ptr<Node> node, int iterator, std::string Filename, uint64_t m_startTime) {

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
    }
}

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

// MODIFIED: More aggressive handover threshold for demonstration
static ns3::GlobalValue
        g_hoSinrDifference("hoSinrDifference",
                           "The value for which an handover between MmWave eNB is triggered",
                           ns3::DoubleValue(0.5), ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue
        g_indicationPeriodicity("indicationPeriodicity",
                                "E2 Indication Periodicity reports (value in seconds)",
                                ns3::DoubleValue(0.1), ns3::MakeDoubleChecker<double>(0.01, 2.0));

static ns3::GlobalValue g_simTime("simTime", "Simulation time in seconds", ns3::DoubleValue(30),
                                  ns3::MakeDoubleChecker<double>(0.1, 100000.0));

static ns3::GlobalValue g_outageThreshold("outageThreshold",
                                          "SNR threshold for outage events [dB]",
                                          ns3::DoubleValue(-5.0),
                                          ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue g_numberOfRaPreambles(
        "numberOfRaPreambles",
        "how many random access preambles are available for the contention based RACH process",
        ns3::UintegerValue(40),
        ns3::MakeUintegerChecker<uint8_t>());

static ns3::GlobalValue
        g_handoverMode("handoverMode",
                       "HO euristic to be used,"
                       "can be only \"NoAuto\", \"FixedTtt\", \"DynamicTtt\",   \"Threshold\"",
                       ns3::StringValue("DynamicTtt"), ns3::MakeStringChecker());

static ns3::GlobalValue g_e2TermIp("e2TermIp", "The IP address of the RIC E2 termination",
                                   ns3::StringValue("127.0.0.1"), ns3::MakeStringChecker());

static ns3::GlobalValue
        g_enableE2FileLogging("enableE2FileLogging",
                              "If true, generate offline file logging instead of connecting to RIC",
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

// MODIFIED: Simplified topology for handover demonstration
static ns3::GlobalValue mmWave_nodes ("N_MmWaveEnbNodes", "Number of mmWaveNodes",
                                      ns3::UintegerValue (2),
                                      ns3::MakeUintegerChecker<uint8_t> ());

static ns3::GlobalValue ue_s ("N_Ues", "Number of User Equipments",
                                      ns3::UintegerValue (1),
                                      ns3::MakeUintegerChecker<uint32_t> ());

static ns3::GlobalValue center_freq ("CenterFrequency", "Center Frequency Value",
                                      ns3::DoubleValue (3.5e9),
                                      ns3::MakeDoubleChecker<double> ());

static ns3::GlobalValue bandwidth_value ("Bandwidth", "Bandwidth Value",
                                      ns3::DoubleValue (20e6),
                                      ns3::MakeDoubleChecker<double> ());

// MODIFIED: Adjusted distances for handover demonstration
static ns3::GlobalValue interside_distance_value_ue ("IntersideDistanceUEs", "Interside Distance Value",
                                      ns3::DoubleValue (500),
                                      ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue interside_distance_value_cell ("IntersideDistanceCells", "Interside Distance Value",
                                                  ns3::DoubleValue (400),
                                                  ns3::MakeDoubleChecker<double> ());

int
main(int argc, char *argv[]) {
    // Enable detailed RRC logging to verify handover
    // Note: Use available logging components in your ns-3 build
    LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
    LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);
    LogComponentEnable("MmWaveEnbNetDevice", LOG_LEVEL_INFO);
    LogComponentEnable("MmWaveUeNetDevice", LOG_LEVEL_INFO);

    maxXAxis = 1000;
    maxYAxis = 1000;

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

    NS_LOG_UNCOND("=== HANDOVER DEMONSTRATION SCENARIO ===");
    NS_LOG_UNCOND("bufferSize " << bufferSize << " OutageThreshold " << outageThreshold
                                << " HandoverMode " << handoverMode << " hoSinrDifference " << hoSinrDifference
                                << " e2TermIp " << e2TermIp
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

    Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableDuReport", BooleanValue(e2du));

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
    
    // CRITICAL: Enable A3 RSRP measurements for handover
    Config::SetDefault("ns3::LteEnbRrc::RsrpFilterCoefficient", UintegerValue(4)); // Fast measurements
    Config::SetDefault("ns3::LteEnbRrc::RsrqFilterCoefficient", UintegerValue(4));
    
    Config::SetDefault("ns3::ThreeGppPropagationLossModel::Frequency",DoubleValue(3.5e9));
    Config::SetDefault("ns3::ThreeGppPropagationLossModel::ShadowingEnabled",BooleanValue(false));
    
    GlobalValue::GetValueByName ("Bandwidth", doubleValue);
    double bandwidth = doubleValue.Get();
    GlobalValue::GetValueByName ("CenterFrequency", doubleValue);
    double centerFrequency = doubleValue.Get();
    GlobalValue::GetValueByName ("IntersideDistanceUEs", doubleValue);
    double isd_ue = doubleValue.Get();
    GlobalValue::GetValueByName ("IntersideDistanceCells", doubleValue);
    double isd_cell = doubleValue.Get();

    int numAntennasMcUe = 1;
    int numAntennasMmWave = 1;

    NS_LOG_INFO("Bandwidth " << bandwidth << " centerFrequency " << double(centerFrequency)
                             << " isd_ue " << isd_ue << " numAntennasMcUe " << numAntennasMcUe
                             << " numAntennasMmWave " << numAntennasMmWave);

    Config::SetDefault("ns3::McUeNetDevice::AntennaNum", UintegerValue(numAntennasMcUe));
    Config::SetDefault("ns3::MmWaveNetDevice::AntennaNum", UintegerValue(numAntennasMmWave));
    Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(bandwidth));
    Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(centerFrequency));

    Ptr <MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
    mmwaveHelper->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
    mmwaveHelper->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

    Ptr <MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
    mmwaveHelper->SetEpcHelper(epcHelper);

    GlobalValue::GetValueByName ("N_MmWaveEnbNodes", uintegerValue);
    uint8_t nMmWaveEnbNodes = uintegerValue.Get();
    uint8_t nLteEnbNodes = 1;
    GlobalValue::GetValueByName ("N_Ues", uintegerValue);
    uint32_t ues = uintegerValue.Get();
    uint8_t nUeNodes = ues;
    
    NS_LOG_UNCOND("=== Topology: " << unsigned(nLteEnbNodes) << " LTE eNB + " 
                  << unsigned(nMmWaveEnbNodes) << " mmWave gNBs + " 
                  << unsigned(nUeNodes) << " UE ===");

    Ptr <Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr <Node> remoteHost = remoteHostContainer.Get(0);
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(2500));
    p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.010)));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
    Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1);
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr <Ipv4StaticRouting> remoteHostStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

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

    // MODIFIED: Simplified positioning for handover demonstration
    // LTE eNB at center, 2 mmWave gNBs positioned 500m apart for stronger SINR difference
    Vector centerPosition = Vector(maxXAxis / 2, maxYAxis / 2, 3);

    Ptr <ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();

    // LTE eNB at center
    enbPositionAlloc->Add(centerPosition);
    
    // mmWave gNB 1: 250m to the left of center
    enbPositionAlloc->Add(Vector(centerPosition.x - 250, centerPosition.y, 3));
    
    // mmWave gNB 2: 250m to the right of center
    enbPositionAlloc->Add(Vector(centerPosition.x + 250, centerPosition.y, 3));

    NS_LOG_UNCOND("=== gNB Positions ===");
    NS_LOG_UNCOND("LTE eNB: (" << centerPosition.x << ", " << centerPosition.y << ")");
    NS_LOG_UNCOND("mmWave gNB 1: (" << (centerPosition.x - 250) << ", " << centerPosition.y << ")");
    NS_LOG_UNCOND("mmWave gNB 2: (" << (centerPosition.x + 250) << ", " << centerPosition.y << ")");

    MobilityHelper enbmobility;
    enbmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbmobility.SetPositionAllocator(enbPositionAlloc);
    enbmobility.Install(allEnbNodes);

    // MODIFIED: UE mobility for handover demonstration
    // UE starts near gNB1, moves at high speed toward gNB2
    MobilityHelper uemobility;
    
    Ptr<ListPositionAllocator> uePositionAlloc = CreateObject<ListPositionAllocator>();
    // UE starts 80m to the left of gNB1 (ensuring initial attachment to gNB1)
    Vector ueStartPos = Vector(centerPosition.x - 330, centerPosition.y, 1.5);
    uePositionAlloc->Add(ueStartPos);
    
    uemobility.SetPositionAllocator(uePositionAlloc);
    uemobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    uemobility.Install(ueNodes);
    
    // Set UE velocity: moving from left to right at 40 m/s (faster for clearer handover)
    // This will cause UE to cross from gNB1 coverage to gNB2 coverage
    Ptr<ConstantVelocityMobilityModel> cvMobility = 
        ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    cvMobility->SetVelocity(Vector(40.0, 0.0, 0.0)); // 40 m/s to the right
    
    NS_LOG_UNCOND("=== UE Mobility ===");
    NS_LOG_UNCOND("UE Start Position: (" << ueStartPos.x << ", " << ueStartPos.y << ")");
    NS_LOG_UNCOND("UE Velocity: 40 m/s (moving left to right)");
    NS_LOG_UNCOND("Distance between gNBs: 500m");
    NS_LOG_UNCOND("Expected to trigger handover from gNB1 (Cell 2) to gNB2 (Cell 3)");

    // Install mmWave, lte, mc Devices to the nodes
    NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice(lteEnbNodes);
    NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice(mmWaveEnbNodes);
    NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice(ueNodes);

    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIface;
    ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(mcUeDevs));
    
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
        Ptr <Node> ueNode = ueNodes.Get(u);
        Ptr <Ipv4StaticRouting> ueStaticRouting =
                ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    // Add X2 interfaces
    mmwaveHelper->AddX2Interface(lteEnbNodes, mmWaveEnbNodes);

    // CRITICAL: Attach only ONCE - this is the initial registration
    NS_LOG_UNCOND("=== Performing Initial Attachment (ONCE ONLY) ===");
    mmwaveHelper->AttachToClosestEnb(mcUeDevs, mmWaveEnbDevs, lteEnbDevs);
    NS_LOG_UNCOND("=== Initial Attachment Complete ===");

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

    // Install and start applications
    uint16_t portUdp = 60000;
    Address sinkLocalAddressUdp(InetSocketAddress(Ipv4Address::GetAny(), portUdp));
    PacketSinkHelper sinkHelperUdp("ns3::UdpSocketFactory", sinkLocalAddressUdp);
    AddressValue serverAddressUdp(InetSocketAddress(remoteHostAddr, portUdp));

    ApplicationContainer sinkApp;
    sinkApp.Add(sinkHelperUdp.Install(remoteHost));

    ApplicationContainer clientApp;

    for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
        // Continuous traffic to keep UE in RRC_CONNECTED
        PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",
                                            InetSocketAddress(Ipv4Address::GetAny(), 1234));
        sinkApp.Add(dlPacketSinkHelper.Install(ueNodes.Get(u)));
        UdpClientHelper dlClient(ueIpIface.GetAddress(u), 1234);
        dlClient.SetAttribute("Interval", TimeValue(MicroSeconds(500)));
        dlClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
        dlClient.SetAttribute("PacketSize", UintegerValue(200));
        clientApp.Add(dlClient.Install(remoteHost));
    }

    sinkApp.Start(Seconds(0));
    clientApp.Start(MilliSeconds(100));
    clientApp.Stop(Seconds(simTime - 0.1));

    struct timeval time_now{};
    gettimeofday(&time_now, nullptr);
    uint64_t t_startTime_simid = (time_now.tv_sec * 1000) + (time_now.tv_usec / 1000);
    std::string ue_poss_out = "ue_position.txt";
    ClearFile(ue_poss_out, t_startTime_simid);
    ClearFile("enbs.txt", t_startTime_simid);
    ClearFile("gnbs.txt", t_startTime_simid);
    
    PrintGnuplottableUeListToFile("ues.txt");

    int nodecount = int(NodeList::GetNNodes());
    int UE_iterator = nodecount - int(nUeNodes);

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

    Ptr <LteHelper> lteHelper = CreateObject<LteHelper>();
    lteHelper->Initialize();
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();

    // Generate NetAnim XML for visualization
    AnimationInterface anim("scenario_0_handover.xml");
    anim.SetMaxPktsPerTraceFile(500000);
    
    // Set node descriptions
    anim.UpdateNodeDescription(lteEnbNodes.Get(0), "LTE-eNB");
    anim.UpdateNodeDescription(mmWaveEnbNodes.Get(0), "gNB-1");
    anim.UpdateNodeDescription(mmWaveEnbNodes.Get(1), "gNB-2");
    anim.UpdateNodeDescription(ueNodes.Get(0), "UE");
    
    // Set colors for visualization
    anim.UpdateNodeColor(lteEnbNodes.Get(0), 0, 255, 0); // Green for LTE
    anim.UpdateNodeColor(mmWaveEnbNodes.Get(0), 255, 0, 0); // Red for gNB1
    anim.UpdateNodeColor(mmWaveEnbNodes.Get(1), 0, 0, 255); // Blue for gNB2
    anim.UpdateNodeColor(ueNodes.Get(0), 255, 255, 0); // Yellow for UE

    NS_LOG_UNCOND("=== Starting Simulation ===");
    NS_LOG_UNCOND("Simulation time: " << simTime << " seconds");
    NS_LOG_UNCOND("Watch for handover logs in output!");
    NS_LOG_UNCOND("Expected handover sequence:");
    NS_LOG_UNCOND("  1. UE initially attached to gNB1 (Cell ID 2)");
    NS_LOG_UNCOND("  2. UE moves toward gNB2 at 30 m/s");
    NS_LOG_UNCOND("  3. SINR from gNB2 improves, measurements trigger HO");
    NS_LOG_UNCOND("  4. Handover from gNB1 to gNB2 (Cell ID 3)");
    NS_LOG_UNCOND("=====================================");
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    NS_LOG_UNCOND("=== Simulation Complete ===");
    NS_LOG_UNCOND("Check logs above for handover events");
    NS_LOG_UNCOND("NetAnim file: scenario_0_handover.xml");

    Simulator::Destroy();
    return 0;
}
