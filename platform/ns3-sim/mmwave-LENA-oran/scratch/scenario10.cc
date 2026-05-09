/* Enhanced Multi-UE/Multi-Cell Periodic Handover Simulation
 * Irregular cell placement with UEs starting from different cells
 * Fixed segmentation fault issues
   bool e2lteEnabled = false;  // DISABLED for testing
  bool e2nrEnabled = false;   // DISABLED for testing
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
#include <iostream>
#include <fstream>
#include <map>

using namespace ns3;
using namespace mmwave;

NS_LOG_COMPONENT_DEFINE("MultiUeHandover");

// Global tracking
int totalHoCount = 0;
std::map<uint64_t, int> ueHoCount;
std::map<uint64_t, uint16_t> ueCurrentCell;
std::map<uint64_t, bool> ueInHandover;  // Track handover state
std::ofstream hoLogFile;
std::ofstream posLogFile;

void NotifyHandoverStart(std::string context, uint64_t imsi, 
                        uint16_t cellId, uint16_t rnti, uint16_t targetCellId) {
  totalHoCount++;
  ueHoCount[imsi]++;
  ueInHandover[imsi] = true;  // Mark as in handover
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
  ueInHandover[imsi] = false;  // Clear handover flag
  
  NS_LOG_UNCOND("✅ HANDOVER COMPLETED - UE-" << imsi);
  NS_LOG_UNCOND("⏱️  Time: " << now << "s | New Cell: " << cellId);
  NS_LOG_UNCOND("🔄 Entering cooling period (1s)...");
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
  
  // Check if UE is in handover - skip reversal if so
  uint64_t imsi = ueId + 1;
  if (ueInHandover[imsi]) {
    NS_LOG_UNCOND("⚠️  UE-" << imsi << " in handover - skipping reversal");
    // Reschedule for later
    Simulator::Schedule(Seconds(2.0), &ReverseUeDirection, ueNode, ueId);
    return;
  }
  
  Vector currentVel = ueMob->GetVelocity();
  Vector currentPos = ueMob->GetPosition();
  
  // Simple X-axis reversal (more stable than 2D)
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

// Check boundaries and reverse if needed
void CheckBoundaries(Ptr<Node> ueNode, uint32_t ueId) {
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNode->GetObject<ConstantVelocityMobilityModel>();
  
  Vector pos = ueMob->GetPosition();
  Vector vel = ueMob->GetVelocity();
  
  // Define safe boundaries (between cells)
  double minX = 200;
  double maxX = 800;
  
  bool needReverse = false;
  
  if (pos.x < minX && vel.x < 0) {
    // Moving west beyond boundary
    needReverse = true;
    NS_LOG_UNCOND("⚠️  UE-" << (ueId+1) << " hit WEST boundary at x=" << pos.x);
  } else if (pos.x > maxX && vel.x > 0) {
    // Moving east beyond boundary
    needReverse = true;
    NS_LOG_UNCOND("⚠️  UE-" << (ueId+1) << " hit EAST boundary at x=" << pos.x);
  }
  
  if (needReverse) {
    Vector newVel = Vector(-vel.x, 0, 0);
    ueMob->SetVelocity(newVel);
    NS_LOG_UNCOND("🔄 Auto-reversing UE-" << (ueId+1) << " direction\n");
  }
  
  // Schedule next check
  Simulator::Schedule(Seconds(0.5), &CheckBoundaries, ueNode, ueId);
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
  
  Simulator::Schedule(Seconds(0.5), &LogUeState, ueNode, ueId);
}

int main(int argc, char* argv[]) {
  uint32_t numLteEnbs = 4;      // Increase to 4
  uint32_t numMmWaveEnbs = 4;   // Increase to 4
  uint32_t numUes = 3;          // Increase to 3
  double simTime = 50.0;
  double ueSpeed = 10.0;
  double reverseInterval = 20.0;
  
  CommandLine cmd;
  cmd.AddValue("numLteEnbs", "Number of LTE eNBs", numLteEnbs);
  cmd.AddValue("numMmWaveEnbs", "Number of mmWave gNBs", numMmWaveEnbs);
  cmd.AddValue("numUes", "Number of UEs", numUes);
  cmd.AddValue("simTime", "Simulation time", simTime);
  cmd.AddValue("ueSpeed", "UE speed (m/s)", ueSpeed);
  cmd.AddValue("reverseInterval", "Reversal interval (s)", reverseInterval);
  cmd.Parse(argc, argv);
  
  // Open log files
  hoLogFile.open("handover_log.csv");
  hoLogFile << "Time,IMSI,UE_HO_Number,Source_Cell,Target_Cell,Event" << std::endl;
  
  posLogFile.open("ue_trajectory.csv");
  posLogFile << "Time,UE_ID,X,Y,VelocityX,VelocityY,Event" << std::endl;
  
  // Disable detailed RRC logs to reduce clutter
  // Also reduce E2-related logging
  LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);  // Show handovers
  LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);   // Show UE side
  LogComponentEnable("EpcX2", LOG_LEVEL_INFO);      // Show X2 messages
  
  NS_LOG_UNCOND("\n╔═══════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║   MULTI-UE MMWAVE HANDOVER SIMULATION         ║");
  NS_LOG_UNCOND("║   (Irregular Cell Placement + E2/RIC)         ║");
  NS_LOG_UNCOND("╚═══════════════════════════════════════════════╝");
  NS_LOG_UNCOND("📊 Configuration:");
  NS_LOG_UNCOND("   - LTE eNBs: " << numLteEnbs);
  NS_LOG_UNCOND("   - mmWave gNBs: " << numMmWaveEnbs);
  NS_LOG_UNCOND("   - Total E2 Agents: " << (numLteEnbs + numMmWaveEnbs));
  NS_LOG_UNCOND("   - UEs: " << numUes);
  NS_LOG_UNCOND("   - UE Speed: " << ueSpeed << " m/s");
  NS_LOG_UNCOND("   - Reversal Interval: " << reverseInterval << "s");
  NS_LOG_UNCOND("   - Simulation Time: " << simTime << "s");
  NS_LOG_UNCOND("═══════════════════════════════════════════════\n");
  
  // E2 Configuration (like scenario0-1.cc)
  double indicationPeriodicity = 0.1;  // 100ms default
  bool e2lteEnabled = false;  // DISABLED for testing
  bool e2nrEnabled = false;   // DISABLED for testing
  bool e2du = false;
  bool e2cuUp = false;
  bool e2cuCp = false;
  bool reducedPmValues = false;
  bool enableE2FileLogging = false;
  std::string e2TermIp = "127.0.0.1";
  
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
  
  // Configuration - Modified for stability
  // Configuration - Modified for stability
  Config::SetDefault("ns3::LteEnbRrc::SecondaryCellHandoverMode", 
                     StringValue("FixedTtt"));
  Config::SetDefault("ns3::LteEnbRrc::HoSinrDifference", DoubleValue(3.0));
  Config::SetDefault("ns3::LteHelper::UseIdealRrc", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::UseIdealRrc", BooleanValue(true));
  Config::SetDefault("ns3::A3RsrpHandoverAlgorithm::Hysteresis", DoubleValue(4.0));  // Higher
  Config::SetDefault("ns3::A3RsrpHandoverAlgorithm::TimeToTrigger", 
                     TimeValue(MilliSeconds(320)));  // Longer
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(100e6));
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  
  // Basic stability settings (only safe parameters)
  Config::SetDefault("ns3::LteEnbRrc::AdmitHandoverRequest", BooleanValue(true));
  Config::SetDefault("ns3::LteEnbRrc::AdmitRrcConnectionRequest", BooleanValue(true));;
  
  // Create helper
  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
  mmwaveHelper->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmwaveHelper->SetChannelConditionModelType(
      "ns3::ThreeGppUmiStreetCanyonChannelConditionModel");
  
  // E2 TermIP - will connect to RIC
  Config::SetDefault("ns3::MmWaveHelper::E2TermIp", StringValue(e2TermIp));
  
  Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
  mmwaveHelper->SetEpcHelper(epcHelper);
  mmwaveHelper->SetLteHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
  
  // Network setup
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
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), 
                                             Ipv4Mask("255.0.0.0"), 1);
  
  // Create nodes
  NodeContainer lteEnbNodes;
  lteEnbNodes.Create(numLteEnbs);
  
  NodeContainer mmWaveEnbNodes;
  mmWaveEnbNodes.Create(numMmWaveEnbs);
  
  NodeContainer ueNodes;
  ueNodes.Create(numUes);
  
  // ═══════════════════════════════════════════════════════════
  // IRREGULAR CELL POSITIONS - Not on a straight line
  // ═══════════════════════════════════════════════════════════
  Ptr<ListPositionAllocator> enbPosAlloc = CreateObject<ListPositionAllocator>();
  
  // Predefined irregular positions for cells (x, y, z)
  // 4 cells for better coverage and more handovers
  std::vector<Vector> cellPositions = {
    Vector(300, 300, 10),   // Cell 1 - West
    Vector(500, 280, 10),   // Cell 2 - Center-North
    Vector(700, 300, 10),   // Cell 3 - East
    Vector(500, 400, 10)    // Cell 4 - South
  };
  
  NS_LOG_UNCOND("📡 Cell Positions (Irregular Layout):");
  
  // LTE eNBs positions
  for (uint32_t i = 0; i < numLteEnbs && i < cellPositions.size(); i++) {
    enbPosAlloc->Add(cellPositions[i]);
    NS_LOG_UNCOND("   LTE eNB-" << (i+1) << " at (" 
                  << cellPositions[i].x << ", " << cellPositions[i].y << ")");
  }
  
  // mmWave gNBs positions (co-located with LTE)
  for (uint32_t i = 0; i < numMmWaveEnbs && i < cellPositions.size(); i++) {
    enbPosAlloc->Add(cellPositions[i]);
    NS_LOG_UNCOND("   mmWave gNB-" << (i+1) << " at (" 
                  << cellPositions[i].x << ", " << cellPositions[i].y << ")");
  }
  
  MobilityHelper enbMobility;
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.SetPositionAllocator(enbPosAlloc);
  enbMobility.Install(lteEnbNodes);
  enbMobility.Install(mmWaveEnbNodes);
  
  // ═══════════════════════════════════════════════════════════
  // UE MOBILITY - Each UE starts from a different cell
  // ═══════════════════════════════════════════════════════════
  MobilityHelper ueMobility;
  ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  ueMobility.Install(ueNodes);
  
  NS_LOG_UNCOND("\n📱 UE Starting Positions:");
  
  for (uint32_t i = 0; i < numUes; i++) {
    Ptr<ConstantVelocityMobilityModel> ueMob = 
        ueNodes.Get(i)->GetObject<ConstantVelocityMobilityModel>();
    
    // Each UE starts near a different cell with MORE spacing
    uint32_t cellIdx = i % cellPositions.size();
    Vector cellPos = cellPositions[cellIdx];
    
    // Much larger offset to delay handovers
    double offsetX = 50 + (i * 100);  // BIG spacing between UEs
    double offsetY = 10 * i;
    
    Vector ueStartPos(cellPos.x + offsetX, cellPos.y + offsetY, 1.5);
    
    // Different speeds to avoid simultaneous handovers
    double speedVariation = ueSpeed + (i * 4);  // Even bigger difference
    Vector velocity(speedVariation, 0, 0);
    
    ueMob->SetPosition(ueStartPos);
    ueMob->SetVelocity(velocity);
    
    NS_LOG_UNCOND("   UE-" << (i+1) << ": Start at (" << (int)ueStartPos.x 
                  << ", " << (int)ueStartPos.y << ") near Cell-" << (cellIdx+1));
    NS_LOG_UNCOND("          Moving EAST at " << speedVariation << " m/s");
  }
  
  // Install devices
  NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice(lteEnbNodes);
  NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice(mmWaveEnbNodes);
  NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice(ueNodes);
  
  // Install IP
  internet.Install(ueNodes);
  Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(mcUeDevs);
  
  for (uint32_t i = 0; i < numUes; i++) {
    Ptr<Ipv4StaticRouting> ueStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(i)->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }
  
  // Add X2
  mmwaveHelper->AddX2Interface(lteEnbNodes, mmWaveEnbNodes);
  
  // Attach - Important: Give time for initial setup
  mmwaveHelper->AttachToClosestEnb(mcUeDevs, mmWaveEnbDevs, lteEnbDevs);
  
  // Connect traces
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                  MakeCallback(&NotifyConnectionEstablished));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                  MakeCallback(&NotifyHandoverStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                  MakeCallback(&NotifyHandoverEnd));
  
  // Schedule direction reversals with LARGE offset to avoid concurrent handovers
  for (uint32_t i = 0; i < numUes; i++) {
    double offset = i * 10.0;  // 10 seconds between each UE - CRITICAL!
    
    for (double t = reverseInterval + offset; t < simTime; t += reverseInterval) {
      Simulator::Schedule(Seconds(t), &ReverseUeDirection, ueNodes.Get(i), i);
    }
  }
  
  // Start position logging
  for (uint32_t i = 0; i < numUes; i++) {
    Simulator::Schedule(Seconds(1.0 + i * 0.1), &LogUeState, ueNodes.Get(i), i);
  }
  
  // Start boundary checking - CRITICAL for preventing out-of-coverage crashes
  for (uint32_t i = 0; i < numUes; i++) {
    Simulator::Schedule(Seconds(0.5), &CheckBoundaries, ueNodes.Get(i), i);
  }
  
  // Traffic - Very conservative to avoid crashes
  for (uint32_t i = 0; i < numUes; i++) {
    uint16_t dlPort = 1234 + i;
    
    PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), dlPort));
    ApplicationContainer sinkApp = dlPacketSinkHelper.Install(ueNodes.Get(i));
    
    UdpClientHelper dlClient(ueIpIface.GetAddress(i), dlPort);
    dlClient.SetAttribute("Interval", TimeValue(MilliSeconds(20)));  // Very conservative
    dlClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
    dlClient.SetAttribute("PacketSize", UintegerValue(500));
    ApplicationContainer clientApp = dlClient.Install(remoteHost);
    
    sinkApp.Start(Seconds(3.0));  // Wait longer for connection
    clientApp.Start(Seconds(3.5 + i * 0.5));
    clientApp.Stop(Seconds(simTime - 1.0));
  }
  
  // Enable traces
  mmwaveHelper->EnableTraces();
  
  // NetAnim
  AnimationInterface anim("multi_ue_handover.xml");
  anim.SetMaxPktsPerTraceFile(500000);
  
  // Colors
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
    anim.UpdateNodeColor(ueNodes.Get(i), 0, 0, 255-(i*50));
    anim.UpdateNodeSize(ueNodes.Get(i)->GetId(), 12, 12);
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
  NS_LOG_UNCOND("📁 Files: handover_log.csv, ue_trajectory.csv, multi_ue_handover.xml");
  NS_LOG_UNCOND("═══════════════════════════════════════════════\n");
  
  hoLogFile.close();
  posLogFile.close();
  
  Simulator::Destroy();
  return 0;
}
