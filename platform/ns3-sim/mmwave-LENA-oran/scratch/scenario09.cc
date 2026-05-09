/* Enhanced Periodic Handover with Detailed Visualization
 * Adds: Position logging, handover tracking, visual colors
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

using namespace ns3;
using namespace mmwave;

NS_LOG_COMPONENT_DEFINE("EnhancedHandover");

// Global tracking
int hoCount = 0;
uint16_t currentServingCell = 1;
std::ofstream hoLogFile;
std::ofstream posLogFile;

// Enhanced handover start callback
void NotifyHandoverStart(std::string context, uint64_t imsi, 
                        uint16_t cellId, uint16_t rnti, uint16_t targetCellId) {
  hoCount++;
  double now = Simulator::Now().GetSeconds();
  
  NS_LOG_UNCOND("\n╔════════════════════════════════════════╗");
  NS_LOG_UNCOND("║     HANDOVER #" << hoCount << " INITIATED        ║");
  NS_LOG_UNCOND("╚════════════════════════════════════════╝");
  NS_LOG_UNCOND("⏱️  Time: " << now << "s");
  NS_LOG_UNCOND("📱 IMSI: " << imsi << " | RNTI: " << rnti);
  NS_LOG_UNCOND("📡 From Cell: " << cellId << " (LTE)");
  NS_LOG_UNCOND("📡 To Cell:   " << targetCellId << " (LTE)");
  NS_LOG_UNCOND("🔄 Expected mmWave: " << (cellId + 2) << " → " << (targetCellId + 2));
  NS_LOG_UNCOND("════════════════════════════════════════");
  
  // Log to file
  hoLogFile << now << "," << hoCount << "," << cellId << "," << targetCellId 
            << ",START" << std::endl;
}

void NotifyHandoverEnd(std::string context, uint64_t imsi, 
                      uint16_t cellId, uint16_t rnti) {
  double now = Simulator::Now().GetSeconds();
  currentServingCell = cellId;
  
  NS_LOG_UNCOND("✅ HANDOVER #" << hoCount << " COMPLETED");
  NS_LOG_UNCOND("⏱️  Time: " << now << "s");
  NS_LOG_UNCOND("📡 New Serving Cell: " << cellId);
  NS_LOG_UNCOND("════════════════════════════════════════\n");
  
  // Log to file
  hoLogFile << now << "," << hoCount << "," << cellId << "," << cellId 
            << ",COMPLETE" << std::endl;
}

// Track connection state
void NotifyConnectionEstablished(std::string context, uint64_t imsi, 
                                uint16_t cellId, uint16_t rnti) {
  NS_LOG_UNCOND("🔗 CONNECTION ESTABLISHED: Cell " << cellId 
                << " at t=" << Simulator::Now().GetSeconds() << "s");
}

// Reverse UE direction with detailed logging
void ReverseUeDirection(Ptr<Node> ueNode) {
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNode->GetObject<ConstantVelocityMobilityModel>();
  
  Vector currentVel = ueMob->GetVelocity();
  Vector currentPos = ueMob->GetPosition();
  
  Vector newVel = Vector(-currentVel.x, currentVel.y, currentVel.z);
  ueMob->SetVelocity(newVel);
  
  double now = Simulator::Now().GetSeconds();
  std::string direction = (newVel.x > 0) ? "EAST →" : "WEST ←";
  
  NS_LOG_UNCOND("\n🔄═════════════════════════════════════");
  NS_LOG_UNCOND("   DIRECTION REVERSAL");
  NS_LOG_UNCOND("═════════════════════════════════════");
  NS_LOG_UNCOND("⏱️  Time: " << now << "s");
  NS_LOG_UNCOND("📍 Position: (" << currentPos.x << ", " << currentPos.y << ")");
  NS_LOG_UNCOND("➡️  New Direction: " << direction);
  NS_LOG_UNCOND("🏃 Velocity: " << newVel.x << " m/s");
  NS_LOG_UNCOND("═════════════════════════════════════\n");
  
  // Log to file
  posLogFile << now << "," << currentPos.x << "," << currentPos.y << ","
             << newVel.x << ",REVERSE" << std::endl;
}

// Periodic position and state logging
void LogUeState(Ptr<Node> ueNode) {
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNode->GetObject<ConstantVelocityMobilityModel>();
  
  Vector pos = ueMob->GetPosition();
  Vector vel = ueMob->GetVelocity();
  double now = Simulator::Now().GetSeconds();
  
  // Log to file
  posLogFile << now << "," << pos.x << "," << pos.y << ","
             << vel.x << ",POSITION" << std::endl;
  
  // Periodic console output (every 5s)
  if ((int)now % 5 == 0) {
    std::string direction = (vel.x > 0) ? "→" : "←";
    NS_LOG_UNCOND("📊 t=" << now << "s: Pos=(" << pos.x << "," << pos.y 
                  << ") Dir=" << direction << " Cell=" << currentServingCell);
  }
  
  // Schedule next
  Simulator::Schedule(Seconds(0.5), &LogUeState, ueNode);
}

int main(int argc, char* argv[]) {
  double simTime = 50.0;
  double ueSpeed = 20.0;
  double reverseInterval = 18.0;
  
  CommandLine cmd;
  cmd.AddValue("simTime", "Simulation time", simTime);
  cmd.AddValue("ueSpeed", "UE speed (m/s)", ueSpeed);
  cmd.AddValue("reverseInterval", "Reversal interval (s)", reverseInterval);
  cmd.Parse(argc, argv);
  
  // Open log files
  hoLogFile.open("handover_log.csv");
  hoLogFile << "Time,HO_Number,Source_Cell,Target_Cell,Event" << std::endl;
  
  posLogFile.open("ue_trajectory.csv");
  posLogFile << "Time,X,Y,Velocity,Event" << std::endl;
  
  LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
  LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);
  
  NS_LOG_UNCOND("\n");
  NS_LOG_UNCOND("╔═══════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║  ENHANCED MMWAVE HANDOVER VISUALIZATION       ║");
  NS_LOG_UNCOND("╚═══════════════════════════════════════════════╝");
  NS_LOG_UNCOND("📊 Configuration:");
  NS_LOG_UNCOND("   - UE Speed: " << ueSpeed << " m/s");
  NS_LOG_UNCOND("   - Reversal Interval: " << reverseInterval << "s");
  NS_LOG_UNCOND("   - Simulation Time: " << simTime << "s");
  NS_LOG_UNCOND("📁 Output Files:");
  NS_LOG_UNCOND("   - handover_log.csv");
  NS_LOG_UNCOND("   - ue_trajectory.csv");
  NS_LOG_UNCOND("   - periodic_handover.xml (NetAnim)");
  NS_LOG_UNCOND("═══════════════════════════════════════════════\n");
  
  // Config
  Config::SetDefault("ns3::LteEnbRrc::SecondaryCellHandoverMode", 
                     StringValue("DynamicTtt"));
  Config::SetDefault("ns3::LteEnbRrc::HoSinrDifference", DoubleValue(2.0));
  Config::SetDefault("ns3::LteHelper::UseIdealRrc", BooleanValue(true));
  Config::SetDefault("ns3::MmWaveHelper::UseIdealRrc", BooleanValue(true));
  Config::SetDefault("ns3::A3RsrpHandoverAlgorithm::Hysteresis", DoubleValue(1.0));
  Config::SetDefault("ns3::A3RsrpHandoverAlgorithm::TimeToTrigger", 
                     TimeValue(MilliSeconds(128)));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(100e6));
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  
  // Create helper
  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
  mmwaveHelper->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
  mmwaveHelper->SetChannelConditionModelType(
      "ns3::ThreeGppUmiStreetCanyonChannelConditionModel");
  
  Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
  mmwaveHelper->SetEpcHelper(epcHelper);
  mmwaveHelper->SetLteHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
  
  // Network
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
  lteEnbNodes.Create(2);
  
  NodeContainer mmWaveEnbNodes;
  mmWaveEnbNodes.Create(2);
  
  NodeContainer ueNodes;
  ueNodes.Create(1);
  
  // Positions
  Ptr<ListPositionAllocator> enbPosAlloc = CreateObject<ListPositionAllocator>();
  enbPosAlloc->Add(Vector(200, 300, 10));  // LTE 1
  enbPosAlloc->Add(Vector(800, 300, 10));  // LTE 2
  enbPosAlloc->Add(Vector(200, 300, 10));  // mmWave 3
  enbPosAlloc->Add(Vector(800, 300, 10));  // mmWave 4
  
  MobilityHelper enbMobility;
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.SetPositionAllocator(enbPosAlloc);
  enbMobility.Install(lteEnbNodes);
  enbMobility.Install(mmWaveEnbNodes);
  
  // UE mobility
  MobilityHelper ueMobility;
  ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  ueMobility.Install(ueNodes);
  
  Ptr<ConstantVelocityMobilityModel> ueMob = 
      ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
  ueMob->SetPosition(Vector(250, 300, 1.5));
  ueMob->SetVelocity(Vector(ueSpeed, 0, 0));
  
  // Install devices
  NetDeviceContainer lteEnbDevs = mmwaveHelper->InstallLteEnbDevice(lteEnbNodes);
  NetDeviceContainer mmWaveEnbDevs = mmwaveHelper->InstallEnbDevice(mmWaveEnbNodes);
  NetDeviceContainer mcUeDevs = mmwaveHelper->InstallMcUeDevice(ueNodes);
  
  // Install IP
  internet.Install(ueNodes);
  Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(mcUeDevs);
  
  Ptr<Ipv4StaticRouting> ueStaticRouting =
      ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(0)->GetObject<Ipv4>());
  ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  
  // Add X2
  mmwaveHelper->AddX2Interface(lteEnbNodes, mmWaveEnbNodes);
  
  // Attach
  mmwaveHelper->AttachToClosestEnb(mcUeDevs, mmWaveEnbDevs, lteEnbDevs);
  
  // Connect traces
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                  MakeCallback(&NotifyConnectionEstablished));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                  MakeCallback(&NotifyHandoverStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                  MakeCallback(&NotifyHandoverEnd));
  
  // Schedule direction reversals
  for (double t = reverseInterval; t < simTime; t += reverseInterval) {
    Simulator::Schedule(Seconds(t), &ReverseUeDirection, ueNodes.Get(0));
  }
  
  // Start position logging
  Simulator::Schedule(Seconds(0.5), &LogUeState, ueNodes.Get(0));
  
  // Traffic
  PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), 1234));
  ApplicationContainer sinkApp = dlPacketSinkHelper.Install(ueNodes.Get(0));
  
  UdpClientHelper dlClient(ueIpIface.GetAddress(0), 1234);
  dlClient.SetAttribute("Interval", TimeValue(MicroSeconds(1000)));
  dlClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
  dlClient.SetAttribute("PacketSize", UintegerValue(500));
  ApplicationContainer clientApp = dlClient.Install(remoteHost);
  
  sinkApp.Start(Seconds(0.5));
  clientApp.Start(Seconds(1.0));
  clientApp.Stop(Seconds(simTime - 0.1));
  
  // Enable traces
  mmwaveHelper->EnableTraces();
  
  // Enhanced NetAnim with colors
  AnimationInterface anim("periodic_handover.xml");
  anim.SetMaxPktsPerTraceFile(500000);
  
  // Set node descriptions
  anim.UpdateNodeDescription(lteEnbNodes.Get(0), "LTE_Cell_1");
  anim.UpdateNodeDescription(lteEnbNodes.Get(1), "LTE_Cell_2");
  anim.UpdateNodeDescription(mmWaveEnbNodes.Get(0), "mmWave_Cell_3");
  anim.UpdateNodeDescription(mmWaveEnbNodes.Get(1), "mmWave_Cell_4");
  anim.UpdateNodeDescription(ueNodes.Get(0), "UE_IMSI_1");
  anim.UpdateNodeDescription(remoteHost, "RemoteHost");
  anim.UpdateNodeDescription(pgw, "PGW");
  
  // Set colors (RGB)
  anim.UpdateNodeColor(lteEnbNodes.Get(0), 0, 255, 0);      // Green
  anim.UpdateNodeColor(lteEnbNodes.Get(1), 0, 200, 0);      // Dark green
  anim.UpdateNodeColor(mmWaveEnbNodes.Get(0), 255, 0, 0);   // Red
  anim.UpdateNodeColor(mmWaveEnbNodes.Get(1), 200, 0, 0);   // Dark red
  anim.UpdateNodeColor(ueNodes.Get(0), 0, 0, 255);          // Blue
  anim.UpdateNodeColor(remoteHost, 128, 128, 128);          // Gray
  anim.UpdateNodeColor(pgw, 255, 255, 0);                   // Yellow
  
  // Set node sizes
  anim.UpdateNodeSize(lteEnbNodes.Get(0)->GetId(), 20, 20);
  anim.UpdateNodeSize(lteEnbNodes.Get(1)->GetId(), 20, 20);
  anim.UpdateNodeSize(mmWaveEnbNodes.Get(0)->GetId(), 15, 15);
  anim.UpdateNodeSize(mmWaveEnbNodes.Get(1)->GetId(), 15, 15);
  anim.UpdateNodeSize(ueNodes.Get(0)->GetId(), 10, 10);
  
  NS_LOG_UNCOND("╔═══════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║          SIMULATION STARTING                  ║");
  NS_LOG_UNCOND("╚═══════════════════════════════════════════════╝\n");
  
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  
  NS_LOG_UNCOND("\n╔═══════════════════════════════════════════════╗");
  NS_LOG_UNCOND("║          SIMULATION COMPLETE                  ║");
  NS_LOG_UNCOND("╚═══════════════════════════════════════════════╝");
  NS_LOG_UNCOND("📊 Statistics:");
  NS_LOG_UNCOND("   - Total Handovers: " << hoCount);
  NS_LOG_UNCOND("   - Average HO Interval: " << (simTime / hoCount) << "s");
  NS_LOG_UNCOND("📁 Output Files Generated:");
  NS_LOG_UNCOND("   - handover_log.csv");
  NS_LOG_UNCOND("   - ue_trajectory.csv");
  NS_LOG_UNCOND("   - periodic_handover.xml");
  NS_LOG_UNCOND("═══════════════════════════════════════════════\n");
  
  hoLogFile.close();
  posLogFile.close();
  
  Simulator::Destroy();
  return 0;
}
