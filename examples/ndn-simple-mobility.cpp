/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * Author: Muhammad Atif Ur Rehman
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

int mobileNdesCount=4;
int mobileNodesVelocity=50;
int staticNodesCount=1;
int revertDirectionTime=12;

void 
SetStaticMobilityModel(NodeContainer nodes){
  MobilityHelper mobile; 
  mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobile.Install(nodes);
  Ptr<ConstantVelocityMobilityModel> cvmm1 = nodes.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
  Vector pos1 (375, 600, 0);
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
    cvmm->SetVelocity(Vector ((mobileNodesVelocity+random_velocity_shift), 0, 0));
  }
  else{
    cvmm->SetVelocity(Vector (-(mobileNodesVelocity+random_velocity_shift), 0, 0));
  }

}
void 
SetMobileNodesMobilityModel(NodeContainer nodes, int defaultXPosition, int defaultYPoistion, bool isForwardDirection){
  MobilityHelper mobile; 
  mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobile.Install(nodes);
  Ptr<ConstantVelocityMobilityModel> cvmm;
  // mobility for horizantal nodes at bottom
  for (size_t i = 0; i < nodes.GetN()/2; i++)
  {
    SetVelocityAndPostion(nodes.Get(i),defaultXPosition,(defaultYPoistion),isForwardDirection);
  }
  // mobility for horizantal nodes at top
  for (size_t i = nodes.GetN()/2; i < nodes.GetN(); i++)
  {
    SetVelocityAndPostion(nodes.Get(i),defaultXPosition, defaultYPoistion*4,isForwardDirection);
  }

}
void RevertDirection(NodeContainer nodes, bool revert) 
{
    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(nodes);
    if(revert==true)
    {

      SetMobileNodesMobilityModel(nodes,0,200,true);
      Simulator::Schedule(Seconds(revertDirectionTime), &RevertDirection, nodes,false);
    }
    else
    {
      SetMobileNodesMobilityModel(nodes,750,200,false);
      Simulator::Schedule(Seconds(revertDirectionTime), &RevertDirection, nodes,true);
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
  cmd.Parse(argc, argv);
  
  NodeContainer staticNodes;
  staticNodes.Create(staticNodesCount);
  NodeContainer mobileNodes;
  mobileNodes.Create(mobileNdesCount);
  


  // 1. Install Wifi
  WifiHelper wifi = GetWifiObject();
  YansWifiPhyHelper wifiPhyHelper=GetPhysicalLayerWifi();
  WifiMacHelper wifiMacHelper=GetMacLayerWifi();
  NetDeviceContainer wifiNetDevices = wifi.Install(wifiPhyHelper, wifiMacHelper, mobileNodes);
                                      wifi.Install(wifiPhyHelper, wifiMacHelper, staticNodes);

  // 2. Install Mobility model
  SetMobileNodesMobilityModel(mobileNodes,0,200,true);
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
  producerHelper.SetAttribute("PayloadSize", StringValue("1200"));
  producerHelper.Install(staticNodes.Get(0));

  // Simulator
  Simulator::Schedule(Seconds(revertDirectionTime), &RevertDirection, mobileNodes,false);
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