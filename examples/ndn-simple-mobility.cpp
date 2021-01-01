/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"

#include "ns3/ndnSIM-module.h"

using namespace std;
namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ndn.WifiExample");

int _mobileNdesCount=4; 

// Graphs for DENM, HNDN, NDN

// Scenario 1: Nodes:            10,20,30,40,50 (dyanmic)
// Scenario 1: Avergae Velocity: 100 m/s 
// Scenario 1: Advertisement Fre: 10 pkt/s 
// generates graph 1, Scenario 2: 20

// Scenario 2: Nodes:            20 
// Scenario 2: Avergae Velocity:  25,50,75,100,125 (dyanmic) 
// Scenario 2: Advertisement Fre: 10 pkt/s 
// generates graph 2,

// Scenario 3: Nodes:            20 
// Scenario 3: Avergae Velocity:  100 m/s (dyanmic) 
// Scenario 2: Advertisement Fre: 10,20,30,40,50 pkt/s 
// generates graph 3


//Scenario 3: Number of node 20 
// Advertisment Packet Tranmission Frequency at accdiedental Vehicle: 2,4,8,10,12 / second 
int _mobileNodesVelocity=50;
int _staticNodesCount=1;
int _revertDirectionTime=12;
int _transmissionInterval=10;

void 
SetStaticMobilityModel(NodeContainer nodes){
  MobilityHelper mobile; 
  mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobile.Install(nodes);
  Ptr<ConstantVelocityMobilityModel> cvmm1 = nodes.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
  Vector pos1 (375, 500, 0);
  Vector vel1 (0, 0, 0);
  cvmm1->SetPosition(pos1);
  cvmm1->SetVelocity(vel1);
}

void 
SetVelocityAndPostion(ns3::Ptr<ns3::Node> node, int xPosition, int yPosition, bool isForwardDirection)
{
  ns3::Ptr<ns3::UniformRandomVariable> m_rand(ns3::CreateObject<ns3::UniformRandomVariable>());

  double random_vertitical_shift_x=m_rand->GetValue(100, 200);
  double random_vertitical_shift_y=m_rand->GetValue(100, 200);
  double random_velocity_shift=m_rand->GetValue(50, 200);

  xPosition = xPosition+random_vertitical_shift_x;
  yPosition = yPosition+random_vertitical_shift_y;

  Ptr<ConstantVelocityMobilityModel> cvmm = node->GetObject<ConstantVelocityMobilityModel> ();
  cvmm->SetPosition(Vector (xPosition, yPosition, 0));
  if(isForwardDirection){
    cvmm->SetVelocity(Vector ((_mobileNodesVelocity+random_velocity_shift), 0, 0));
  }
  else{
    cvmm->SetVelocity(Vector (-(_mobileNodesVelocity+random_velocity_shift), 0, 0));
  }

}
void 
SetMobileNodesMobilityModel(NodeContainer nodes, int defaultXPosition,int defaultXPositionBottom, int defaultYPoistion, bool isForwardDirection){
  MobilityHelper mobile; 
  mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobile.Install(nodes);

  
  

  Ptr<ConstantVelocityMobilityModel> cvmm;
  // mobility for horizantal nodes at bottom
  for (size_t i = 0; i < nodes.GetN()/2; i++)
  {
    SetVelocityAndPostion(nodes.Get(i),defaultXPosition,(defaultYPoistion),!isForwardDirection);
  }
  // mobility for horizantal nodes at top
  for (size_t i = nodes.GetN()/2; i < nodes.GetN(); i++)
  {
    SetVelocityAndPostion(nodes.Get(i),defaultXPositionBottom, (defaultYPoistion*2.5),isForwardDirection);
  }

}
void RevertDirection(NodeContainer nodes, bool revert,int defaultXPosition) 
{

  int defaultXPositionBottom=-1;
  if (defaultXPosition==0)
  {
    defaultXPositionBottom=750;
  }
  else{
    defaultXPositionBottom=0;
  }

    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(nodes);
    if(revert==true)
    {

      SetMobileNodesMobilityModel(nodes,defaultXPosition,defaultXPositionBottom,200,true);
      Simulator::Schedule(Seconds(_revertDirectionTime), &RevertDirection, nodes,false,defaultXPositionBottom);
    }
    else
    {
      SetMobileNodesMobilityModel(nodes,defaultXPosition,defaultXPositionBottom,200,false);
      Simulator::Schedule(Seconds(_revertDirectionTime), &RevertDirection, nodes,true,defaultXPositionBottom);
    }
}

