//by Alyaaaaa
//بسم الله الرحمن الرحيم - NR MANUAL Handover ns-3.42 (ns-O-RAN-flexric) - BASELINE VERSION
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
#include "ns3/isotropic-antenna-model.h"
#include <ns3/lte-ue-net-device.h>
#include <ns3/mmwave-ue-net-device.h>
#include <ns3/mmwave-enb-net-device.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <cmath>
#include <deque>

using namespace ns3;
using namespace mmwave;

NS_LOG_COMPONENT_DEFINE("GraduationNrHandoverBaseline");

// ============================================================
// GLOBAL VARIABLES
// ============================================================
Ptr<MmWaveHelper> g_mmwaveHelper;
NetDeviceContainer g_ueNetDev;
NetDeviceContainer g_enbNetDev;
NodeContainer g_enbNodes;
NodeContainer g_ueNodes;
uint32_t g_hoCounter = 0;
std::ofstream* g_hoLog;
std::ofstream* g_aiDataLog;

// Track handover state
std::map<uint64_t, double> g_lastHandoverTime;
std::map<uint64_t, uint16_t> g_currentServingCell;
std::map<uint64_t, bool> g_manualHandoverScheduled;

// ✅ Track handover quality
std::map<uint64_t, std::deque<uint16_t>> g_cellHistory;
std::map<uint64_t, uint32_t> g_pingPongCount;
std::map<uint64_t, uint32_t> g_hoFailureCount;
std::map<uint64_t, uint32_t> g_successfulHO;
uint32_t g_totalFailures = 0;
uint32_t g_totalPingPongs = 0;
uint32_t g_totalSuccess = 0;

// Track throughput for each UE
std::map<uint64_t, double> g_dlBitrate;
std::map<uint64_t, double> g_ulBitrate;
std::map<uint64_t, uint64_t> g_lastRxBytes;
std::map<uint64_t, double> g_lastBitrateUpdate;

// Session tracking
uint32_t g_sessionCounter = 0;
std::map<uint64_t, uint32_t> g_ueSessionID;

// ============================================================
// HELPER FUNCTIONS
// ============================================================

double CalculateDistance(Ptr<Node> node1, Ptr<Node> node2)
{
    Ptr<MobilityModel> mob1 = node1->GetObject<MobilityModel>();
    Ptr<MobilityModel> mob2 = node2->GetObject<MobilityModel>();
    
    if (!mob1 || !mob2) return 0.0;
    
    Vector pos1 = mob1->GetPosition();
    Vector pos2 = mob2->GetPosition();
    
    double dx = pos1.x - pos2.x;
    double dy = pos1.y - pos2.y;
    double dz = pos1.z - pos2.z;
    
    return sqrt(dx*dx + dy*dy + dz*dz);
}

double GetUESpeed(Ptr<Node> ueNode)
{
    Ptr<MobilityModel> mobility = ueNode->GetObject<MobilityModel>();
    if (!mobility) return 0.0;
    
    Vector velocity = mobility->GetVelocity();
    return sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
}

double CalculateBearing(Ptr<Node> ueNode, Ptr<Node> cellNode)
{
    Ptr<MobilityModel> ueMob = ueNode->GetObject<MobilityModel>();
    Ptr<MobilityModel> cellMob = cellNode->GetObject<MobilityModel>();
    
    if (!ueMob || !cellMob) return 0.0;
    
    Vector uePos = ueMob->GetPosition();
    Vector cellPos = cellMob->GetPosition();
    
    double dx = cellPos.x - uePos.x;
    double dy = cellPos.y - uePos.y;
    
    double bearing = atan2(dy, dx) * 180.0 / M_PI;
    if (bearing < 0) bearing += 360.0;
    
    return bearing;
}

Ptr<Node> GetEnbNodeByCellId(uint16_t cellId)
{
    for (uint32_t i = 0; i < g_enbNetDev.GetN(); ++i) {
        Ptr<MmWaveEnbNetDevice> enbDev = DynamicCast<MmWaveEnbNetDevice>(g_enbNetDev.Get(i));
        if (enbDev && enbDev->GetCellId() == cellId) {
            return g_enbNodes.Get(i);
        }
    }
    return nullptr;
}

