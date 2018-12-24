/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/error-model.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include <iostream>

NS_LOG_COMPONENT_DEFINE ("network-term");

using namespace ns3;
using namespace std;

Ptr<PacketSink> sink;                                    /* Pointer to the packet sink application */

int
main(int argc, char *argv[])
{
  uint32_t payloadSize = 400;                            /* Transport layer payload size in bytes. */
  std::string dataRate = "10Gbps";                       /* Application layer datarate. */
  std::string tcpVariant = "TcpNewReno";                 /* TCP variant type. */
  std::string phyRate = "HtMcs7";                        /* Wi-Fi Physical layer bitrate. */
  bool pcapTracing = false;                              /* PCAP Tracing is enabled or not. */
  double error_p = 0.01;                                 /* Packet error rate. */  
//  bool ChecksumEnable = true;

  std::string bandwidth = "10Gbps";                      /* Data rate for P2P links. */  
  std::string delay = "1ns";                             /* Propagation delay.
  */
  bool sack = false;                                     /* Selective ACK is enabled or not. */
  double simulationTime = 100;                           /* Simulation time in seconds. */

  std::string queue_disc_type = "ns3::PfifoFastQueueDisc";
  std::string recovery = "ns3::TcpClassicRecovery";

  std::string errorModelType = "ns3::RateErrorModel";

  // original
  /* Command line argument parser setup. */
  CommandLine cmd;
  cmd.AddValue ("payloadSize", "Payload size in bytes", payloadSize);
  cmd.AddValue ("dataRate", "Application data ate", dataRate);
  cmd.AddValue ("tcpVariant", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
		"TcpLp", tcpVariant);
  cmd.AddValue ("phyRate", "Physical layer bitrate", phyRate);
  cmd.AddValue ("simulationTime", "Simulation time in seconds", simulationTime);
  cmd.AddValue ("pcap", "Enable/disable PCAP Tracing", pcapTracing);
  cmd.AddValue ("error_p", "Packet error rate", error_p);
  cmd.AddValue ("bandwidth", "Bottleneck bandwidth", bandwidth);
  cmd.AddValue ("delay", "Bottleneck delay", delay);
  cmd.AddValue ("queue_disc_type", "Queue disc type for gateway (e.g. ns3::CoDelQueueDisc)", queue_disc_type);
  cmd.AddValue ("sack", "Enable or disable SACK option", sack);
  cmd.AddValue ("recovery", "Recovery algorithm type to use (e.g., ns3::TcpPrrRecovery", recovery);
//  cmd.AddValue("errorModelType", "TypeId of the error model to use", errorModelType);
  cmd.Parse (argc, argv);

  tcpVariant = std::string ("ns3::") + tcpVariant;

  // Select TCP variant
  if (tcpVariant.compare ("ns3::TcpWestwoodPlus") == 0)
    { 
      // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpWestwood::GetTypeId ()));
      // the default protocol type in ns3::TcpWestwood is WESTWOOD
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
    }
  else
    {
      TypeId tcpTid;
      NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (tcpVariant, &tcpTid), "TypeId " << tcpVariant << " not found");
      Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (tcpVariant)));
    }

  /* No fragmentation and no RTS/CTS */
  Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("999999"));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("999999"));

  /* Configure TCP Options */
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (payloadSize));

  // 4 MB of TCP buffer
  Config::SetDefault ("ns3::TcpSocket::RcvBufSize", UintegerValue (1 << 21));
  Config::SetDefault ("ns3::TcpSocket::SndBufSize", UintegerValue (1 << 21));

  /* Set up Wi-Fi */
  WifiMacHelper wifiMac;
  WifiHelper wifiHelper;
  wifiHelper.SetStandard (WIFI_PHY_STANDARD_80211n_5GHZ);

  /* Set up Legacy Channel */
  YansWifiChannelHelper wifiChannel ;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel", "Frequency", DoubleValue (5e9));

  /* Setup Physical Layer */
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();
  wifiPhy.SetChannel (wifiChannel.Create ());
  wifiPhy.Set ("TxPowerStart", DoubleValue (10.0));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (10.0));
  wifiPhy.Set ("TxPowerLevels", UintegerValue (1));
  wifiPhy.Set ("TxGain", DoubleValue (0));
  wifiPhy.Set ("RxGain", DoubleValue (0));
  wifiPhy.Set ("RxNoiseFigure", DoubleValue (10));
  wifiPhy.Set ("CcaMode1Threshold", DoubleValue (-79));
  wifiPhy.Set ("EnergyDetectionThreshold", DoubleValue (-79 + 3));
  wifiPhy.SetErrorRateModel ("ns3::YansErrorRateModel");
  wifiHelper.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                      "DataMode", StringValue (phyRate),
                                      "ControlMode", StringValue ("HtMcs0"));


  /* Configure Server */
  NodeContainer serverNode;
  serverNode.Create (1);

  /* Configure AP */
  NodeContainer apWifiNode;
  apWifiNode.Create (1);

  Ssid ssid = Ssid ("network");
  wifiMac.SetType ("ns3::ApWifiMac",
                    "Ssid", SsidValue (ssid));

  NetDeviceContainer apDevice;
  apDevice = wifiHelper.Install (wifiPhy, wifiMac, apWifiNode);

  /* Configure STA */

  int nWifis = 3;
  NodeContainer staWifiNode;
  staWifiNode.Create (nWifis);

  wifiMac.SetType ("ns3::StaWifiMac",
                    "Ssid", SsidValue (ssid),
                    "ActiveProbing", BooleanValue (false));

  NetDeviceContainer staDevices;
  staDevices = wifiHelper.Install (wifiPhy, wifiMac, staWifiNode);

  /* Mobility model */
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  positionAlloc->Add (Vector (10.0, 10.0, 0.0));
  positionAlloc->Add (Vector (10.0, 0.0, 0.0));
  positionAlloc->Add (Vector (0.0, 10.0, 0.0));
  positionAlloc->Add (Vector (-10.0, 0.0, 0.0));

  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (apWifiNode);
  mobility.Install (staWifiNode);
  mobility.Install (serverNode);

  /* Configure the error model */
  // Here we use BurstErrorModel with packet error rate.
  Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable> ();
  uv->SetStream (100);
  BurstErrorModel error_model;
  error_model.SetRandomVariable (uv);
  error_model.SetRandomBurstSize (uv);
  error_model.SetBurstRate (error_p);

  PointToPointHelper UnReLink;
  UnReLink.SetDeviceAttribute ("DataRate", StringValue (bandwidth));
  UnReLink.SetChannelAttribute ("Delay", StringValue (delay));
  UnReLink.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));

  /* Internet stack */
  InternetStackHelper stack;
  stack.Install (apWifiNode);
  stack.Install (staWifiNode);
  stack.Install (serverNode);

  Ipv4InterfaceContainer sink_interfaces;

  TrafficControlHelper tchPfifo;
  tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");

  TrafficControlHelper tchCoDel;
  tchCoDel.SetRootQueueDisc ("ns3::CoDelQueueDisc");

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");

  NetDeviceContainer infraDevices (staDevices, apDevice);
  tchPfifo.Install (infraDevices);
  address.NewNetwork();

  Ipv4InterfaceContainer interfaces = address.Assign(infraDevices);
  infraDevices = UnReLink.Install (apWifiNode.Get(0), serverNode.Get(0));
  if (queue_disc_type.compare ("ns3::PfifoFastQueueDisc") == 0)
    {
      tchPfifo.Install (infraDevices);
    }
  else if (queue_disc_type.compare ("ns3::CoDelQueueDisc") == 0)
    {
      tchCoDel.Install (infraDevices);
    }
  else
    {
      NS_FATAL_ERROR ("Queue not recognized. Allowed values are ns3::CoDelQueueDisc or ns3::PfifoFastQueueDisc");
    }
  address.NewNetwork();
  interfaces = address.Assign(infraDevices);
  sink_interfaces.Add (interfaces.Get (1));

  /* Populate routing table */
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  /* Install TCP Receiver on the server */
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), 9));
  ApplicationContainer sinkApp = sinkHelper.Install (serverNode);
  sink = StaticCast<PacketSink> (sinkApp.Get (0));

  /* Install TCP/UDP Transmitter on the station */
  AddressValue remoteAddress (InetSocketAddress (sink_interfaces.GetAddress (0, 0), 9));
  BulkSendHelper ftp ("ns3::TcpSocketFactory", Address ());
  ftp.SetAttribute ("Remote", remoteAddress);
  ftp.SetAttribute ("SendSize", UintegerValue (payloadSize));
  ftp.SetAttribute ("MaxBytes", UintegerValue (int(100 * 1000000)));
  ApplicationContainer sourceApp = ftp.Install (staWifiNode);

  /* Start Applications */
  sinkApp.Start (Seconds (0.0));
  sourceApp.Start (Seconds (1.0));


  // Set up tracing if enabled
  if (pcapTracing)
    {
      wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO);
      wifiPhy.EnablePcap ("AccessPoint", apDevice);
      wifiPhy.EnablePcap ("Station", staDevices);
      UnReLink.EnablePcapAll("server", true);
    }

  // Flow monitor
  FlowMonitorHelper flowHelper;
  flowHelper.InstallAll();

  /* Start Simulation */
  Simulator::Stop (Seconds (simulationTime + 1));
  cout << "before Run" << endl; 
  Simulator::Run ();
  cout << "after Run" << endl;
  flowHelper.SerializeToXmlFile ("network-term.flowmonitor", true, true);
   cout << "before Destory" << endl;
  Simulator::Destroy ();
  cout << "after Destory" << endl;

  return 0;
}
