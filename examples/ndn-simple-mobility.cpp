/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Md Ashiqur Rahman: University of Arizona.
 * Muhammad Atif U Rehman: Hongik University
 *
 **/
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"
#include "ns3/constant-velocity-mobility-model.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
NS_LOG_COMPONENT_DEFINE ("simple-wifi-mobility");

using namespace std;

namespace ns3 {


    void RevertDirection(NodeContainer consumers, bool revert) //DEBUG purpose
    {
    if(revert==true)
    {
    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(consumers);
    ////// Setting each mobile consumer 100m apart from each other
    Ptr<ConstantVelocityMobilityModel> cvmm = consumers.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos (450, 100, 0);
    Vector vel (0, -50, 0); //y axis backward direction
    cvmm->SetPosition(pos);
    cvmm->SetVelocity(vel);
    // consumer 2
    Ptr<ConstantVelocityMobilityModel> cvmm1 = consumers.Get(1)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos1 (450, -300, 0);
    Vector vel1 (0, 50, 0);   //y axis forward direction
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
    Vector pos (450, -300, 0);
    Vector vel (0, 50, 0);
    cvmm->SetPosition(pos);
    cvmm->SetVelocity(vel);
    // consumer 2
    Ptr<ConstantVelocityMobilityModel> cvmm1 = consumers.Get(1)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos1 (450, 100, 0);
    Vector vel1 (0, -50, 0);   
    cvmm1->SetPosition(pos1);
    cvmm1->SetVelocity(vel1);


        Simulator::Schedule(Seconds(12), &RevertDirection, consumers,true);
    }


    }

  int main (int argc, char *argv[])
  {
    std::string phyMode ("DsssRate1Mbps");
    uint32_t wifiSta = 4;

    int accesspoint1 = 2;            // number of AP nodes
    int spacing = 300;            // between bottom-row nodes
    int range = 110;
    double endtime = 20.0;

    string animFile = "ap-mobility-animation.xml";

    CommandLine cmd;
    cmd.AddValue ("animFile", "File Name for Animation Output", animFile);
    cmd.Parse (argc, argv);

    ////// disable fragmentation, RTS/CTS for frames below 2200 bytes and fix non-unicast data rate
    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue("2200"));
    Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("2200"));
    Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue(phyMode));

    ////// The below set of helpers will help us to put together the wifi NICs we want 
    WifiHelper wifi;

    wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
    YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();

    ////// ns-3 supports RadioTap and Prism tracing extensions for 802.11b
    wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannel;

    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");

    ////// the following has an absolute cutoff at distance > range (range == radius)
    wifiChannel.AddPropagationLoss ("ns3::RangePropagationLossModel", 
                                    "MaxRange", DoubleValue(range));
    wifiPhy.SetChannel (wifiChannel.Create ());
    wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue (phyMode),
                                  "ControlMode", StringValue (phyMode));

    ////// Add a non-QoS upper mac of STAs, and disable rate control
    WifiMacHelper wifiMacHelper;
    ////// Active associsation of STA to AP via probing.
    wifiMacHelper.SetType("ns3::AdhocWifiMac");

    NodeContainer mobileNodes;
    mobileNodes.Create(2);

    NetDeviceContainer staDevice = wifi.Install (wifiPhy, wifiMacHelper, mobileNodes);
    NetDeviceContainer devices = staDevice;

    ////// Setup AP.
    WifiMacHelper wifiMac;
    wifiMacHelper.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer apDevice = wifi.Install (wifiPhy, wifiMac, mobileNodes[0]);
    devices.Add (apDevice);


    ////// Setting mobility model and movement parameters for mobile nodes
    ////// ConstantVelocityMobilityModel is a subclass of MobilityModel
    MobilityHelper mobile; 
    mobile.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobile.Install(mobileNodes);
    ////// Setting position and velocity of mobile node 1
    Ptr<ConstantVelocityMobilityModel> cvmm = mobileNodes.Get(0)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos (450, -300, 0);
    Vector vel (0, 25, 0);
    cvmm->SetPosition(pos);
    cvmm->SetVelocity(vel);
     ////// Setting position and velocity of mobile node 2
    Ptr<ConstantVelocityMobilityModel> cvmm1 = mobileNodes.Get(1)->GetObject<ConstantVelocityMobilityModel> ();
    Vector pos1 (400, 100, 0);
    Vector vel1 (0, -25, 0);
    cvmm1->SetPosition(pos1);
    cvmm1->SetVelocity(vel1);

    
    // std::cout << "position: " << cvmm->GetPosition() << " velocity: " << cvmm->GetVelocity() << std::endl;
    // std::cout << "mover mobility model: " << mobile.GetMobilityModelType() << std::endl; // just for confirmation

    // 3. Install NDN stack on all nodes
    NS_LOG_INFO("Installing NDN stack");
    ndn::StackHelper ndnHelper;
    ndnHelper.InstallAll();

    // Choosing forwarding strategy
    ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/broadcast");


    // Installing global routing interface on all nodes
    ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
    ndnGlobalRoutingHelper.InstallAll();

// Atif-Code: No need to setup consumer application since we are dealing with the push based communication in which the producer node initiates the communication

  ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
  // consumerHelper.SetPrefix("/test/prefix");
  // consumerHelper.SetAttribute("Frequency", DoubleValue(10.0));
  consumerHelper.Install(mobileNodes.Get(0));

  ndn::AppHelper producerHelper("ns3::ndn::Producer");
  producerHelper.SetPrefix("/");
  producerHelper.SetAttribute("PayloadSize", StringValue("1200"));
  producerHelper.Install(mobileNodes.Get(1));
    
    

    // Calculate and install FIBs
    ndn::GlobalRoutingHelper::CalculateRoutes();


    Simulator::Stop (Seconds (endtime));

    AnimationInterface anim (animFile);    

    Simulator::Schedule(Seconds(12), &RevertDirection, mobileNodes,true);
    
    Simulator::Run ();
    Simulator::Destroy ();

    return 0;
  }

}

int main(int argc, char* argv[])
{
  return ns3::main(argc, argv);
}