Ptr<Node> GetUeNodeByImsi(uint64_t imsi)
{
    for (uint32_t i = 0; i < g_ueNetDev.GetN(); ++i) {
        Ptr<MmWaveUeNetDevice> ueDev = DynamicCast<MmWaveUeNetDevice>(g_ueNetDev.Get(i));
        if (ueDev && ueDev->GetImsi() == imsi) {
            return g_ueNodes.Get(i);
        }
    }
    return nullptr;
}

int RsrpToNRxLev(int rsrp)
{
    return std::max(0, std::min(97, rsrp + 140));
}

int SnrToNQual(double snr)
{
    return std::max(0, std::min(34, (int)((snr + 20.0) * 34.0 / 50.0)));
}

double EstimateSNR(int rsrp)
{
    return (rsrp + 90.0) * 30.0 / 40.0 - 10.0;
}

int EstimateCQI(double snr)
{
    if (snr < -5) return 1;
    else if (snr > 25) return 15;
    else return (int)((snr + 5.0) * 14.0 / 30.0) + 1;
}

double CalculateRSRQ(int rsrp, int numNeighbors)
{
    double interference = numNeighbors * 2.0;
    return rsrp - interference - 10.0;
}

// ============================================================
// PING-PONG DETECTION
// ============================================================
bool DetectPingPong(uint64_t imsi, uint16_t newCell)
{
    if (g_cellHistory[imsi].size() >= 10) {
        g_cellHistory[imsi].pop_front();
    }
    g_cellHistory[imsi].push_back(newCell);
    
    if (g_cellHistory[imsi].size() >= 3) {
        for (size_t i = 0; i < g_cellHistory[imsi].size() - 1; ++i) {
            if (g_cellHistory[imsi][i] == newCell) {
                return true;
            }
        }
    }
    
    return false;
}

// ============================================================
// SIMULATE HANDOVER FAILURE
// ============================================================
bool SimulateHandoverFailure(uint64_t imsi, uint16_t sourceCellId, uint16_t targetCellId, 
                            int sourceCellRsrp, int targetCellRsrp, double ueSpeed)
{
    if (targetCellRsrp < -100 && (rand() % 100) < 30) {
        return true;
    }
    
    if (ueSpeed > 25.0 && (targetCellRsrp - sourceCellRsrp) < 8 && (rand() % 100) < 20) {
        return true;
    }
    
    if ((rand() % 100) < 5) {
        return true;
    }
    
    if (sourceCellRsrp < -95 && (rand() % 100) < 15) {
        return true;
    }
    
    return false;
}

// ============================================================
// THROUGHPUT TRACKING
// ============================================================
void RxCallback(std::string context, Ptr<const Packet> packet)
{
    std::size_t pos = context.find("/NodeList/");
    if (pos == std::string::npos) return;
    
    std::size_t start = pos + 10;
    std::size_t end = context.find("/", start);
    uint32_t nodeId = std::stoi(context.substr(start, end - start));
    
    for (uint32_t i = 0; i < g_ueNetDev.GetN(); ++i) {
        if (g_ueNodes.Get(i)->GetId() == nodeId) {
            Ptr<MmWaveUeNetDevice> ueDev = DynamicCast<MmWaveUeNetDevice>(g_ueNetDev.Get(i));
            if (ueDev) {
                double now = Simulator::Now().GetSeconds();
                uint32_t packetSize = packet->GetSize();
                uint64_t imsi = ueDev->GetImsi();
                
                if (g_lastBitrateUpdate.find(imsi) == g_lastBitrateUpdate.end()) {
                    g_lastBitrateUpdate[imsi] = now;
                    g_lastRxBytes[imsi] = 0;
                }
                
                g_lastRxBytes[imsi] += packetSize;
                
                double timeDiff = now - g_lastBitrateUpdate[imsi];
                if (timeDiff >= 1.0) {
                    double bitrate = (g_lastRxBytes[imsi] * 8.0) / timeDiff / 1e6;
                    g_dlBitrate[imsi] = bitrate;
                    g_lastRxBytes[imsi] = 0;
                    g_lastBitrateUpdate[imsi] = now;
                }
            }
            break;
        }
    }
}

