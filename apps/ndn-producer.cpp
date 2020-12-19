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

#include "ndn-producer.hpp"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/random-variable-stream.h"
#include "model/ndn-l3-protocol.hpp"
#include "helper/ndn-fib-helper.hpp"
#include <ndn-cxx/lp/tags.hpp>
#include <memory>
#include <string.h>

#include <ns3/node-list.h>
#include <ns3/node.h>
#include "ns3/mobility-model.h"
#include "ns3/core-module.h"
NS_LOG_COMPONENT_DEFINE("ndn.Producer");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(Producer);

TypeId
Producer::GetTypeId(void)
{
  static TypeId tid =
    TypeId("ns3::ndn::Producer")
      .SetGroupName("Ndn")
      .SetParent<App>()
      .AddConstructor<Producer>()
      .AddAttribute("Prefix", "Prefix, for which producer has the data", StringValue("/"),
                    MakeNameAccessor(&Producer::m_prefix), MakeNameChecker())
      .AddAttribute(
         "Postfix",
         "Postfix that is added to the output data (e.g., for adding producer-uniqueness)",
         StringValue("/"), MakeNameAccessor(&Producer::m_postfix), MakeNameChecker())
      .AddAttribute("PayloadSize", "Virtual payload size for Content packets", UintegerValue(1024),
                    MakeUintegerAccessor(&Producer::m_virtualPayloadSize),
                    MakeUintegerChecker<uint32_t>())
      .AddAttribute("Freshness", "Freshness of data packets, if 0, then unlimited freshness",
                    TimeValue(Seconds(0)), MakeTimeAccessor(&Producer::m_freshness),
                    MakeTimeChecker())
      .AddAttribute(
         "Signature",
         "Fake signature, 0 valid signature (default), other values application-specific",
         UintegerValue(0), MakeUintegerAccessor(&Producer::m_signature),
         MakeUintegerChecker<uint32_t>())
      .AddAttribute("KeyLocator",
                    "Name to be used for key locator.  If root, then key locator is not used",
                    NameValue(), MakeNameAccessor(&Producer::m_keyLocator), MakeNameChecker());
  return tid;
}

Producer::Producer()
{
  NS_LOG_FUNCTION_NOARGS();
}

// inherited from Application base class.
void
Producer::StartApplication()
{
  // Atif-Code 
  std::cout<<"ndn.Producer StartApplication(): Producer application has been started"<<std::endl;
  ScheduleAdvertisementPacket(true);
  NS_LOG_FUNCTION_NOARGS();
  App::StartApplication();

  FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
}

void
Producer::StopApplication()
{
  NS_LOG_FUNCTION_NOARGS();

  Simulator::Cancel(m_sendEvent);

  App::StopApplication();
}

// Atif-Code 
void
Producer::ScheduleAdvertisementPacket(bool isFromApplicationStarted)
{

  if (isFromApplicationStarted) {
    m_sendEvent = Simulator::Schedule(Seconds(0.0), &Producer::PushAdvertisementData, this);
    isFromApplicationStarted = false;
  }
  else if (!m_sendEvent.IsRunning())
    m_sendEvent = Simulator::Schedule((Seconds(1.0 / 100)),
                                      &Producer::PushAdvertisementData, this);
}