WifiHelper
GetWifiObject(){
  WifiHelper wifi;
  wifi.SetStandard(WIFI_PHY_STANDARD_80211a);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                               StringValue("OfdmRate24Mbps"));
  return wifi;
}

YansWifiPhyHelper
GetPhysicalLayerWifi(){
  
  YansWifiChannelHelper wifiChannel; 
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss("ns3::ThreeLogDistancePropagationLossModel");
  wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");

  YansWifiPhyHelper wifiPhyHelper = YansWifiPhyHelper::Default();
  wifiPhyHelper.SetChannel(wifiChannel.Create());
  wifiPhyHelper.Set("TxPowerStart", DoubleValue(10));
  wifiPhyHelper.Set("TxPowerEnd", DoubleValue(10));

  return wifiPhyHelper;
}
WifiMacHelper
GetMacLayerWifi(){
  WifiMacHelper wifiMacHelper;
  wifiMacHelper.SetType("ns3::AdhocWifiMac");

  return wifiMacHelper;
}

void 
SetNDNStack(NodeContainer nodes){
  NS_LOG_INFO("Installing NDN stack");
  ndn::StackHelper ndnHelper;
  ndnHelper.setPolicy("nfd::cs::lru");
  ndnHelper.setCsSize(1000);
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.Install(nodes);
}
int
main(int argc, char* argv[])
{
  ns3::PacketMetadata::Enable ();
  // disable fragmentation
  Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue("2200"));
  Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("2200"));
  Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
                     StringValue("OfdmRate24Mbps"));
  // reading command line arguments                    
  CommandLine cmd;
  cmd.AddValue("_mobileNdesCount", "Total Number of Mobile Nodes: ", _mobileNdesCount);
  cmd.AddValue("_mobileNodesVelocity", "Mobile Nodes Velocity: ", _mobileNodesVelocity);
    cmd.AddValue("_transmissionInterval", "Advertisement Packet Transmission Frequency: ", _transmissionInterval);
  
  cmd.Parse (argc, argv);

  std::cout<<"Total Number of Mobile Nodes: "<<_mobileNdesCount<<std::endl;
  std::cout<<"Mobile Nodes Velocity: "<<_mobileNodesVelocity<<std::endl;
  std::cout<<"Advertisement Packet Interval in Miliseconds: "<<_transmissionInterval<<std::endl;

  NodeContainer staticNodes;
  staticNodes.Create(_staticNodesCount);
  NodeContainer mobileNodes;
  mobileNodes.Create(_mobileNdesCount);
  


  // 1. Install Wifi
  WifiHelper wifi = GetWifiObject();
  YansWifiPhyHelper wifiPhyHelper=GetPhysicalLayerWifi();
  WifiMacHelper wifiMacHelper=GetMacLayerWifi();
  NetDeviceContainer wifiNetDevices = wifi.Install(wifiPhyHelper, wifiMacHelper, mobileNodes);
                                      wifi.Install(wifiPhyHelper, wifiMacHelper, staticNodes);

  // 2. Install Mobility model
  SetMobileNodesMobilityModel(mobileNodes,0,750,200,false);
  SetStaticMobilityModel(staticNodes);
    

  // 3. Install NDN stack
  SetNDNStack(mobileNodes);
  SetNDNStack(staticNodes);

  // 4. Set broadcast strategy
  ndn::StrategyChoiceHelper::Install(mobileNodes, "/", "/localhost/nfd/strategy/broadcast");
    ndn::StrategyChoiceHelper::Install(staticNodes, "/", "/localhost/nfd/strategy/broadcast");

  // 4. Set up applications
  NS_LOG_INFO("Installing Applications"); 

  // Atif-Code: No need to setup consumer application since we are dealing with the push based communication in which the producer node initiates the communication
  ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
  consumerHelper.Install(mobileNodes);

  ndn::AppHelper producerHelper("ns3::ndn::Producer");
  producerHelper.SetPrefix("/");
  producerHelper.SetAttribute("AdvTransmissionInterval",StringValue(std::to_string(_transmissionInterval)));
  producerHelper.SetAttribute("PayloadSize", StringValue("1200"));
  producerHelper.Install(staticNodes.Get(0));

  // Simulator
  Simulator::Schedule(Seconds(_revertDirectionTime), &RevertDirection, mobileNodes,false,0);
  Simulator::Stop(Seconds(30.0));

  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

} 

int
main(int argc, char* argv[])
{
  return ns3::main(argc, argv);
}