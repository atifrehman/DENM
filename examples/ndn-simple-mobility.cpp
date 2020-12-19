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

void RevertDirection(NodeContainer consumers, bool revert) //DEBUG purpose
    {
    if(revert==true)
    {
    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(consumers);
    ////// Setting each mobile consumer 100m apart from each other
    Ptr<ConstantVelocityMobilityModel> cvmm = consumers.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos (0, -100, 0);
    Vector vel (50, 0, 0); //y axis backward direction
    cvmm->SetPosition(pos);
    cvmm->SetVelocity(vel);
    // consumer 2
    Ptr<ConstantVelocityMobilityModel> cvmm1 = consumers.Get(1)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos1 (300, 0, 0);
    Vector vel1 (0, 0, 0);   //y axis forward direction
    cvmm1->SetPosition(pos1);
    cvmm1->SetVelocity(vel1);

    Simulator::Schedule(Seconds(12), &RevertDirection, consumers,false);
    }
    else
    {
         ////// Setting mobility model and movement parameters for mobile nodes
    ////// ConstantVelocityMobilityModel is a subclass of MobilityModel
    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(consumers);
    ////// Setting each mobile consumer 100m apart from each other
    Ptr<ConstantVelocityMobilityModel> cvmm = consumers.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos (900, 0, 0);
    Vector vel (-50, 0, 0);
    cvmm->SetPosition(pos);
    cvmm->SetVelocity(vel);
    // consumer 2
    Ptr<ConstantVelocityMobilityModel> cvmm1 = consumers.Get(1)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos1 (300, 0, 0);
    Vector vel1 (0, 0, 0);   
    cvmm1->SetPosition(pos1);
    cvmm1->SetVelocity(vel1);


        Simulator::Schedule(Seconds(12), &RevertDirection, consumers,true);
    }


    }
int
main(int argc, char* argv[])
{
  // disable fragmentation
  Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue("2200"));
  Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("2200"));
  Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode",
                     StringValue("OfdmRate24Mbps"));

  CommandLine cmd;
  cmd.Parse(argc, argv);

  //////////////////////
  //////////////////////
  //////////////////////
  WifiHelper wifi;
  // wifi.SetRemoteStationManager ("ns3::AarfWifiManager");
  wifi.SetStandard(WIFI_PHY_STANDARD_80211a);
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                               StringValue("OfdmRate24Mbps"));

  YansWifiChannelHelper wifiChannel; // = YansWifiChannelHelper::Default ();
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss("ns3::ThreeLogDistancePropagationLossModel");
  wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel");

  // YansWifiPhy wifiPhy = YansWifiPhy::Default();
  YansWifiPhyHelper wifiPhyHelper = YansWifiPhyHelper::Default();
  wifiPhyHelper.SetChannel(wifiChannel.Create());
  wifiPhyHelper.Set("TxPowerStart", DoubleValue(10));
  wifiPhyHelper.Set("TxPowerEnd", DoubleValue(10));

  WifiMacHelper wifiMacHelper;
  wifiMacHelper.SetType("ns3::AdhocWifiMac");



  NodeContainer nodes;
  nodes.Create(2);

  ////////////////
  // 1. Install Wifi
  NetDeviceContainer wifiNetDevices = wifi.Install(wifiPhyHelper, wifiMacHelper, nodes);

  // 2. Install Mobility model
 ////// Setting mobility model and movement parameters for mobile nodes
    ////// ConstantVelocityMobilityModel is a subclass of MobilityModel
    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(nodes);
    ////// Setting position and velocity of mobile node 1
    Ptr<ConstantVelocityMobilityModel> cvmm = nodes.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos (0, -100, 0);
    Vector vel (50, 0, 0);
    cvmm->SetPosition(pos);
    cvmm->SetVelocity(vel);
     ////// Setting position and velocity of mobile node 2
    Ptr<ConstantVelocityMobilityModel> cvmm1 = nodes.Get(1)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos1 (425, 0, 0);
    Vector vel1 (0, 0, 0);
    cvmm1->SetPosition(pos1);
    cvmm1->SetVelocity(vel1);

  // 3. Install NDN stack
  NS_LOG_INFO("Installing NDN stack");
  ndn::StackHelper ndnHelper;
  // ndnHelper.AddNetDeviceFaceCreateCallback (WifiNetDevice::GetTypeId (), MakeCallback
  // (MyNetDeviceFaceCallback));
  ndnHelper.setPolicy("nfd::cs::lru");
  ndnHelper.setCsSize(1000);
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.Install(nodes);

  // Set BestRoute strategy
  ndn::StrategyChoiceHelper::Install(nodes, "/", "/localhost/nfd/strategy/best-route");

  // 4. Set up applications
  NS_LOG_INFO("Installing Applications"); 


//
// Atif-Code: No need to setup consumer application since we are dealing with the push based communication in which the producer node initiates the communication

   ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
  // consumerHelper.SetPrefix("/test/prefix");
  // consumerHelper.SetAttribute("Frequency", DoubleValue(10.0));
  consumerHelper.Install(nodes.Get(0));

  ndn::AppHelper producerHelper("ns3::ndn::Producer");
  producerHelper.SetPrefix("/");
  producerHelper.SetAttribute("PayloadSize", StringValue("1200"));
  producerHelper.Install(nodes.Get(1));

  ////////////////
  Simulator::Schedule(Seconds(12), &RevertDirection, nodes,false);
  Simulator::Stop(Seconds(30.0));

  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

} // namespace ns3

int
main(int argc, char* argv[])
{
  return ns3::main(argc, argv);
}