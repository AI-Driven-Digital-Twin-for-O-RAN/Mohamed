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
 *          Modified for Handover Demo
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
#include "ns3/netanim-module.h"  // Added for NetAnim visualization
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
 * Scenario Zero - Extended for Handover Demo
 * 
 * Key Changes:
 * 1. Fixed gNB positions in triangular layout (400m separation)
 * 2. Fixed UE starting positions near specific gNBs
 * 3. ConstantVelocityMobilityModel for controlled movement
 * 4. NetAnim output for visualization
 * 5. Enhanced handover logging
 */

NS_LOG_COMPONENT_DEFINE ("ScenarioZero");

// Callback to log handover events
void
NotifyHandoverStartUe (std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCellId)
{
  NS_LOG_UNCOND ("HO_START: Time=" << Simulator::Now().GetSeconds() 
                 << "s IMSI=" << imsi 
                 << " From_Cell=" << cellId 
                 << " To_Cell=" << targetCellId 
                 << " RNTI=" << rnti);
}

void
NotifyHandoverEndOkUe (std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  NS_LOG_UNCOND ("HO_COMPLETE: Time=" << Simulator::Now().GetSeconds() 
                 << "s IMSI=" << imsi 
                 << " New_Cell=" << cellId 
                 << " RNTI=" << rnti);
}

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
  else
    {
      outFile1 << "timestamp,id,x,y,simid,ESstate,currEC,maxEC,totalcurrEC" << std::endl;
      outFile1 << timestamp << "," << "0" << "," << maxXAxis << "," << maxYAxis << std::endl;
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

static ns3::GlobalValue
        g_hoSinrDifference("hoSinrDifference",
                           "The value for which an handover between MmWave eNB is triggered",
                           ns3::DoubleValue(3), ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue
        g_indicationPeriodicity("indicationPeriodicity",
                                "E2 Indication Periodicity reports (value in seconds)",
                                ns3::DoubleValue(0.1), ns3::MakeDoubleChecker<double>(0.01, 2.0));

static ns3::GlobalValue g_simTime("simTime", "Simulation time in seconds", ns3::DoubleValue(60),
                                  ns3::MakeDoubleChecker<double>(0.1, 100000.0));

static ns3::GlobalValue g_outageThreshold("outageThreshold",
                                          "SNR threshold for outage events [dB]", // use -1000.0 with NoAuto
                                          ns3::DoubleValue(-5.0),
                                          ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue g_numberOfRaPreambles(
        "numberOfRaPreambles",
        "how many random access preambles are available for the contention based RACH process",
        ns3::UintegerValue(40), // Indicated for TS use case, 52 is default
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

// HANDOVER DEMO: Fixed topology parameters
static ns3::GlobalValue mmWave_nodes ("N_MmWaveEnbNodes", "Number of mmWaveNodes",
                                      ns3::UintegerValue (3),  // Changed to 3 gNBs
                                      ns3::MakeUintegerChecker<uint8_t> ());

static ns3::GlobalValue ue_s ("N_Ues", "Number of User Equipments",
                                      ns3::UintegerValue (6),  // Changed to 6 UEs
                                      ns3::MakeUintegerChecker<uint32_t> ());

static ns3::GlobalValue center_freq ("CenterFrequency", "Center Frequency Value",
                                      ns3::DoubleValue (28e9),  // mmWave frequency
                                      ns3::MakeDoubleChecker<double> ());

static ns3::GlobalValue bandwidth_value ("Bandwidth", "Bandwidth Value",
                                      ns3::DoubleValue (100e6),  // 100 MHz bandwidth
                                      ns3::MakeDoubleChecker<double> ());

static ns3::GlobalValue interside_distance_value_ue ("IntersideDistanceUEs", "Interside Distance Value",
                                      ns3::DoubleValue (50),  // UE initial offset
                                      ns3::MakeDoubleChecker<double> ());
static ns3::GlobalValue interside_distance_value_cell ("IntersideDistanceCells", "Interside Distance Value",
                                                  ns3::DoubleValue (400),  // 400m cell separation
                                                  ns3::MakeDoubleChecker<double> ());

int
main(int argc, char *argv[]) {
    // Enable critical logging for handover visibility
    LogComponentEnable ("LteEnbRrc", LOG_LEVEL_INFO);
    LogComponentEnable ("EpcX2", LOG_LEVEL_INFO);

    // HANDOVER DEMO: Smaller scenario for clarity
    maxXAxis = 1200;
    maxYAxis = 1200;

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

    NS_LOG_UNCOND("=== HANDOVER DEMO CONFIGURATION ===");
    NS_LOG_UNCOND("bufferSize " << bufferSize << " OutageThreshold " << outageThreshold
                                << " HandoverMode " << handoverMode << " hoSinrDifference " << hoSinrDifference);

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

    // set to false to use the 3GPP radiation pattern
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
    Config::SetDefault("ns3::ThreeGppPropagationLossModel::Frequency",DoubleValue(28e9));
    Config::SetDefault("ns3::ThreeGppPropagationLossModel::ShadowingEnabled",BooleanValue(false));
    
    // Carrier bandwidth in Hz
    GlobalValue::GetValueByName ("Bandwidth", doubleValue);
    double bandwidth = doubleValue.Get();
    // Center frequency in Hz
    GlobalValue::GetValueByName ("CenterFrequency", doubleValue);
    double centerFrequency = doubleValue.Get();
    // Distance between cells
    GlobalValue::GetValueByName ("IntersideDistanceUEs", doubleValue);
    double isd_ue = doubleValue.Get();
    GlobalValue::GetValueByName ("IntersideDistanceCells", doubleValue);
    double isd_cell = doubleValue.Get();

    // Number of antennas
    int numAntennasMcUe = 1;
    int numAntennasMmWave = 1;

    NS_LOG_INFO("Bandwidth " << bandwidth << " centerFrequency " << double(centerFrequency)
                             << " isd_cell " << isd_cell << " numAntennasMcUe " << numAntennasMcUe
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

    GlobalValue::GetValueByName ("N_MmWaveEnbNodes", uintegerValue);
    uint8_t nMmWaveEnbNodes = uintegerValue.Get();
    uint8_t nLteEnbNodes = 1;
    GlobalValue::GetValueByName ("N_Ues", uintegerValue);
    uint32_t ues = uintegerValue.Get();
    uint8_t nUeNodes = ues;
    
    NS_LOG_UNCOND("Topology: " << unsigned(nLteEnbNodes) << " LTE eNB + " 
                              << unsigned(nMmWaveEnbNodes) << " mmWave gNBs + " 
                              << unsigned(nUeNodes) << " UEs");

    // Get SGW/PGW and create a single RemoteHost
    Ptr <Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr <Node> remoteHost = remoteHostContainer.Get(0);
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    // Create the Internet by connecting remoteHost to pgw
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

    // HANDOVER DEMO: Fixed cell positions in triangular layout
    Vector centerPosition = Vector(maxXAxis / 2, maxYAxis / 2, 10);  // Center at (600, 600, 10)

    Ptr <ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();

    // LTE eNB at center (anchor cell)
    enbPositionAlloc->Add(centerPosition);
    
    NS_LOG_UNCOND("=== CELL POSITIONS ===");
    NS_LOG_UNCOND("LTE eNB (Cell 1): " << centerPosition);

    // mmWave gNBs in triangular pattern around center
    // gNB1 (Cell 2): East of center
    Vector gnb1Pos = Vector(centerPosition.x + isd_cell, centerPosition.y, 10);
    enbPositionAlloc->Add(gnb1Pos);
    NS_LOG_UNCOND("mmWave gNB1 (Cell 2): " << gnb1Pos);

    // gNB2 (Cell 3): Northwest of center
    double angle1 = 2.0 * M_PI / 3.0;  // 120 degrees
    Vector gnb2Pos = Vector(centerPosition.x + isd_cell * cos(angle1), 
                           centerPosition.y + isd_cell * sin(angle1), 10);
    enbPositionAlloc->Add(gnb2Pos);
    NS_LOG_UNCOND("mmWave gNB2 (Cell 3): " << gnb2Pos);

    // gNB3 (Cell 4): Southwest of center
    double angle2 = 4.0 * M_PI / 3.0;  // 240 degrees
    Vector gnb3Pos = Vector(centerPosition.x + isd_cell * cos(angle2), 
                           centerPosition.y + isd_cell * sin(angle2), 10);
    enbPositionAlloc->Add(gnb3Pos);
    NS_LOG_UNCOND("mmWave gNB3 (Cell 4): " << gnb3Pos);

    MobilityHelper enbmobility;
    enbmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbmobility.SetPositionAllocator(enbPositionAlloc);
    enbmobility.Install(allEnbNodes);

    // HANDOVER DEMO: Fixed UE starting positions with directional mobility
    NS_LOG_UNCOND("=== UE INITIAL POSITIONS & MOBILITY ===");
    
    MobilityHelper uemobility;
    uemobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    uemobility.Install(ueNodes);

    // UE positions and velocities designed to trigger handovers
    // UE 0-1: Start near gNB1, move toward gNB2
    Ptr<ConstantVelocityMobilityModel> ueMob0 = ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    ueMob0->SetPosition(Vector(gnb1Pos.x - 50, gnb1Pos.y + 30, 1.5));
    ueMob0->SetVelocity(Vector(-400, 20, 0));  // Moving northwest at ~9.4 m/s
    NS_LOG_UNCOND("UE 1 (IMSI 1): Start=" << ueMob0->GetPosition() 
                  << " Velocity=" << ueMob0->GetVelocity() << " -> Expected HO: Cell2→Cell3");

    Ptr<ConstantVelocityMobilityModel> ueMob1 = ueNodes.Get(1)->GetObject<ConstantVelocityMobilityModel>();
    ueMob1->SetPosition(Vector(gnb1Pos.x - 30, gnb1Pos.y - 40, 1.5));
    ueMob1->SetVelocity(Vector(-200, 50, 0));  // Moving northwest
    NS_LOG_UNCOND("UE 2 (IMSI 2): Start=" << ueMob1->GetPosition() 
                  << " Velocity=" << ueMob1->GetVelocity() << " -> Expected HO: Cell2→Cell3");

    // UE 2-3: Start near gNB2, move toward gNB3
    Ptr<ConstantVelocityMobilityModel> ueMob2 = ueNodes.Get(2)->GetObject<ConstantVelocityMobilityModel>();
    ueMob2->SetPosition(Vector(gnb2Pos.x + 40, gnb2Pos.y - 50, 1.5));
    ueMob2->SetVelocity(Vector(-400, -80, 0));  // Moving southwest at ~8.9 m/s
    NS_LOG_UNCOND("UE 3 (IMSI 3): Start=" << ueMob2->GetPosition() 
                  << " Velocity=" << ueMob2->GetVelocity() << " -> Expected HO: Cell3→Cell4");

    Ptr<ConstantVelocityMobilityModel> ueMob3 = ueNodes.Get(3)->GetObject<ConstantVelocityMobilityModel>();
    ueMob3->SetPosition(Vector(gnb2Pos.x + 20, gnb2Pos.y - 30, 1.5));
    ueMob3->SetVelocity(Vector(-500, -70, 0));  // Moving southwest
    NS_LOG_UNCOND("UE 4 (IMSI 4): Start=" << ueMob3->GetPosition() 
                  << " Velocity=" << ueMob3->GetVelocity() << " -> Expected HO: Cell3→Cell4");

    // UE 4-5: Start near gNB3, move toward gNB1
    Ptr<ConstantVelocityMobilityModel> ueMob4 = ueNodes.Get(4)->GetObject<ConstantVelocityMobilityModel>();
    ueMob4->SetPosition(Vector(gnb3Pos.x - 30, gnb3Pos.y + 50, 1.5));
    ueMob4->SetVelocity(Vector(90, 10, 0));  // Moving east at 9 m/s
    NS_LOG_UNCOND("UE 5 (IMSI 5): Start=" << ueMob4->GetPosition() 
                  << " Velocity=" << ueMob4->GetVelocity() << " -> Expected HO: Cell4→Cell2");

    Ptr<ConstantVelocityMobilityModel> ueMob5 = ueNodes.Get(5)->GetObject<ConstantVelocityMobilityModel>();
    ueMob5->SetPosition(Vector(gnb3Pos.x + 40, gnb3Pos.y + 30, 1.5));
    ueMob5->SetVelocity(Vector(80, 20, 0));  // Moving east-northeast
    NS_LOG_UNCOND("UE 6 (IMSI 6): Start=" << ueMob5->GetPosition() 
                  << " Velocity=" << ueMob5->GetVelocity() << " -> Expected HO: Cell4→Cell2");

    // Install mmWave, lte, mc Devices to the nodes
    NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice(lteEnbNodes);
    NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice(mmWaveEnbNodes);
    NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice(ueNodes);

    // Install the IP stack on the UEs
    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIface;
    ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(mcUeDevs));
    
    // Set the default gateway for the UEs
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
        Ptr <Node> ueNode = ueNodes.Get(u);
        Ptr <Ipv4StaticRouting> ueStaticRouting =
                ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    // Add X2 interfaces
    mmwaveHelper->AddX2Interface(lteEnbNodes, mmWaveEnbNodes);

    // Manual attachment to closest cell
    mmwaveHelper->AttachToClosestEnb(mcUeDevs, mmWaveEnbDevs, lteEnbDevs);

    // HANDOVER DEMO: Connect handover traces
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                    MakeCallback(&NotifyHandoverStartUe));
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                    MakeCallback(&NotifyHandoverEndOkUe));

    // Energy monitoring (kept from original)
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
        // Downlink traffic to UEs
        PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",
                                            InetSocketAddress(Ipv4Address::GetAny(), 1234));
        sinkApp.Add(dlPacketSinkHelper.Install(ueNodes.Get(u)));
        UdpClientHelper dlClient(ueIpIface.GetAddress(u), 1234);
        dlClient.SetAttribute("Interval", TimeValue(MicroSeconds(500)));
        dlClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
        dlClient.SetAttribute("PacketSize", UintegerValue(200));
        clientApp.Add(dlClient.Install(remoteHost));
    }

    // Start applications
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

    // Enable LTE PHY/MAC traces
    Ptr <LteHelper> lteHelper = CreateObject<LteHelper>();
    lteHelper->Initialize();
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();

    // HANDOVER DEMO: Enable NetAnim output for visualization
    AnimationInterface anim("scenario0_handover.xml");
    anim.SetMaxPktsPerTraceFile(500000);
    
    // Set node descriptions for NetAnim
    anim.UpdateNodeDescription(lteEnbNodes.Get(0), "LTE_eNB");
    for (uint32_t i = 0; i < mmWaveEnbNodes.GetN(); ++i) {
        std::ostringstream desc;
        desc << "gNB" << (i+1);
        anim.UpdateNodeDescription(mmWaveEnbNodes.Get(i), desc.str());
    }
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        std::ostringstream desc;
        desc << "UE" << (i+1);
        anim.UpdateNodeDescription(ueNodes.Get(i), desc.str());
    }
    
    // Set node colors
    anim.UpdateNodeColor(lteEnbNodes.Get(0), 0, 255, 0);  // Green for LTE
    for (uint32_t i = 0; i < mmWaveEnbNodes.GetN(); ++i) {
        anim.UpdateNodeColor(mmWaveEnbNodes.Get(i), 255, 0, 0);  // Red for mmWave
    }
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        anim.UpdateNodeColor(ueNodes.Get(i), 0, 0, 255);  // Blue for UEs
    }

    NS_LOG_UNCOND("=== STARTING SIMULATION ===");
    NS_LOG_UNCOND("Simulation time: " << simTime << " seconds");
    NS_LOG_UNCOND("Expected handovers: UEs moving across cell boundaries");
    NS_LOG_UNCOND("Watch for HO_START and HO_COMPLETE messages");
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    NS_LOG_UNCOND("=== SIMULATION COMPLETE ===");
    NS_LOG_UNCOND("Check scenario0_handover.xml with NetAnim");
    NS_LOG_UNCOND("Check ue_position.txt, gnbs.txt for trajectory data");
    
    Simulator::Destroy();
    return 0;
}