// ============================================================
// MANUAL HANDOVER TRIGGER
// ============================================================
void TriggerManualHandover(uint64_t imsi, uint16_t currentCellId, uint16_t targetCellId, 
                          uint16_t rnti, int sourceCellRsrp, int targetCellRsrp)
{
    double now = Simulator::Now().GetSeconds();
    
    if (g_lastHandoverTime.find(imsi) != g_lastHandoverTime.end()) {
        if (now - g_lastHandoverTime[imsi] < 2.5) {
            return;
        }
    }
    
    uint16_t actualCurrentCell = currentCellId;
    if (g_currentServingCell.find(imsi) != g_currentServingCell.end()) {
        actualCurrentCell = g_currentServingCell[imsi];
    }
    
    if (actualCurrentCell == targetCellId) {
        return;
    }
    
    Ptr<Node> ueNode = GetUeNodeByImsi(imsi);
    double ueSpeed = ueNode ? GetUESpeed(ueNode) : 0.0;
    
    bool failed = SimulateHandoverFailure(imsi, actualCurrentCell, targetCellId, 
                                         sourceCellRsrp, targetCellRsrp, ueSpeed);
    
    bool isPingPong = DetectPingPong(imsi, targetCellId);
    
    g_manualHandoverScheduled[imsi] = true;
    g_hoCounter++;
    
    std::string hoType = "Manual";
    std::string hoStatus = "✅ SUCCESS";
    
    if (failed) {
        g_hoFailureCount[imsi]++;
        g_totalFailures++;
        hoStatus = "❌ FAILURE";
        hoType += " (Failed)";
    } else {
        g_successfulHO[imsi]++;
        g_totalSuccess++;
        g_lastHandoverTime[imsi] = now;
        g_currentServingCell[imsi] = targetCellId;
        
        if (isPingPong) {
            g_pingPongCount[imsi]++;
            g_totalPingPongs++;
            hoStatus = "⚠️ PING-PONG";
            hoType += " (Ping-Pong)";
        }
    }
    
    std::stringstream msg;
    msg << "HANDOVER #" << g_hoCounter << " | " << std::fixed << std::setprecision(3) 
        << now << "s | IMSI:" << imsi << " | Cell:" << actualCurrentCell 
        << " → Cell:" << targetCellId << " | Type: " << hoType 
        << " | Status: " << hoStatus
        << " | RSRP: " << sourceCellRsrp << "→" << targetCellRsrp << " dBm";
    
    std::cout << "\n🎯 " << msg.str() << std::endl;
    *g_hoLog << msg.str() << std::endl;
    g_hoLog->flush();
}

