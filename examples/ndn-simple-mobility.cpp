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

int mobileNdesCount=2;
int mobileNodesVelocity=50;
int staticNodesCount=1;
void RevertDirection(NodeContainer nodes, bool revert) //DEBUG purpose
{
    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(nodes);
    if(revert==true)
    {

      // mobility for horizantal nodes at top
      for (size_t i = 0; i < 1; i++)
      {
        Ptr<ConstantVelocityMobilityModel> cvmm = nodes.Get(i)->GetObject<ConstantVelocityMobilityModel> ();
        Vector pos (0, -200, 0);
        Vector vel (mobileNodesVelocity, 0, 0);
        cvmm->SetPosition(pos);
        cvmm->SetVelocity(vel);
      }

      // mobility for horizantal nodes at top
      for (size_t i = 1; i < 2; i++)
      {
        Ptr<ConstantVelocityMobilityModel> cvmm1 = nodes.Get(i)->GetObject<ConstantVelocityMobilityModel> ();
        Vector pos1 (100, -150, 0);
        Vector vel1 (mobileNodesVelocity, 0, 0);
        cvmm1->SetPosition(pos1);
        cvmm1->SetVelocity(vel1);
      }
      Simulator::Schedule(Seconds(12), &RevertDirection, nodes,false);
    }
    else
    {
     // mobility for horizantal nodes at top
      for (size_t i = 0; i < 1; i++)
      {
        Ptr<ConstantVelocityMobilityModel> cvmm = nodes.Get(i)->GetObject<ConstantVelocityMobilityModel> ();
        Vector pos (750, -200, 0);
        Vector vel (-mobileNodesVelocity, 0, 0);
        cvmm->SetPosition(pos);
        cvmm->SetVelocity(vel);
      }
      
      // mobility for horizantal nodes at top
      for (size_t i = 1; i < 2; i++)
      {
        Ptr<ConstantVelocityMobilityModel> cvmm1 = nodes.Get(i)->GetObject<ConstantVelocityMobilityModel> ();
        Vector pos1 (650, -150, 0);
        Vector vel1 (-mobileNodesVelocity, 0, 0);
        cvmm1->SetPosition(pos1);
        cvmm1->SetVelocity(vel1);
      }
      Simulator::Schedule(Seconds(12), &RevertDirection, nodes,true);
    }
}
void 
SetMobileNodesMobilityModel(NodeContainer nodes){
  MobilityHelper mobile; 
  mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobile.Install(nodes);

  // mobility for horizantal nodes at top
  for (size_t i = 0; i < 1; i++)
  {
    Ptr<ConstantVelocityMobilityModel> cvmm = nodes.Get(i)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos (0, -200, 0);
    Vector vel (mobileNodesVelocity, 0, 0);
    cvmm->SetPosition(pos);
    cvmm->SetVelocity(vel);
  }
  
  // mobility for horizantal nodes at top
  for (size_t i = 1; i < 2; i++)
  {
    Ptr<ConstantVelocityMobilityModel> cvmm1 = nodes.Get(i)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos1 (100, -150, 0);
    Vector vel1 (mobileNodesVelocity, 0, 0);
    cvmm1->SetPosition(pos1);
    cvmm1->SetVelocity(vel1);
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
SetStaticMobilityModel(NodeContainer nodes){
  MobilityHelper mobile; 
  mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  mobile.Install(nodes);
  Ptr<ConstantVelocityMobilityModel> cvmm1 = nodes.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
  Vector pos1 (375, 0, 0);
  Vector vel1 (0, 0, 0);
  cvmm1->SetPosition(pos1);
  cvmm1->SetVelocity(vel1);
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
  SetMobileNodesMobilityModel(mobileNodes);
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
  Simulator::Schedule(Seconds(12), &RevertDirection, mobileNodes,false);
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