void
Producer::OnInterest(shared_ptr<const Interest> interest)
{
  App::OnInterest(interest); // tracing inside
  NS_LOG_FUNCTION(this << interest);

  if (!m_active)
    return;

  Name dataName(interest->getName());

  auto data = make_shared<Data>();
  data->setName(dataName);
 // std::cout<<"ndn.Producer onData()  InterestName: "<<data->getName().toUri()<<std::endl;
  data->setFreshnessPeriod(::ndn::time::milliseconds(m_freshness.GetMilliSeconds()));

  data->setContent(make_shared< ::ndn::Buffer>(m_virtualPayloadSize));

//Atif-Code:  setting geo tag
  std::tuple<double,double,double> pos={29.81,71.32,36.123};
  lp::GeoTag geoTag(pos);
  data->setTag<lp::GeoTag>(std::make_shared<lp::GeoTag>(geoTag));


  std::shared_ptr<lp::GeoTag> tag = data->getTag<lp::GeoTag>();
  std::tuple<double, double, double> location=tag->getPos();
  // std::cout<<"ndn.producer onInterest()  Geo tag x location: "<<std::get<0>(location)<<std::endl;
  // std::cout<<"ndn.producer onInterest()  Geo tag y location: "<<std::get<1>(location)<<std::endl;
  // std::cout<<"ndn.producer onInterest()  Geo tag z location: "<<std::get<2>(location)<<std::endl;
  
//Atif-Code: setting geo tag end 


  Signature signature;
  SignatureInfo signatureInfo(static_cast< ::ndn::tlv::SignatureTypeValue>(255));

  if (m_keyLocator.size() > 0) {
    signatureInfo.setKeyLocator(m_keyLocator);
  }

  signature.setInfo(signatureInfo);
  signature.setValue(::ndn::makeNonNegativeIntegerBlock(::ndn::tlv::SignatureValue, m_signature));

  data->setSignature(signature);

  NS_LOG_INFO("node(" << GetNode()->GetId() << ") responding with Data: " << data->getName());

  // to create real wire encoding
  data->wireEncode();

  m_transmittedDatas(data, this, m_face);
  m_appLink->onReceiveData(*data);
  ScheduleAdvertisementPacket(false);
}

// Atif-Code: 

void
Producer::PushAdvertisementData(){

  shared_ptr<Name> name = GetDENMDataName();
  shared_ptr<Interest> interest = make_shared<Interest>();
  interest->setName(*name);
  interest->setCanBePrefix(false);

  OnInterest(interest);

}

shared_ptr<Name>
Producer::GetDENMDataName(){
  // We have to set content type, application type, current time, current location of producer 
  // application type: 0,1,2,3
  // content type: 0,1,2,3
  // current in string format
  // curren location (x,y) in string format

  Ptr<UniformRandomVariable> m_rand(CreateObject<UniformRandomVariable>());
  std::string applicationType= std::to_string(std::ceil(m_rand->GetValue(0, 3)));
  std::string contentType=std::to_string(std::ceil(m_rand->GetValue(0, 3)));
  std::string currentLocation=CurrentNodeLocation();
  std::string currentTime= std::to_string(CurrentTime());
  
  std::string nameString="/denm/"+applicationType+"/"+contentType+"/"+currentLocation+"/"+currentTime+"/";
  
  //std::cout<<"ndn.Producer GetDENMDataName(): name value: "<<nameString<<std::endl;

  shared_ptr<Name> name = make_shared<Name>(nameString);

  return name;
}
std::string
Producer::CurrentNodeLocation()
{
    std::string currentLocation;
    if (ns3::Simulator::GetContext() < 1000000) {

        ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
        uint32_t nodeId = node->GetId();
        //std::cout<<"ndn.Forwarder getCurrentNodeLocation(): node-id:  "<<nodeId<<std::endl;
        ns3::Ptr<ns3::MobilityModel> mobility = node->GetObject<ns3::MobilityModel>();
        if (mobility!=nullptr)
        {
          currentLocation = std::to_string(mobility->GetPosition().x)+"-"+std::to_string(mobility->GetPosition().y)
          +"-"+std::to_string(mobility->GetPosition().z);
        }
        else
        {
          std::cout<<"ndn.Producer getCurrentNodeLocation(): mobility return nullptr."<<std::endl;
        }
   }
   return currentLocation;
}
int
Producer::CurrentTime()
{
ndn::time::steady_clock::TimePoint now = ::ndn::time::steady_clock::now(); 
ndn::time::milliseconds milliseconds = ::ndn::time::duration_cast<::ndn::time::milliseconds>(now.time_since_epoch());
return milliseconds.count(); 
}

// Atif-Code: End
} // namespace ndn
} // namespace ns3
