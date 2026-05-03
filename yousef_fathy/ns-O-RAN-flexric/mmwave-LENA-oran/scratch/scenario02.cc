/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/* Handover Demonstration Scenario - Modified from Scenario 0
 * 1 LTE eNB (anchor) + 3 mmWave gNBs + 6 UEs with linear mobility
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
#include "ns3/netanim-module.h"
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

// Global variables (unchanged from original)
std::map<uint64_t, uint16_t> imsi_cellid;
std::map<uint16_t, std::set<uint64_t>> imsi_list;
std::map<uint16_t, Ptr<Node>> cellid_node;
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

NS_LOG_COMPONENT_DEFINE ("ScenarioZeroHandover");

// ADDED: Handover notification callbacks
void NotifyConnectionEstablishedUe(uint64_t imsi, uint16_t cellid, uint16_t rnti) {
  NS_LOG_UNCOND("✓ UE IMSI " << imsi << " CONNECTED to Cell " << cellid 
                 << " at t=" << Simulator::Now().GetSeconds() << "s");
}

void NotifyHandoverStartUe(uint64_t imsi, uint16_t cellid, uint16_t rnti, uint16_t targetCellId) {
  NS_LOG_UNCOND(">>> HANDOVER START: UE " << imsi 
                 << " from Cell " << cellid << " → Cell " << targetCellId 
                 << " at t=" << Simulator::Now().GetSeconds() << "s");
}

void NotifyHandoverEndOkUe(uint64_t imsi, uint16_t cellid, uint16_t rnti) {
  NS_LOG_UNCOND("<<< HANDOVER COMPLETE: UE " << imsi 
                 << " now at Cell " << cellid 
                 << " at t=" << Simulator::Now().GetSeconds() << "s");
}

// Original helper functions (unchanged)
void PrintGnuplottableUeListToFile(std::string filename) {
    std::ofstream outFile;
    outFile.open(filename.c_str(), std::ios_base::out | std::ios_base::trunc);
    if (!outFile.is_open()) {
        NS_LOG_ERROR("Can't open file " << filename);
        return;
    }
    for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
        Ptr<Node> node = *it;
        int nDevs = node->GetNDevices();
        for (int j = 0; j < nDevs; j++) {
            Ptr<McUeNetDevice> mcuedev = node->GetDevice(j)->GetObject<McUeNetDevice>();
            if (mcuedev) {
                Vector pos = node->GetObject<MobilityModel>()->GetPosition();
                outFile << "set label \"" << mcuedev->GetImsi() << "\" at " << pos.x << "," << pos.y
                        << " left font \"Helvetica,8\" textcolor rgb \"black\" front point pt 1 ps "
                           "0.3 lc rgb \"black\" offset 0,0" << std::endl;
            }
        }
    }
}

void PrintGnuplottableEnbListToFile(uint64_t m_startTime) {
  uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();
  std::string filename1 = "enbs.txt";
  std::string filename2 = "gnbs.txt";
  
  curr_total_energy_consumption = 0;
  for (NodeList::Iterator it = NodeList::Begin(); it != NodeList::End(); ++it) {
      Ptr<Node> node = *it;
      int nDevs = node->GetNDevices();
      for (int j = 0; j < nDevs; j++) {
          Ptr<LteEnbNetDevice> enbdev = node->GetDevice(j)->GetObject<LteEnbNetDevice>();
          Ptr<MmWaveEnbNetDevice> mmdev = node->GetDevice(j)->GetObject<MmWaveEnbNetDevice>();
          if (enbdev) {
              Vector pos = node->GetObject<MobilityModel>()->GetPosition();
              std::ofstream outFile1;
              outFile1.open(filename1.c_str(), std::ios_base::out | std::ios_base::app);
              if (outFile1.is_open()) {
                outFile1 << timestamp << "," << enbdev->GetCellId() << "," << pos.x << "," << pos.y << ","
                         << m_startTime << "," << "0" << "," << "30" << std::endl;
                outFile1.close();
              }
            } else if (mmdev) {
                Vector pos = node->GetObject<MobilityModel>()->GetPosition();
                std::ofstream outFile2;
                outFile2.open(filename2.c_str(), std::ios_base::out | std::ios_base::app);
                if (outFile2.is_open()) {
                  auto ueMap = mmdev->GetUeMap();
                  for (const auto &ue: ueMap) {
                      uint64_t imsi_assoc = ue.second->GetImsi();
                      ue_assoc_list[imsi_assoc - 1] = mmdev->GetCellId();
                  }
                  uint16_t cell_id = mmdev->GetCellId();
                  Ptr<MmWaveEnbPhy> enbPhy = node->GetDevice(j)->GetObject<MmWaveEnbNetDevice>()->GetPhy();
                  double es_power = enbPhy->GetTxPower();
                  esON_list[cell_id] = (es_power == 0);
                  curr_total_energy_consumption += current_energy_consumption[cell_id];
                  outFile2 << timestamp << "," << cell_id << "," << pos.x << "," << pos.y << ","
                           << m_startTime << "," << esON_list[cell_id] << ","
                           << current_energy_consumption[cell_id] << "," << max_energy_consumption
                           << "," << sum_curr_total_energy_consumption << std::endl;
                  outFile2.close();
                }
            }
        }
    }
}

void ClearFile(std::string Filename, uint64_t m_startTime) {
    std::ofstream outFile;
    outFile.open(Filename.c_str(), std::ios_base::out | std::ios_base::trunc);
    if (!outFile.is_open()) {
        NS_LOG_ERROR("Can't open file " << Filename);
        return;
    }
    outFile.close();
    uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();
    std::ofstream outFile1;
    outFile1.open(Filename.c_str(), std::ios_base::out | std::ios_base::app);

  if (Filename == "ue_position.txt") {
      outFile1 << "timestamp,id,x,y,type,cell,simid" << std::endl;
    }
  else {
      outFile1 << "timestamp,id,x,y,simid,ESstate,currEC,maxEC,totalcurrEC" << std::endl;
      outFile1 << timestamp << "," << "0" << "," << maxXAxis << "," << maxYAxis << std::endl;
    }
    outFile1.close();
}

void PrintPosition(Ptr<Node> node, int iterator, std::string Filename, uint64_t m_startTime) {
  uint64_t timestamp = m_startTime + (uint64_t) Simulator::Now().GetMilliSeconds();
  int imsi;
  Ptr<Node> node1 = NodeList::GetNode(iterator);
  int nDevs = node->GetNDevices();
  std::ofstream outFile;
  for (int j = 0; j < nDevs; j++) {
      Ptr<McUeNetDevice> mcuedev = node1->GetDevice(j)->GetObject<McUeNetDevice>();
      if (mcuedev) {
          imsi = int(mcuedev->GetImsi());
          int serving_cell = ue_assoc_list[imsi - 1];
          if (serving_cell==0) serving_cell=1;
          Ptr<MobilityModel> model = node->GetObject<MobilityModel>();
          Vector position = model->GetPosition();
          outFile.open(Filename.c_str(), std::ios_base::out | std::ios_base::app);
          if (outFile.is_open()) {
              outFile << timestamp << "," << imsi << "," << position.x << "," << position.y << ",mc,"
                      << serving_cell << "," << m_startTime << std::endl;
              outFile.close();
            }
        }
    }
}

void EnergyConsumptionUpdate(int nodeIndex, std::string filename, double totaloldEnergyConsumption,
                         double totalnewEnergyConsumption) {
  Time currentTime = Simulator::Now();
  std::ofstream outFile;
  outFile.open(filename, std::ios_base::out | std::ios_base::app);
  outFile << currentTime.GetSeconds() << "," << totalnewEnergyConsumption << ","
          << (totalnewEnergyConsumption - totaloldEnergyConsumption) << std::endl;
  totalnewEnergyConsumption_storage[nodeIndex] = totalnewEnergyConsumption;
}

void EnergyConsumptionPrint(int nodeIndex) {
  current_energy_consumption[nodeIndex] =
      totalnewEnergyConsumption_storage[nodeIndex] - totaloldEnergyConsumption_storage[nodeIndex];
  totaloldEnergyConsumption_storage[nodeIndex] = totalnewEnergyConsumption_storage[nodeIndex];
}

// GlobalValue declarations (unchanged except where noted)
static ns3::GlobalValue g_bufferSize("bufferSize", "RLC tx buffer size (MB)",
                                     ns3::UintegerValue(10), ns3::MakeUintegerChecker<uint32_t>());
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
static ns3::GlobalValue g_hoSinrDifference("hoSinrDifference",
                           "The value for which an handover between MmWave eNB is triggered",
                           ns3::DoubleValue(3), ns3::MakeDoubleChecker<double>());
static ns3::GlobalValue g_indicationPeriodicity("indicationPeriodicity",
                                "E2 Indication Periodicity reports (value in seconds)",
                                ns3::DoubleValue(0.1), ns3::MakeDoubleChecker<double>(0.01, 2.0));
static ns3::GlobalValue g_simTime("simTime", "Simulation time in seconds", ns3::DoubleValue(30),  // CHANGED: 30s
                                  ns3::MakeDoubleChecker<double>(0.1, 100000.0));
static ns3::GlobalValue g_outageThreshold("outageThreshold", "SNR threshold for outage events [dB]",
                                          ns3::DoubleValue(-5.0), ns3::MakeDoubleChecker<double>());
static ns3::GlobalValue g_numberOfRaPreambles("numberOfRaPreambles",
        "how many random access preambles are available for the contention based RACH process",
        ns3::UintegerValue(40), ns3::MakeUintegerChecker<uint8_t>());
static ns3::GlobalValue g_handoverMode("handoverMode",
                       "HO euristic to be used, can be only \"NoAuto\", \"FixedTtt\", \"DynamicTtt\", \"Threshold\"",
                       ns3::StringValue("DynamicTtt"), ns3::MakeStringChecker());
static ns3::GlobalValue g_e2TermIp("e2TermIp", "The IP address of the RIC E2 termination",
                                   ns3::StringValue("127.0.0.1"), ns3::MakeStringChecker());
static ns3::GlobalValue g_enableE2FileLogging("enableE2FileLogging",
                              "If true, generate offline file logging instead of connecting to RIC",
                              ns3::BooleanValue(false), ns3::MakeBooleanChecker());
static ns3::GlobalValue g_e2_func_id("KPM_E2functionID", "Function ID to subscribe",
                                     ns3::DoubleValue(2), ns3::MakeDoubleChecker<double>());
static ns3::GlobalValue g_rc_e2_func_id("RC_E2functionID", "Function ID to subscribe",
                                        ns3::DoubleValue(3), ns3::MakeDoubleChecker<double>());
static ns3::GlobalValue g_controlFileName("controlFileName", "The path to the control file (can be absolute)",
                                          ns3::StringValue(""), ns3::MakeStringChecker());

// MODIFIED: Handover scenario parameters
static ns3::GlobalValue mmWave_nodes("N_MmWaveEnbNodes", "Number of mmWaveNodes",
                                      ns3::UintegerValue(3), ns3::MakeUintegerChecker<uint8_t>());  // CHANGED: 3 gNBs
static ns3::GlobalValue ue_s("N_Ues", "Number of User Equipments",
                                      ns3::UintegerValue(6), ns3::MakeUintegerChecker<uint32_t>());  // CHANGED: 6 UEs
static ns3::GlobalValue center_freq("CenterFrequency", "Center Frequency Value",
                                      ns3::DoubleValue(28e9), ns3::MakeDoubleChecker<double>());  // CHANGED: 28 GHz
static ns3::GlobalValue bandwidth_value("Bandwidth", "Bandwidth Value",
                                      ns3::DoubleValue(100e6), ns3::MakeDoubleChecker<double>());  // CHANGED: 100 MHz
static ns3::GlobalValue interside_distance_value_ue("IntersideDistanceUEs", "Interside Distance Value",
                                      ns3::DoubleValue(1000), ns3::MakeDoubleChecker<double>());
static ns3::GlobalValue interside_distance_value_cell("IntersideDistanceCells", "Interside Distance Value",
                                                  ns3::DoubleValue(200), ns3::MakeDoubleChecker<double>());  // CHANGED: 200m

int main(int argc, char *argv[]) {
    // ADDED: Enable handover logging
    LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
    LogComponentEnable("EpcX2", LOG_LEVEL_INFO);

    // MODIFIED: Smaller scenario area for handover demonstration
    maxXAxis = 800;   // CHANGED: from 4000 to 800m
    maxYAxis = 400;   // CHANGED: from 4000 to 400m

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

    NS_LOG_UNCOND("=== HANDOVER SCENARIO CONFIGURATION ===");
    NS_LOG_UNCOND("Handover Mode: " << handoverMode);
    NS_LOG_UNCOND("HO SINR Difference: " << hoSinrDifference << " dB");
    NS_LOG_UNCOND("Outage Threshold: " << outageThreshold << " dB");
    NS_LOG_UNCOND("Scenario Area: " << maxXAxis << "m x " << maxYAxis << "m");

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

    // Config::SetDefault calls (unchanged)
    Config::SetDefault("ns3::LteEnbNetDevice::ControlFileName", StringValue(controlFilename));
    Config::SetDefault("ns3::LteEnbNetDevice::E2Periodicity", DoubleValue(indicationPeriodicity));
    Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity", DoubleValue(indicationPeriodicity));
    Config::SetDefault("ns3::MmWaveHelper::E2ModeLte", BooleanValue(e2lteEnabled));
    Config::SetDefault("ns3::MmWaveHelper::E2ModeNr", BooleanValue(e2nrEnabled));
    Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableDuReport", BooleanValue(e2du));
    Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuUpReport", BooleanValue(e2cuUp));
    Config::SetDefault("ns3::LteEnbNetDevice::EnableCuUpReport", BooleanValue(e2cuUp));
    Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableCuCpReport", BooleanValue(e2cuCp));
    Config::SetDefault("ns3::LteEnbNetDevice::EnableCuCpReport", BooleanValue(e2cuCp));
    Config::SetDefault("ns3::MmWaveEnbNetDevice::ReducedPmValues", BooleanValue(reducedPmValues));
    Config::SetDefault("ns3::LteEnbNetDevice::ReducedPmValues", BooleanValue(reducedPmValues));
    Config::SetDefault("ns3::LteEnbNetDevice::EnableE2FileLogging", BooleanValue(enableE2FileLogging));
    Config::SetDefault("ns3::MmWaveEnbNetDevice::EnableE2FileLogging", BooleanValue(enableE2FileLogging));
    Config::SetDefault("ns3::LteEnbNetDevice::KPM_E2functionID", DoubleValue(g_e2_func_id));
    Config::SetDefault("ns3::MmWaveEnbNetDevice::KPM_E2functionID", DoubleValue(g_e2_func_id));
    Config::SetDefault("ns3::LteEnbNetDevice::RC_E2functionID", DoubleValue(g_rc_e2_func_id));
    Config::SetDefault("ns3::MmWaveEnbMac::NumberOfRaPreambles", UintegerValue(numberOfRaPreambles));
    Config::SetDefault("ns3::MmWaveHelper::HarqEnabled", BooleanValue(harqEnabled));
    Config::SetDefault("ns3::MmWaveHelper::UseIdealRrc", BooleanValue(true));
    Config::SetDefault("ns3::MmWaveHelper::E2TermIp", StringValue(e2TermIp));
    Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled", BooleanValue(harqEnabled));
    Config::SetDefault("ns3::MmWavePhyMacCommon::NumHarqProcess", UintegerValue(100));
    Config::SetDefault("ns3::PhasedArrayModel::AntennaElement",
      PointerValue(CreateObject<IsotropicAntennaModel>()));
    Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(100.0)));
    Config::SetDefault("ns3::ThreeGppChannelConditionModel::UpdatePeriod", TimeValue(MilliSeconds(100)));
    Config::SetDefault("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue(MilliSeconds(10.0)));
    Config::SetDefault("ns3::LteRlcUmLowLat::ReportBufferStatusTimer", TimeValue(MilliSeconds(10.0)));
    Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));
    Config::SetDefault("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));
    Config::SetDefault("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));
    Config::SetDefault("ns3::LteEnbRrc::OutageThreshold", DoubleValue(outageThreshold));
    Config::SetDefault("ns3::LteEnbRrc::SecondaryCellHandoverMode", StringValue(handoverMode));
    Config::SetDefault("ns3::LteEnbRrc::HoSinrDifference", DoubleValue(hoSinrDifference));
    Config::SetDefault("ns3::ThreeGppPropagationLossModel::Frequency", DoubleValue(28e9));  // CHANGED: 28 GHz
    Config::SetDefault("ns3::ThreeGppPropagationLossModel::ShadowingEnabled", BooleanValue(false));

    GlobalValue::GetValueByName("Bandwidth", doubleValue);
    double bandwidth = doubleValue.Get();
    GlobalValue::GetValueByName("CenterFrequency", doubleValue);
    double centerFrequency = doubleValue.Get();
    GlobalValue::GetValueByName("IntersideDistanceCells", doubleValue);
    double isd_cell = doubleValue.Get();

    int numAntennasMcUe = 1;
    int numAntennasMmWave = 1;

    NS_LOG_UNCOND("Bandwidth: " << bandwidth/1e6 << " MHz");
    NS_LOG_UNCOND("Center Frequency: " << centerFrequency/1e9 << " GHz");
    NS_LOG_UNCOND("Cell ISD: " << isd_cell << " m");

    Config::SetDefault("ns3::McUeNetDevice::AntennaNum", UintegerValue(numAntennasMcUe));
    Config::SetDefault("ns3::MmWaveNetDevice::AntennaNum", UintegerValue(numAntennasMmWave));
    Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(bandwidth));
    Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(centerFrequency));

    Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
    mmwaveHelper->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
    mmwaveHelper->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");

    Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
    mmwaveHelper->SetEpcHelper(epcHelper);

    GlobalValue::GetValueByName("N_MmWaveEnbNodes", uintegerValue);
    uint8_t nMmWaveEnbNodes = uintegerValue.Get();
    uint8_t nLteEnbNodes = 1;
    GlobalValue::GetValueByName("N_Ues", uintegerValue);
    uint32_t nUeNodes = uintegerValue.Get();

    NS_LOG_UNCOND("Deploying " << unsigned(nLteEnbNodes) << " LTE eNB + " 
                  << unsigned(nMmWaveEnbNodes) << " mmWave gNBs + " 
                  << nUeNodes << " UEs");

    // Network setup (unchanged)
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
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
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    // Create nodes
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

    // MODIFIED: Base station positions - linear deployment for handover
    Vector centerPosition = Vector(maxXAxis / 2, maxYAxis / 2, 3);
    Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
    
    // LTE eNB at center (anchor)
    enbPositionAlloc->Add(centerPosition);
    
    // MODIFIED: 3 mmWave gNBs in a line with spacing for overlapping coverage
    // gNB positions create overlapping coverage zones for handover
    enbPositionAlloc->Add(Vector(centerPosition.x - isd_cell, centerPosition.y, 3));  // Left gNB
    enbPositionAlloc->Add(Vector(centerPosition.x, centerPosition.y, 3));              // Center gNB (co-located with LTE)
    enbPositionAlloc->Add(Vector(centerPosition.x + isd_cell, centerPosition.y, 3));  // Right gNB

    NS_LOG_UNCOND("=== BASE STATION POSITIONS ===");
    NS_LOG_UNCOND("LTE eNB (Cell 1): (" << centerPosition.x << ", " << centerPosition.y << ")");
    NS_LOG_UNCOND("mmWave gNB (Cell 2): (" << centerPosition.x - isd_cell << ", " << centerPosition.y << ")");
    NS_LOG_UNCOND("mmWave gNB (Cell 3): (" << centerPosition.x << ", " << centerPosition.y << ")");
    NS_LOG_UNCOND("mmWave gNB (Cell 4): (" << centerPosition.x + isd_cell << ", " << centerPosition.y << ")");

    MobilityHelper enbmobility;
    enbmobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbmobility.SetPositionAllocator(enbPositionAlloc);
    enbmobility.Install(allEnbNodes);

    // MODIFIED: UE mobility - linear trajectories to trigger handovers
    MobilityHelper uemobility;
    Ptr<ListPositionAllocator> uePositionAlloc = CreateObject<ListPositionAllocator>();
    
    // UE starting positions and velocities for handover scenario
    // UEs 1-3: Move left to right (will trigger handovers: Cell 2 -> Cell 3 -> Cell 4)
    uePositionAlloc->Add(Vector(100, centerPosition.y - 30, 1.5));   // UE 1
    uePositionAlloc->Add(Vector(120, centerPosition.y, 1.5));        // UE 2
    uePositionAlloc->Add(Vector(140, centerPosition.y + 30, 1.5));   // UE 3
    
    // UEs 4-6: Move right to left (will trigger handovers: Cell 4 -> Cell 3 -> Cell 2)
    uePositionAlloc->Add(Vector(700, centerPosition.y - 30, 1.5));   // UE 4
    uePositionAlloc->Add(Vector(680, centerPosition.y, 1.5));        // UE 5
    uePositionAlloc->Add(Vector(660, centerPosition.y + 30, 1.5));   // UE 6

    uemobility.SetPositionAllocator(uePositionAlloc);
    uemobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    uemobility.Install(ueNodes);

    // Set UE velocities
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        Ptr<ConstantVelocityMobilityModel> cvmm = ueNodes.Get(i)->GetObject<ConstantVelocityMobilityModel>();
        if (i < 3) {
            // UEs 1-3 move left to right at 20 m/s
            cvmm->SetVelocity(Vector(20, 0, 0));
            NS_LOG_UNCOND("UE " << i+1 << " starts at (" << ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition().x 
                          << ", " << ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition().y 
                          << ") moving RIGHT at 20 m/s");
        } else {
            // UEs 4-6 move right to left at 20 m/s
            cvmm->SetVelocity(Vector(-20, 0, 0));
            NS_LOG_UNCOND("UE " << i+1 << " starts at (" << ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition().x 
                          << ", " << ueNodes.Get(i)->GetObject<MobilityModel>()->GetPosition().y 
                          << ") moving LEFT at 20 m/s");
        }
    }

    // Install devices
    NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice(lteEnbNodes);
    NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice(mmWaveEnbNodes);
    NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice(ueNodes);

    // Install IP stack on UEs
    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIface;
    ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(mcUeDevs));
    
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
        Ptr<Node> ueNode = ueNodes.Get(u);
        Ptr<Ipv4StaticRouting> ueStaticRouting =
                ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }

    // Add X2 interfaces
    mmwaveHelper->AddX2Interface(lteEnbNodes, mmWaveEnbNodes);

    // Manual attachment
    mmwaveHelper->AttachToClosestEnb(mcUeDevs, mmWaveEnbDevs, lteEnbDevs);

    // ADDED: Connect handover trace sources
    for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
        Ptr<McUeNetDevice> mcUeDev = mcUeDevs.Get(u)->GetObject<McUeNetDevice>();
        if (mcUeDev) {
            Ptr<LteUeRrc> ueRrc = mcUeDev->GetLteRrc();
            if (ueRrc) {
                ueRrc->TraceConnectWithoutContext("ConnectionEstablished", 
                    MakeCallback(&NotifyConnectionEstablishedUe));
                ueRrc->TraceConnectWithoutContext("HandoverStart", 
                    MakeCallback(&NotifyHandoverStartUe));
                ueRrc->TraceConnectWithoutContext("HandoverEndOk", 
                    MakeCallback(&NotifyHandoverEndOkUe));
            }
        }
    }

    // Energy model setup (unchanged)
    BasicEnergySourceHelper basicEnergySourceHelper;
    basicEnergySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(1000000000000));
    basicEnergySourceHelper.Set("BasicEnergySupplyVoltageV", DoubleValue(5.0));
    energy::EnergySourceContainer sources = basicEnergySourceHelper.Install(mmWaveEnbNodes);
    MmWaveRadioEnergyModelEnbHelper nrEnbHelper;
    energy::DeviceEnergyModelContainer deviceEModel = nrEnbHelper.Install(mmWaveEnbDevs, sources);

    GlobalValue::GetValueByName("simTime", doubleValue);
    double simTime = doubleValue.Get();
    int numPrints = simTime / 0.1;

    std::vector<std::ofstream> outFiles;
    for (int x = 0; x < nMmWaveEnbNodes; ++x) {
        std::ostringstream energyFileName;
        energyFileName << "energyfilecell" << x + 2 << ".csv";
        std::ofstream outFile;
        outFile.open(energyFileName.str(), std::ios_base::out | std::ios_base::trunc);
        outFile << "Time,NetEnergy,DiffEnergy" << std::endl;
        outFiles.push_back(std::move(outFile));
    }

    for (int x = 0; x < nMmWaveEnbNodes; ++x) {
        std::ostringstream filename;
        filename << "energyfilecell" << x + 2 << ".csv";
        deviceEModel.Get(x)->TraceConnectWithoutContext(
            "TotalEnergyConsumption",
            MakeBoundCallback(&EnergyConsumptionUpdate, x, filename.str()));
        for (int i = 0; i < numPrints; i++) {
            Simulator::Schedule(Seconds(i * simTime / numPrints), &EnergyConsumptionPrint, x);
        }
    }

    // Install applications
    uint16_t portUdp = 60000;
    Address sinkLocalAddressUdp(InetSocketAddress(Ipv4Address::GetAny(), portUdp));
    PacketSinkHelper sinkHelperUdp("ns3::UdpSocketFactory", sinkLocalAddressUdp);
    AddressValue serverAddressUdp(InetSocketAddress(remoteHostAddr, portUdp));

    ApplicationContainer sinkApp;
    sinkApp.Add(sinkHelperUdp.Install(remoteHost));

    ApplicationContainer clientApp;

    for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
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

    // Enable LTE stack traces
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    lteHelper->Initialize();
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();

    // ADDED: Enable NetAnim output for visualization
    AnimationInterface anim("scenario0-handover.xml");
    anim.EnablePacketMetadata(true);
    
    // Set node descriptions for NetAnim
    anim.UpdateNodeDescription(lteEnbNodes.Get(0), "LTE-eNB");
    for (uint32_t i = 0; i < mmWaveEnbNodes.GetN(); ++i) {
        std::ostringstream desc;
        desc << "gNB-" << (i+2);
        anim.UpdateNodeDescription(mmWaveEnbNodes.Get(i), desc.str());
    }
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        std::ostringstream desc;
        desc << "UE-" << (i+1);
        anim.UpdateNodeDescription(ueNodes.Get(i), desc.str());
    }

    // Set colors for visualization
    anim.UpdateNodeColor(lteEnbNodes.Get(0), 255, 0, 0);  // Red for LTE
    for (uint32_t i = 0; i < mmWaveEnbNodes.GetN(); ++i) {
        anim.UpdateNodeColor(mmWaveEnbNodes.Get(i), 0, 0, 255);  // Blue for mmWave
    }
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        anim.UpdateNodeColor(ueNodes.Get(i), 0, 255, 0);  // Green for UEs
    }

    bool run = true;
    if (run) {
        NS_LOG_UNCOND("=== STARTING SIMULATION ===");
        NS_LOG_UNCOND("Simulation time: " << simTime << " seconds");
        NS_LOG_UNCOND("Expected handovers: UEs moving through overlapping coverage zones");
        NS_LOG_UNCOND("Watch for HANDOVER START/COMPLETE messages in the log");
        Simulator::Stop(Seconds(simTime));
        NS_LOG_INFO("Run Simulation.");
        Simulator::Run();
    }

    NS_LOG_UNCOND("=== SIMULATION COMPLETE ===");
    NS_LOG_UNCOND("Output files generated:");
    NS_LOG_UNCOND("  - scenario0-handover.xml (NetAnim visualization)");
    NS_LOG_UNCOND("  - ue_position.txt (UE trajectory data)");
    NS_LOG_UNCOND("  - gnbs.txt (gNB status data)");
    NS_LOG_UNCOND("  - enbs.txt (eNB status data)");
    
    Simulator::Destroy();
    NS_LOG_INFO("Done.");
    return 0;
}
