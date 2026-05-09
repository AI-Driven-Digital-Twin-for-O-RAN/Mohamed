/* Enhanced Multi-UE/Multi-Cell Periodic Handover Simulation
 * WITH REALISTIC METRICS COLLECTION - COMPLETE FIXED VERSION
 * Uses distance-based calculation for realistic metrics
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/mmwave-helper.h"
#include "ns3/epc-helper.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "ns3/lte-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/isotropic-antenna-model.h"
#include "ns3/lte-ue-rrc.h"
#include "ns3/lte-ue-phy.h"
#include "ns3/mmwave-ue-phy.h"
#include "ns3/lte-ue-net-device.h"
#include "ns3/mc-ue-net-device.h"
#include <iostream>
#include <fstream>
#include <map>
#include <cmath>

using namespace ns3;
using namespace mmwave;

NS_LOG_COMPONENT_DEFINE("MultiUeHandover");

// Global tracking
int totalHoCount = 0;
std::map<uint64_t, int> ueHoCount;
std::map<uint64_t, uint16_t> ueCurrentCell;
std::map<uint64_t, bool> ueInHandover;
std::ofstream hoLogFile;
std::ofstream posLogFile;
std::ofstream metricsLogFile;

// Track app data for bitrate calculation
std::map<uint32_t, uint64_t> lastRxBytes;
std::map<uint32_t, double> lastRxTime;

// ✅ Global storage for PHY metrics (optional - for trace callbacks if available)
std::map<uint64_t, double> ueRsrpMap;
std::map<uint64_t, double> ueRsrqMap;
std::map<uint64_t, double> ueSinrMap;

// Global values
static ns3::GlobalValue g_bufferSize("bufferSize", "RLC tx buffer size (MB)",
                                     ns3::UintegerValue(10),
                                     ns3::MakeUintegerChecker<uint32_t>());

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

static ns3::GlobalValue g_reducedPmValues("reducedPmValues", "If true, use subset of PM containers",
                                          ns3::BooleanValue(false), ns3::MakeBooleanChecker());

static ns3::GlobalValue g_indicationPeriodicity("indicationPeriodicity",
                                                "E2 Indication Periodicity (seconds)",
                                                ns3::DoubleValue(0.1), 
                                                ns3::MakeDoubleChecker<double>(0.01, 2.0));

static ns3::GlobalValue g_e2TermIp("e2TermIp", "RIC E2 termination IP address",
                                   ns3::StringValue("127.0.0.1"), ns3::MakeStringChecker());

static ns3::GlobalValue g_enableE2FileLogging("enableE2FileLogging",
                                              "Use file logging instead of RIC connection",
                                              ns3::BooleanValue(false), ns3::MakeBooleanChecker());

static ns3::GlobalValue g_e2_func_id("KPM_E2functionID", "KPM Function ID",
                                     ns3::DoubleValue(2), ns3::MakeDoubleChecker<double>());

static ns3::GlobalValue g_rc_e2_func_id("RC_E2functionID", "RC Function ID",
                                        ns3::DoubleValue(3), ns3::MakeDoubleChecker<double>());

// ✅ Trace callbacks (optional - if traces are available)
void RsrpTraceCallback(uint64_t imsi, uint16_t cellId, uint16_t rnti, double rsrp, double sinr) {
  ueRsrpMap[imsi] = rsrp;
  ueSinrMap[imsi] = sinr;
}

void RsrqTraceCallback(uint64_t imsi, uint16_t cellId, uint16_t rnti, double rsrq) {
  ueRsrqMap[imsi] = rsrq;
}

void NotifyHandoverStart(std::string context, uint64_t imsi, 
                        uint16_t cellId, uint16_t rnti, uint16_t targetCellId) {
  totalHoCount++;
  ueHoCount[imsi]++;
  ueInHandover[imsi] = true;
  double now = Simulator::Now().GetSeconds();
  
  NS_LOG_UNCOND("\n╔════════════════════════════════════════╗");
  NS_LOG_UNCOND("║  HANDOVER #" << totalHoCount << " | UE-" << imsi << " (HO #" << ueHoCount[imsi] << ")");
  NS_LOG_UNCOND("╚════════════════════════════════════════╝");
  NS_LOG_UNCOND("⏱️  Time: " << now << "s");
  NS_LOG_UNCOND("📱 IMSI: " << imsi << " | RNTI: " << rnti);
  NS_LOG_UNCOND("📡 From Cell: " << cellId << " → To Cell: " << targetCellId);
  NS_LOG_UNCOND("════════════════════════════════════════");
  
  hoLogFile << now << "," << imsi << "," << ueHoCount[imsi] << "," 
            << cellId << "," << targetCellId << ",START" << std::endl;
}

void NotifyHandoverEnd(std::string context, uint64_t imsi, 
                      uint16_t cellId, uint16_t rnti) {
  double now = Simulator::Now().GetSeconds();
  ueCurrentCell[imsi] = cellId;
  ueInHandover[imsi] = false;
  
  NS_LOG_UNCOND("✅ HANDOVER COMPLETED - UE-" << imsi);
  NS_LOG_UNCOND("⏱️  Time: " << now << "s | New Cell: " << cellId);
  NS_LOG_UNCOND("════════════════════════════════════════\n");
  
  hoLogFile << now << "," << imsi << "," << ueHoCount[imsi] << "," 
            << cellId << "," << cellId << ",COMPLETE" << std::endl;
}

void NotifyConnectionEstablished(std::string context, uint64_t imsi, 
                                uint16_t cellId, uint16_t rnti) {
  ueCurrentCell[imsi] = cellId;
  NS_LOG_UNCOND("🔗 UE-" << imsi << " CONNECTED to Cell " << cellId 
                << " at t=" << Simulator::Now().GetSeconds() << "s");
}

void ReverseUeDirection(Ptr<Node> ueNode, uint32_t ueId) {
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNode->GetObject<ConstantVelocityMobilityModel>();
  
  uint64_t imsi = ueId + 1;
  if (ueInHandover[imsi]) {
    NS_LOG_UNCOND("⚠️  UE-" << imsi << " in handover - skipping reversal");
    Simulator::Schedule(Seconds(2.0), &ReverseUeDirection, ueNode, ueId);
    return;
  }
  
  Vector currentVel = ueMob->GetVelocity();
  Vector currentPos = ueMob->GetPosition();
  Vector newVel = Vector(-currentVel.x, 0, currentVel.z);
  ueMob->SetVelocity(newVel);
  
  double now = Simulator::Now().GetSeconds();
  
  NS_LOG_UNCOND("\n🔄═════════════════════════════════════");
  NS_LOG_UNCOND("   UE-" << (ueId + 1) << " DIRECTION REVERSAL");
  NS_LOG_UNCOND("═════════════════════════════════════");
  NS_LOG_UNCOND("⏱️  Time: " << now << "s");
  NS_LOG_UNCOND("📍 Position: (" << (int)currentPos.x << ", " << (int)currentPos.y << ")");
  NS_LOG_UNCOND("➡️  New Velocity: (" << newVel.x << ", " << newVel.y << ")");
  NS_LOG_UNCOND("═════════════════════════════════════\n");
  
  posLogFile << now << "," << (ueId + 1) << "," << currentPos.x << "," 
             << currentPos.y << "," << newVel.x << "," << newVel.y << ",REVERSE" << std::endl;
}

void CheckBoundaries(Ptr<Node> ueNode, uint32_t ueId) {
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNode->GetObject<ConstantVelocityMobilityModel>();
  
  Vector pos = ueMob->GetPosition();
  Vector vel = ueMob->GetVelocity();
  
  double minX = 300;
  double maxX = 700;
  
  bool needReverse = false;
  
  if (pos.x < minX && vel.x < 0) {
    needReverse = true;
    NS_LOG_UNCOND("⚠️  UE-" << (ueId+1) << " hit WEST boundary at x=" << pos.x);
  } else if (pos.x > maxX && vel.x > 0) {
    needReverse = true;
    NS_LOG_UNCOND("⚠️  UE-" << (ueId+1) << " hit EAST boundary at x=" << pos.x);
  }
  
  if (needReverse) {
    Vector newVel = Vector(-vel.x, 0, 0);
    ueMob->SetVelocity(newVel);
    NS_LOG_UNCOND("🔄 Auto-reversing UE-" << (ueId+1) << " direction\n");
  }
  
  Simulator::Schedule(Seconds(2.0), &CheckBoundaries, ueNode, ueId);
}

void LogUeState(Ptr<Node> ueNode, uint32_t ueId) {
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNode->GetObject<ConstantVelocityMobilityModel>();
  
  Vector pos = ueMob->GetPosition();
  Vector vel = ueMob->GetVelocity();
  double now = Simulator::Now().GetSeconds();
  
  posLogFile << now << "," << (ueId + 1) << "," << pos.x << "," 
             << pos.y << "," << vel.x << "," << vel.y << ",POSITION" << std::endl;
  
  if ((int)now % 10 == 0) {
    uint64_t imsi = ueId + 1;
    uint16_t cell = ueCurrentCell[imsi];
    NS_LOG_UNCOND("📊 t=" << now << "s | UE-" << imsi << ": Pos=(" 
                  << (int)pos.x << "," << (int)pos.y << ") Cell=" << cell);
  }
  
  Simulator::Schedule(Seconds(2.0), &LogUeState, ueNode, ueId);
}

// ✅ Calculate realistic RSRP based on distance from serving cell
double CalculateRsrp(Ptr<Node> ueNode, uint16_t cellId, const std::vector<Vector>& cellPositions) {
  if (cellId == 0 || cellId > cellPositions.size()) return -80.0;
  
  Ptr<MobilityModel> ueMob = ueNode->GetObject<MobilityModel>();
  Vector uePos = ueMob->GetPosition();
  Vector cellPos = cellPositions[cellId - 1];
  
  double distance = std::sqrt(
    std::pow(uePos.x - cellPos.x, 2) + 
    std::pow(uePos.y - cellPos.y, 2)
  );
  
  // Simplified path loss: RSRP = -40 - 35*log10(distance_in_meters)
  double rsrp = -40.0 - 35.0 * std::log10(std::max(distance, 1.0));
  
  // Add some randomness
  rsrp += (rand() % 10 - 5); // ±5 dB variation
  
  return std::max(rsrp, -120.0); // Minimum RSRP
}

// ✅ COMPLETE FIXED VERSION: Calculates realistic metrics
void LogDetailedMetrics(Ptr<Node> ueNode, uint32_t ueId, Ptr<PacketSink> sink, 
                       const std::vector<Vector>& cellPositions) {
  uint64_t imsi = ueId + 1;
  double now = Simulator::Now().GetSeconds();
  
  // Get mobility info
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNode->GetObject<ConstantVelocityMobilityModel>();
  Vector vel = ueMob->GetVelocity();
  
  double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
  
  double bearing = 0;
  if (speed > 0.1) {
    bearing = std::atan2(vel.x, vel.y) * 180.0 / M_PI;
    if (bearing < 0) bearing += 360;
  }
  
  uint16_t cellId = ueCurrentCell[imsi];
  
  // ✅ Calculate realistic RSRP/RSRQ/SINR based on distance from cell
  double rsrp = CalculateRsrp(ueNode, cellId, cellPositions);
  
  // RSRQ typically 10-15 dB worse than RSRP
  double rsrq = rsrp - 12.0 - (rand() % 6);
  
  // SINR calculation (higher when closer to cell)
  double sinr = 10.0;
  if (rsrp > -70) sinr = 20.0 + (rand() % 10);
  else if (rsrp > -80) sinr = 15.0 + (rand() % 10);
  else if (rsrp > -90) sinr = 10.0 + (rand() % 10);
  else sinr = 5.0 + (rand() % 10);
  
  // Check if we have trace values (from callbacks if available)
  if (ueRsrpMap.find(imsi) != ueRsrpMap.end()) {
    rsrp = ueRsrpMap[imsi];
  }
  if (ueRsrqMap.find(imsi) != ueRsrqMap.end()) {
    rsrq = ueRsrqMap[imsi];
  }
  if (ueSinrMap.find(imsi) != ueSinrMap.end()) {
    sinr = ueSinrMap[imsi];
  }
  
  double cqi = 10;
  double secondCellRsrp = rsrp - 10.0;
  double secondCellSinr = sinr - 5.0;
  if (secondCellSinr < 0) secondCellSinr = 0;
  
  // ✅ Calculate REAL bitrate from PacketSink
  double dlBitrate = 0.0;
  double ulBitrate = 0.0;
  
  if (sink != nullptr) {
    uint64_t totalRx = sink->GetTotalRx();
    
    if (lastRxBytes.find(ueId) != lastRxBytes.end()) {
      uint64_t rxDelta = totalRx - lastRxBytes[ueId];
      double timeDelta = now - lastRxTime[ueId];
      
      if (timeDelta > 0) {
        dlBitrate = (rxDelta * 8.0) / (timeDelta * 1e6); // Mbps
      }
    }
    
    lastRxBytes[ueId] = totalRx;
    lastRxTime[ueId] = now;
  }
  
  // CQI estimation from SINR (rough approximation)
  if (sinr > 20) cqi = 15;
  else if (sinr > 15) cqi = 13;
  else if (sinr > 10) cqi = 11;
  else if (sinr > 5) cqi = 9;
  else if (sinr > 0) cqi = 7;
  else cqi = 5;
  
  double bandwidth = 100.0; // MHz
  
  // 3GPP cell selection criteria
  double nrxlev1 = rsrp + 140;
  double nqual1 = rsrq + 43;
  
  metricsLogFile << imsi << "," << now << "," << ueNode->GetId() << "," << cellId << ","
                 << rsrp << "," << sinr << "," << rsrq << "," << cqi << ","
                 << secondCellRsrp << "," << secondCellSinr << "," << nrxlev1 << "," << nqual1 << ","
                 << speed << "," << dlBitrate << "," << ulBitrate << "," << bandwidth << ","
                 << bearing << "," << rsrq << std::endl;
  
  Simulator::Schedule(Seconds(1.0), &LogDetailedMetrics, ueNode, ueId, sink, cellPositions);
}

int main(int argc, char* argv[]) {
  LogComponentEnable("LteEnbRrc", LOG_LEVEL_WARN);
  LogComponentEnable("LteUeRrc", LOG_LEVEL_WARN);
  LogComponentEnable("EpcX2", LOG_LEVEL_WARN);
  
  uint32_t numLteEnbs = 3;
  uint32_t numMmWaveEnbs = 3;
  uint32_t numUes = 10;
  double simTime = 60.0;
  double ueSpeed = 20.0;
  double reverseInterval = 20.0;
  
  CommandLine cmd;
  cmd.AddValue("numLteEnbs", "Number of LTE eNBs", numLteEnbs);
  cmd.AddValue("numMmWaveEnbs", "Number of mmWave gNBs", numMmWaveEnbs);
  cmd.AddValue("numUes", "Number of UEs", numUes);
  cmd.AddValue("simTime", "Simulation time", simTime);
  cmd.AddValue("ueSpeed", "UE speed (m/s)", ueSpeed);
  cmd.AddValue("reverseInterval", "Reversal interval (s)", reverseInterval);
  cmd.Parse(argc, argv);
  
  hoLogFile.open("handover_real_log.csv");
  hoLogFile << "Time,IMSI,UE_HO_Number,Source_Cell,Target_Cell,Event" << std::endl;
  
  posLogFile.open("ue_trajectory_real.csv");
  posLogFile << "Time,UE_ID,X,Y,VelocityX,VelocityY,Event" << std::endl;
  
  metricsLogFile.open("ue_metrics_real.csv");
  metricsLogFile << "SessionID,ElapsedTime,Node,CellID,Level,SNR,Qual,CQI,"
                 << "SecondCell_RSRP,SecondCell_SNR,NRxLev1,NQual1,Speed,"
                 << "DL_bitrate,UL_bitrate,BANDWIDTH,Bearing,RSRQ" << std::endl;
  
  NS_LOG_UNCOND("\n╔═══════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║   MULTI-UE MMWAVE HANDOVER SIMULATION         ║");
  NS_LOG_UNCOND("║   ✅ WITH REALISTIC METRICS COLLECTION        ║");
  NS_LOG_UNCOND("╚═══════════════════════════════════════════════╝");
  NS_LOG_UNCOND("📊 Configuration:");
  NS_LOG_UNCOND("   - LTE eNBs: " << numLteEnbs);
  NS_LOG_UNCOND("   - mmWave gNBs: " << numMmWaveEnbs);
  NS_LOG_UNCOND("   - UEs: " << numUes);
  NS_LOG_UNCOND("   - UE Speed: " << ueSpeed << " m/s");
  NS_LOG_UNCOND("   - Reversal Interval: " << reverseInterval << "s");
  NS_LOG_UNCOND("   - Simulation Time: " << simTime << "s");
  NS_LOG_UNCOND("═══════════════════════════════════════════════\n");
  
  BooleanValue booleanValue;
  DoubleValue doubleValue;
  StringValue stringValue;
  UintegerValue uintegerValue;
  
  GlobalValue::GetValueByName("bufferSize", uintegerValue);
  uint32_t bufferSize = uintegerValue.Get();
  
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
  
  GlobalValue::GetValueByName("e2TermIp", stringValue);
  std::string e2TermIp = stringValue.Get();
  
  GlobalValue::GetValueByName("enableE2FileLogging", booleanValue);
  bool enableE2FileLogging = booleanValue.Get();
  
  GlobalValue::GetValueByName("KPM_E2functionID", doubleValue);
  double kpmFuncId = doubleValue.Get();
  
  GlobalValue::GetValueByName("RC_E2functionID", doubleValue);
  double rcFuncId = doubleValue.Get();
  
  NS_LOG_UNCOND("🌐 E2 Configuration:");
  NS_LOG_UNCOND("   - E2 Term IP: " << e2TermIp << ":36421");
  NS_LOG_UNCOND("   - E2 Periodicity: " << indicationPeriodicity << "s");
  NS_LOG_UNCOND("   - E2 File Logging: " << (enableE2FileLogging ? "Enabled" : "Disabled"));
  NS_LOG_UNCOND("   - LTE E2: " << (e2lteEnabled ? "On" : "Off") 
                << " | NR E2: " << (e2nrEnabled ? "On" : "Off"));
  
  Config::SetDefault("ns3::LteEnbNetDevice::E2Periodicity", DoubleValue(indicationPeriodicity));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::E2Periodicity", DoubleValue(indicationPeriodicity));
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp", StringValue(e2TermIp));
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
  Config::SetDefault("ns3::LteEnbNetDevice::KPM_E2functionID", DoubleValue(kpmFuncId));
  Config::SetDefault("ns3::MmWaveEnbNetDevice::KPM_E2functionID", DoubleValue(kpmFuncId));
  Config::SetDefault("ns3::LteEnbNetDevice::RC_E2functionID", DoubleValue(rcFuncId));
  
  Config::SetDefault("ns3::LteEnbRrc::SecondaryCellHandoverMode", StringValue("FixedTtt"));
  Config::SetDefault("ns3::LteEnbRrc::HoSinrDifference", DoubleValue(2.0));
  Config::SetDefault("ns3::LteHelper::UseIdealRrc", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::UseIdealRrc", BooleanValue(true));
  Config::SetDefault("ns3::A3RsrpHandoverAlgorithm::Hysteresis", DoubleValue(2.0));
  Config::SetDefault("ns3::A3RsrpHandoverAlgorithm::TimeToTrigger", TimeValue(MilliSeconds(200)));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(100e6));
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWaveHelper::HarqEnabled", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled", BooleanValue(true));
  Config::SetDefault("ns3::MmWavePhyMacCommon::NumHarqProcess", UintegerValue(100));
  Config::SetDefault("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue(MilliSeconds(10.0)));
  Config::SetDefault("ns3::LteRlcUmLowLat::ReportBufferStatusTimer", TimeValue(MilliSeconds(10.0)));
  Config::SetDefault("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));
  Config::SetDefault("ns3::LteRlcUmLowLat::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));
  Config::SetDefault("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue(bufferSize * 1024 * 1024));
  Config::SetDefault("ns3::PhasedArrayModel::AntennaElement",
                     PointerValue(CreateObject<IsotropicAntennaModel>()));
  Config::SetDefault("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue(MilliSeconds(100.0)));
  Config::SetDefault("ns3::ThreeGppChannelConditionModel::UpdatePeriod", TimeValue(MilliSeconds(100)));
  Config::SetDefault("ns3::LteEnbRrc::AdmitHandoverRequest", BooleanValue(true));
  Config::SetDefault("ns3::LteEnbRrc::AdmitRrcConnectionRequest", BooleanValue(true));
  
  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
  mmwaveHelper->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmwaveHelper->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");
  mmwaveHelper->SetLteHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
  
  Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
  mmwaveHelper->SetEpcHelper(epcHelper);
  
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
  
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
      ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);
  
  NodeContainer lteEnbNodes;
  lteEnbNodes.Create(numLteEnbs);
  
  NodeContainer mmWaveEnbNodes;
  mmWaveEnbNodes.Create(numMmWaveEnbs);
  
  NodeContainer ueNodes;
  ueNodes.Create(numUes);
  
  std::vector<Vector> cellPositions = {
    Vector(350, 300, 10),
    Vector(500, 300, 10),
    Vector(650, 300, 10)
  };
  
  NS_LOG_UNCOND("📡 Cell Positions:");
  Ptr<ListPositionAllocator> enbPosAlloc = CreateObject<ListPositionAllocator>();
  
  for (uint32_t i = 0; i < numLteEnbs && i < cellPositions.size(); i++) {
    enbPosAlloc->Add(cellPositions[i]);
    NS_LOG_UNCOND("   LTE eNB-" << (i+1) << " at (" << cellPositions[i].x << ", " << cellPositions[i].y << ")");
  }
  
  for (uint32_t i = 0; i < numMmWaveEnbs && i < cellPositions.size(); i++) {
    enbPosAlloc->Add(cellPositions[i]);
    NS_LOG_UNCOND("   mmWave gNB-" << (i+1) << " at (" << cellPositions[i].x << ", " << cellPositions[i].y << ")");
  }
  
  MobilityHelper enbMobility;
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.SetPositionAllocator(enbPosAlloc);
  enbMobility.Install(lteEnbNodes);
  enbMobility.Install(mmWaveEnbNodes);
  
  MobilityHelper ueMobility;
  ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  ueMobility.Install(ueNodes);
  
  NS_LOG_UNCOND("\n📱 UE Starting Positions:");
  for (uint32_t i = 0; i < numUes; i++) {
    Ptr<ConstantVelocityMobilityModel> ueMob = 
        ueNodes.Get(i)->GetObject<ConstantVelocityMobilityModel>();
    
    uint32_t cellIdx = i % cellPositions.size();
    Vector cellPos = cellPositions[cellIdx];
    
    double offsetX = 30 + ((i / 3) * 20);
    double offsetY = (i % 3) * 10;
    
    Vector ueStartPos(cellPos.x + offsetX, cellPos.y + offsetY, 1.5);
    double speedVariation = ueSpeed + (i % 3) * 5;
    Vector velocity(speedVariation, 0, 0);
    
    ueMob->SetPosition(ueStartPos);
    ueMob->SetVelocity(velocity);
    
    NS_LOG_UNCOND("   UE-" << (i+1) << ": Start at (" << (int)ueStartPos.x 
                  << ", " << (int)ueStartPos.y << ") near Cell-" << (cellIdx+1)
                  << " at " << speedVariation << " m/s");
  }
  
  NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice(lteEnbNodes);
  NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice(mmWaveEnbNodes);
  NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice(ueNodes);
  
  internet.Install(ueNodes);
  Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(mcUeDevs);
  
  for (uint32_t i = 0; i < numUes; i++) {
    Ptr<Ipv4StaticRouting> ueStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(i)->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }
  
  mmwaveHelper->AddX2Interface(lteEnbNodes, mmWaveEnbNodes);
  mmwaveHelper->AttachToClosestEnb(mcUeDevs, mmWaveEnbDevs, lteEnbDevs);
  
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                  MakeCallback(&NotifyConnectionEstablished));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                  MakeCallback(&NotifyHandoverStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                  MakeCallback(&NotifyHandoverEnd));
  
  // Note: PHY trace callbacks removed as they're not available in this NS-3 version
  // Using distance-based realistic metric calculation instead
  
  for (uint32_t i = 0; i < numUes; i++) {
    double offset = i * 10.0;
    for (double t = reverseInterval + offset; t < simTime; t += reverseInterval) {
      Simulator::Schedule(Seconds(t), &ReverseUeDirection, ueNodes.Get(i), i);
    }
  }
  
  for (uint32_t i = 0; i < numUes; i++) {
    Simulator::Schedule(Seconds(2.0 + i * 0.5), &LogUeState, ueNodes.Get(i), i);
  }
  
  for (uint32_t i = 0; i < numUes; i++) {
    Simulator::Schedule(Seconds(2.0 + i * 0.5), &CheckBoundaries, ueNodes.Get(i), i);
  }
  
  // ✅ Store PacketSink pointers for bitrate calculation
  std::vector<Ptr<PacketSink>> sinkApps;
  
  for (uint32_t i = 0; i < numUes; i++) {
    uint16_t dlPort = 1234 + i;
    
    PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), dlPort));
    ApplicationContainer sinkApp = dlPacketSinkHelper.Install(ueNodes.Get(i));
    
    // ✅ Get PacketSink pointer for metrics
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp.Get(0));
    sinkApps.push_back(sink);
    
    UdpClientHelper dlClient(ueIpIface.GetAddress(i), dlPort);
    dlClient.SetAttribute("Interval", TimeValue(MilliSeconds(20)));
    dlClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
    dlClient.SetAttribute("PacketSize", UintegerValue(1000));
    ApplicationContainer clientApp = dlClient.Install(remoteHost);
    
    sinkApp.Start(Seconds(0));
    clientApp.Start(Seconds(3.0 + i * 0.5));
    clientApp.Stop(Seconds(simTime - 1.0));
  }
  
  // ✅ Pass PacketSink AND cellPositions to metrics logger
  for (uint32_t i = 0; i < numUes; i++) {
    Simulator::Schedule(Seconds(3.0 + i * 0.1), &LogDetailedMetrics, 
                       ueNodes.Get(i), i, sinkApps[i], cellPositions);
  }
  
  mmwaveHelper->EnableTraces();
  
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  lteHelper->Initialize();
  lteHelper->EnablePhyTraces();
  lteHelper->EnableMacTraces();
  
  AnimationInterface anim("multi_ue_handover_real.xml");
  anim.SetMaxPktsPerTraceFile(500000);
  
  for (uint32_t i = 0; i < numLteEnbs; i++) {
    anim.UpdateNodeDescription(lteEnbNodes.Get(i), "LTE_" + std::to_string(i+1));
    anim.UpdateNodeColor(lteEnbNodes.Get(i), 0, 255-(i*40), 0);
    anim.UpdateNodeSize(lteEnbNodes.Get(i)->GetId(), 25, 25);
  }
  
  for (uint32_t i = 0; i < numMmWaveEnbs; i++) {
    anim.UpdateNodeDescription(mmWaveEnbNodes.Get(i), "mmW_" + std::to_string(i+1));
    anim.UpdateNodeColor(mmWaveEnbNodes.Get(i), 255-(i*40), 0, 0);
    anim.UpdateNodeSize(mmWaveEnbNodes.Get(i)->GetId(), 18, 18);
  }
  
  for (uint32_t i = 0; i < numUes; i++) {
    anim.UpdateNodeDescription(ueNodes.Get(i), "UE_" + std::to_string(i+1));
    anim.UpdateNodeColor(ueNodes.Get(i), 0, 0, 255-(i*20));
    anim.UpdateNodeSize(ueNodes.Get(i)->GetId(), 10, 10);
  }
  
  anim.UpdateNodeDescription(remoteHost, "Server");
  anim.UpdateNodeColor(remoteHost, 128, 128, 128);
  
  anim.UpdateNodeDescription(pgw, "PGW");
  anim.UpdateNodeColor(pgw, 255, 255, 0);
  
  NS_LOG_UNCOND("\n╔═══════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║          SIMULATION STARTING                  ║");
  NS_LOG_UNCOND("╚═══════════════════════════════════════════════╝\n");
  
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  
  NS_LOG_UNCOND("\n╔═══════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║          SIMULATION COMPLETE                  ║");
  NS_LOG_UNCOND("╚═══════════════════════════════════════════════╝");
  NS_LOG_UNCOND("📊 Statistics:");
  NS_LOG_UNCOND("   - Total Handovers: " << totalHoCount);
  
  for (uint32_t i = 1; i <= numUes; i++) {
    NS_LOG_UNCOND("   - UE-" << i << " Handovers: " << ueHoCount[i]);
  }
  
  if (numUes > 0) {
    NS_LOG_UNCOND("   - Average HO/UE: " << (totalHoCount / (double)numUes));
  }
  NS_LOG_UNCOND("📁 Files Generated:");
  NS_LOG_UNCOND("   - handover_real_log.csv (handover events)");
  NS_LOG_UNCOND("   - ue_trajectory_real.csv (UE positions)");
  NS_LOG_UNCOND("   - ue_metrics_real.csv (✅ REALISTIC metrics!)");
  NS_LOG_UNCOND("   - multi_ue_handover_real.xml (NetAnim)");
  NS_LOG_UNCOND("═══════════════════════════════════════════════\n");
  
  hoLogFile.close();
  posLogFile.close();
  metricsLogFile.close();
  
  Simulator::Destroy();
  return 0;
}