// ============================================================
// MEASUREMENT REPORT CALLBACK (SIMPLIFIED FOR MMWAVE)
// ============================================================
void CollectMeasurements()
{
    static std::ofstream rsrpLog("rsrp-measurements.txt", std::ios::app);
    static bool headerWritten = false;
    
    if (!headerWritten) {
        rsrpLog << "Time(s)\tIMSI\tServingCell\tEstRSRP\tSpeed\tDistance\n";
        headerWritten = true;
    }
    
    double now = Simulator::Now().GetSeconds();
    
    for (uint32_t i = 0; i < g_ueNetDev.GetN(); ++i) {
        Ptr<MmWaveUeNetDevice> ueDev = DynamicCast<MmWaveUeNetDevice>(g_ueNetDev.Get(i));
        if (!ueDev) continue;
        
        uint64_t imsi = ueDev->GetImsi();
        Ptr<Node> ueNode = g_ueNodes.Get(i);
        
        // Initialize session if needed
        if (g_ueSessionID.find(imsi) == g_ueSessionID.end()) {
            g_ueSessionID[imsi] = ++g_sessionCounter;
        }
        
        // Estimate current serving cell based on distance
        uint16_t closestCell = 1;
        double minDist = 1e9;
        
        for (uint32_t j = 0; j < g_enbNetDev.GetN(); ++j) {
            Ptr<MmWaveEnbNetDevice> enbDev = DynamicCast<MmWaveEnbNetDevice>(g_enbNetDev.Get(j));
            if (enbDev) {
                double dist = CalculateDistance(ueNode, g_enbNodes.Get(j));
                if (dist < minDist) {
                    minDist = dist;
                    closestCell = enbDev->GetCellId();
                }
            }
        }
        
        if (g_currentServingCell.find(imsi) == g_currentServingCell.end()) {
            g_currentServingCell[imsi] = closestCell;
        }
        
        uint16_t currentServingCell = g_currentServingCell[imsi];
        Ptr<Node> servingCellNode = GetEnbNodeByCellId(currentServingCell);
        
        if (!servingCellNode) continue;
        
        double speed = GetUESpeed(ueNode);
        double distance = CalculateDistance(ueNode, servingCellNode);
        double bearing = CalculateBearing(ueNode, servingCellNode);
        
        // Estimate RSRP based on distance (simplified path loss)
        int rsrp = -70 - (int)(20 * log10(std::max(1.0, distance)));
        double snr = EstimateSNR(rsrp);
        int cqi = EstimateCQI(snr);
        int nrxlev = RsrpToNRxLev(rsrp);
        int nqual = SnrToNQual(snr);
        
        // Find best neighbor
        uint16_t bestNeighborCell = 0;
        int bestNeighborRsrp = -999;
        double bestNeighborSnr = -999;
        int numNeighbors = 0;
        
        for (uint32_t j = 0; j < g_enbNetDev.GetN(); ++j) {
            Ptr<MmWaveEnbNetDevice> enbDev = DynamicCast<MmWaveEnbNetDevice>(g_enbNetDev.Get(j));
            if (enbDev && enbDev->GetCellId() != currentServingCell) {
                double nDist = CalculateDistance(ueNode, g_enbNodes.Get(j));
                int nRsrp = -70 - (int)(20 * log10(std::max(1.0, nDist)));
                
                numNeighbors++;
                
                if (nRsrp > bestNeighborRsrp) {
                    bestNeighborRsrp = nRsrp;
                    bestNeighborCell = enbDev->GetCellId();
                    bestNeighborSnr = EstimateSNR(nRsrp);
                }
            }
        }
        
        double rsrq = CalculateRSRQ(rsrp, numNeighbors);
        double dlBitrate = g_dlBitrate.find(imsi) != g_dlBitrate.end() ? g_dlBitrate[imsi] : 0.0;
        double ulBitrate = g_ulBitrate.find(imsi) != g_ulBitrate.end() ? g_ulBitrate[imsi] : 0.0;
        double bandwidth = 20.0; // MHz (from scenario-three)
        int rsrpDelta = bestNeighborRsrp - rsrp;
        
        // Write to AI training CSV
        *g_aiDataLog << g_ueSessionID[imsi] << ","
                     << std::fixed << std::setprecision(3) << now << ","
                     << ueNode->GetId() << "," << currentServingCell << "," << rsrp << ","
                     << std::setprecision(2) << snr << "," << nqual << "," << cqi << ","
                     << bestNeighborRsrp << "," << std::setprecision(2) << bestNeighborSnr << ","
                     << nrxlev << "," << nqual << "," << std::setprecision(2) << speed << ","
                     << std::setprecision(3) << dlBitrate << "," << ulBitrate << ","
                     << std::setprecision(1) << bandwidth << ","
                     << std::setprecision(2) << bearing << "," << rsrq << "," << rsrp
                     << std::endl;
        
        rsrpLog << now << "\t" << imsi << "\t" << currentServingCell << "\t" << rsrp 
                << "\t" << std::fixed << std::setprecision(2) << speed << "\t" 
                << std::setprecision(1) << distance << "\n";
        
        // Trigger handover if needed
        if (rsrpDelta >= 10 && bestNeighborCell != 0 && bestNeighborCell != currentServingCell) {
            std::cout << "📊 [" << std::fixed << std::setprecision(2) << now << "s] "
                      << "IMSI:" << imsi << " Cell:" << currentServingCell 
                      << " RSRP:" << rsrp << "dBm | Best neighbor Cell" << bestNeighborCell
                      << "(" << bestNeighborRsrp << "dBm) Δ=" << rsrpDelta 
                      << "dB 🔔 TRIGGERING HANDOVER!" << std::endl;
            
            Simulator::Schedule(MilliSeconds(10), &TriggerManualHandover, 
                              imsi, currentServingCell, bestNeighborCell, 0, 
                              rsrp, bestNeighborRsrp);
        }
    }
    
    rsrpLog.flush();
    g_aiDataLog->flush();
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char* argv[])
{
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "🎓 GRADUATION PROJECT: NR HANDOVER - BASELINE (ns-3.42)\n";
    std::cout << std::string(70, '=') << "\n" << std::endl;

    srand(time(NULL));

    // Parameters
    uint8_t nEnbNodes = 3;
    uint8_t nUeNodes = 5;
    double simTime = 60.0;
    double ueSpeed = 20.0;
    double bandwidth = 20e6;
    double centerFrequency = 3.5e9;
    double isd = 500.0;

    CommandLine cmd;
    cmd.AddValue("nEnbNodes", "Number of eNBs", nEnbNodes);
    cmd.AddValue("nUeNodes", "Number of UEs", nUeNodes);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("ueSpeed", "UE speed in m/s", ueSpeed);
    cmd.Parse(argc, argv);

    std::cout << "🔥 Network: " << (int)nEnbNodes << " eNBs, " << (int)nUeNodes << " UEs\n";
    std::cout << "🔥 Simulation: " << simTime << "s\n";
    std::cout << "🔥 UE Speed: " << ueSpeed << " m/s\n";
    std::cout << "🔥 Bandwidth: " << bandwidth/1e6 << " MHz @ " << centerFrequency/1e9 << " GHz\n";
    std::cout << "🎯 Expected: 85-90% Success, 5-10% Ping-Pong, 5% Failures\n" << std::endl;

    // Configuration (based on scenario-three)
    Config::SetDefault("ns3::MmWaveHelper::UseIdealRrc", BooleanValue(true));
    Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(bandwidth));
    Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(centerFrequency));
    Config::SetDefault("ns3::PhasedArrayModel::AntennaElement",
                       PointerValue(CreateObject<IsotropicAntennaModel>()));
    Config::SetDefault("ns3::MmWavePhyMacCommon::NumHarqProcess", UintegerValue(20));
    Config::SetDefault("ns3::MmWaveHelper::HarqEnabled", BooleanValue(true));

    std::cout << "✅ Configuration: Ideal RRC, 20 HARQ processes\n" << std::endl;

    // Create helpers
    std::cout << "🔧 Creating MmWave helpers..." << std::endl;
    g_mmwaveHelper = CreateObject<MmWaveHelper>();
    g_mmwaveHelper->SetPathlossModelType("ns3::ThreeGppUmiStreetCanyonPropagationLossModel");
    g_mmwaveHelper->SetChannelConditionModelType("ns3::ThreeGppUmiStreetCanyonChannelConditionModel");
    
    Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
    g_mmwaveHelper->SetEpcHelper(epcHelper);
    std::cout << "✅ MmWave helpers created\n" << std::endl;

    // Remote Host
    std::cout << "🔧 Setting up remote host..." << std::endl;
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);
    
    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(2500));
    p2ph.SetChannelAttribute("Delay", TimeValue(Seconds(0.001)));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);

    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting = 
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);
    std::cout << "✅ Remote host configured\n" << std::endl;

    // Create Nodes
    g_enbNodes.Create(nEnbNodes);
    g_ueNodes.Create(nUeNodes);

    // eNB Positions - Triangle
    std::cout << "🔧 Configuring eNB positions..." << std::endl;
    MobilityHelper enbMobility;
    Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
    
    enbPositionAlloc->Add(Vector(0.0, 0.0, 30.0));
    enbPositionAlloc->Add(Vector(isd, 0.0, 30.0));
    enbPositionAlloc->Add(Vector(isd/2, isd * sqrt(3)/2, 30.0));

    enbMobility.SetPositionAllocator(enbPositionAlloc);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(g_enbNodes);
    std::cout << "✅ eNB positions set (triangle: " << isd << "m sides)\n" << std::endl;

    // UE Mobility
    std::cout << "🔧 Configuring UE mobility..." << std::endl;
    MobilityHelper ueMobility;
    Ptr<ListPositionAllocator> uePositionAlloc = CreateObject<ListPositionAllocator>();
    
    uePositionAlloc->Add(Vector(50.0, 50.0, 1.5));
    uePositionAlloc->Add(Vector(80.0, 30.0, 1.5));
    uePositionAlloc->Add(Vector(450.0, 50.0, 1.5));
    uePositionAlloc->Add(Vector(200.0, 380.0, 1.5));
    uePositionAlloc->Add(Vector(250.0, 100.0, 1.5));
    
    for (uint8_t i = 5; i < nUeNodes; ++i) {
        double x = (rand() % (int)(isd * 1.2));
        double y = (rand() % (int)(isd * 1.2));
        uePositionAlloc->Add(Vector(x, y, 1.5));
    }
    
    ueMobility.SetPositionAllocator(uePositionAlloc);
    
    std::stringstream speedStr;
    speedStr << "ns3::UniformRandomVariable[Min=" << (ueSpeed - 5.0) 
             << "|Max=" << (ueSpeed + 5.0) << "]";
    
    ueMobility.SetMobilityModel("ns3::RandomDirection2dMobilityModel",
                               "Speed", StringValue(speedStr.str()),
                               "Pause", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"),
                               "Bounds", RectangleValue(Rectangle(-100, isd*1.5, -100, isd*1.5)));
    ueMobility.Install(g_ueNodes);
    std::cout << "✅ UE mobility configured\n" << std::endl;

    // Install Devices (mmWave-only approach)
    std::cout << "🔧 Installing devices..." << std::endl;
    g_enbNetDev = g_mmwaveHelper->InstallEnbDevice(g_enbNodes);
    g_ueNetDev = g_mmwaveHelper->InstallUeDevice(g_ueNodes);  // Regular UE instead of MC
    std::cout << "✅ Devices installed\n" << std::endl;

    // Internet Stack
    std::cout << "🔧 Installing internet stack..." << std::endl;
    internet.Install(g_ueNodes);
    Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(g_ueNetDev);

    for (uint32_t u = 0; u < g_ueNodes.GetN(); ++u) {
        Ptr<Node> ueNode = g_ueNodes.Get(u);
        Ptr<Ipv4StaticRouting> ueStaticRouting = 
            ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
        ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
    }
    std::cout << "✅ Internet stack configured\n" << std::endl;

    // Attach UEs (simple mmWave-only attach)
    std::cout << "🔧 Attaching UEs..." << std::endl;
    g_mmwaveHelper->AttachToClosestEnb(g_ueNetDev, g_enbNetDev);
    
    for (uint32_t u = 0; u < g_ueNetDev.GetN(); ++u) {
        Ptr<MmWaveUeNetDevice> ueDev = DynamicCast<MmWaveUeNetDevice>(g_ueNetDev.Get(u));
        if (ueDev) {
            uint64_t imsi = ueDev->GetImsi();
            std::cout << "   IMSI:" << imsi << " attached to closest eNB" << std::endl;
        }
    }
    std::cout << "✅ All UEs attached\n" << std::endl;

    // Applications
    std::cout << "🔧 Installing applications..." << std::endl;
    ApplicationContainer sinkApp, clientApp;
    
    for (uint32_t u = 0; u < g_ueNodes.GetN(); ++u) {
        PacketSinkHelper dlPacketSinkHelper("ns3::UdpSocketFactory",
                                           InetSocketAddress(Ipv4Address::GetAny(), 1234));
        sinkApp.Add(dlPacketSinkHelper.Install(g_ueNodes.Get(u)));
        
        UdpClientHelper dlClient(ueIpIface.GetAddress(u), 1234);
        dlClient.SetAttribute("Interval", TimeValue(MicroSeconds(500)));
        dlClient.SetAttribute("MaxPackets", UintegerValue(UINT32_MAX));
        dlClient.SetAttribute("PacketSize", UintegerValue(1280));
        clientApp.Add(dlClient.Install(remoteHost));
    }
    
    sinkApp.Start(Seconds(3.0));
    sinkApp.Stop(Seconds(simTime - 0.1));
    clientApp.Start(Seconds(3.5));
    clientApp.Stop(Seconds(simTime - 0.1));
    std::cout << "✅ Applications configured\n" << std::endl;

    // Setup AI Training Data CSV
    std::cout << "🔧 Setting up AI training data logging..." << std::endl;
    std::ofstream aiDataLog("ai_training_data_baseline.csv");
    g_aiDataLog = &aiDataLog;
    
    aiDataLog << "SessionID,ElapsedTime,Node,CellID,Level,SNR,Qual,CQI,"
              << "SecondCell_RSRP,SecondCell_SNR,NRxLev,NQual,Speed,"
              << "DL_bitrate,UL_bitrate,BANDWIDTH,Bearing,RSRQ,RSRP\n";
    std::cout << "✅ AI training CSV created\n" << std::endl;

    // Setup handover log
    std::cout << "🔧 Setting up handover logging..." << std::endl;
    std::ofstream hoLog("handover-log-baseline.txt");
    hoLog << "========================================\n";
    hoLog << "  NR HANDOVER - BASELINE (ns-3.42)\n";
    hoLog << "========================================\n";
    hoLog << "Simulation: " << simTime << "s\n";
    hoLog << "UEs: " << (int)nUeNodes << " | eNBs: " << (int)nEnbNodes << "\n";
    hoLog << "UE Speed: " << ueSpeed << " m/s\n";
    hoLog << "🎯 Threshold: 10dB\n";
    hoLog << "🎯 Cooldown: 2.5s\n";
    hoLog << "========================================\n\n";
    g_hoLog = &hoLog;
    std::cout << "✅ Handover logging configured\n" << std::endl;

    // Connect callbacks
    Config::Connect("/NodeList/*/ApplicationList/*/$ns3::UdpServer/Rx", 
                   MakeCallback(&RxCallback));

    // Schedule periodic measurements
    for (double t = 0.5; t < simTime; t += 0.24) {
        Simulator::Schedule(Seconds(t), &CollectMeasurements);
    }

    // Enable traces
    g_mmwaveHelper->EnableTraces();
    std::cout << "✅ Traces configured\n" << std::endl;

    // Run simulation
    std::cout << "\n🚀 Starting " << simTime << "s simulation!\n";
    std::cout << "💪 " << (int)nUeNodes << " UEs with " << ueSpeed << " m/s average speed\n";
    std::cout << "✅ AI Training Data: 19 features per measurement\n";
    std::cout << "🎯 BASELINE: Expecting mixed results\n";
    std::cout << std::string(70, '-') << "\n" << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    std::cout << "\n" << std::string(70, '-') << std::endl;

    // Statistics
    uint32_t totalAttempts = g_totalSuccess + g_totalFailures;
    double successRate = totalAttempts > 0 ? (g_totalSuccess * 100.0 / totalAttempts) : 0.0;
    double failureRate = totalAttempts > 0 ? (g_totalFailures * 100.0 / totalAttempts) : 0.0;
    double pingPongRatio = g_totalSuccess > 0 ? (g_totalPingPongs * 1.0 / g_totalSuccess) : 0.0;
    
    hoLog << "\n========================================\n";
    hoLog << "SIMULATION COMPLETE - BASELINE SCENARIO\n";
    hoLog << "========================================\n";
    hoLog << "Total Handover Attempts: " << totalAttempts << "\n";
    hoLog << "✅ Successful: " << g_totalSuccess << " (" << std::fixed << std::setprecision(1) 
          << successRate << "%)\n";
    hoLog << "❌ Failed: " << g_totalFailures << " (" << failureRate << "%)\n";
    hoLog << "⚠️ Ping-Pongs: " << g_totalPingPongs << "\n";
    hoLog << "\n📊 Handover Quality Metrics:\n";
    hoLog << "Success Rate: " << successRate << "%\n";
    hoLog << "Failure Rate: " << failureRate << "%\n";
    hoLog << "Ping-Pong Ratio: " << std::setprecision(2) << pingPongRatio << "\n";
    hoLog << "\nPer-UE Statistics:\n";
    
    for (uint32_t u = 0; u < g_ueNetDev.GetN(); ++u) {
        Ptr<MmWaveUeNetDevice> ueDev = DynamicCast<MmWaveUeNetDevice>(g_ueNetDev.Get(u));
        if (ueDev) {
            uint64_t imsi = ueDev->GetImsi();
            hoLog << "IMSI:" << imsi 
                  << " | Success:" << g_successfulHO[imsi]
                  << " | Failures:" << g_hoFailureCount[imsi]
                  << " | Ping-Pongs:" << g_pingPongCount[imsi] << "\n";
        }
    }
    
    hoLog << "\n🎯 BASELINE for AI training\n";
    hoLog << "========================================\n";
    hoLog.close();
    aiDataLog.close();

    // Console summary
    std::cout << "\n🎯 **BASELINE SIMULATION COMPLETED!**\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << "📊 HANDOVER QUALITY METRICS:\n";
    std::cout << "   Total Attempts: " << totalAttempts << "\n";
    std::cout << "   ✅ Successful: " << g_totalSuccess << " (" << std::fixed << std::setprecision(1) 
              << successRate << "%)\n";
    std::cout << "   ❌ Failed: " << g_totalFailures << " (" << failureRate << "%)\n";
    std::cout << "   ⚠️ Ping-Pongs: " << g_totalPingPongs << "\n";
    std::cout << std::string(70, '=') << "\n";
    
    std::cout << "\n📊 Generated Files:\n";
    std::cout << "   ✅ ai_training_data_baseline.csv\n";
    std::cout << "   ✅ handover-log-baseline.txt\n";
    std::cout << "   ✅ rsrp-measurements.txt\n";
    
    std::cout << "\n🎓 READY FOR AI MODEL TRAINING! 🚀\n";
    std::cout << std::string(70, '=') << "\n" << std::endl;

    Simulator::Destroy();
    return 0;
}
