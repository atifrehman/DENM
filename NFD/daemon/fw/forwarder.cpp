/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014-2019,  Regents of the University of California,
 *                           Arizona Board of Regents,
 *                           Colorado State University,
 *                           University Pierre & Marie Curie, Sorbonne University,
 *                           Washington University in St. Louis,
 *                           Beijing Institute of Technology,
 *                           The University of Memphis.
 *
 * This file is part of NFD (Named Data Networking Forwarding Daemon).
 * See AUTHORS.md for complete list of NFD authors and contributors.
 *
 * NFD is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NFD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "forwarder.hpp"
#include "../../../model/ndn-l3-protocol.hpp"
#include "algorithm.hpp"
#include "best-route-strategy2.hpp"
#include "strategy.hpp"
#include "common/global.hpp"
#include "common/logger.hpp"
#include "table/cleanup.hpp"
#include <ndn-cxx/lp/tags.hpp>
#include "face/null-face.hpp"
#include <ns3/node-list.h>
#include <ns3/node.h>
#include "ns3/mobility-model.h"
#include "ns3/core-module.h"
#include "ns3/random-variable-stream.h"
namespace nfd {

NFD_LOG_INIT(Forwarder);

static Name
getDefaultStrategyName()
{
  return fw::BestRouteStrategy2::getStrategyName();
}

Forwarder::Forwarder(FaceTable& faceTable)
  : m_faceTable(faceTable)
  , m_unsolicitedDataPolicy(make_unique<fw::DefaultUnsolicitedDataPolicy>())
  , m_fib(m_nameTree)
  , m_pit(m_nameTree)
  , m_measurements(m_nameTree)
  , m_strategyChoice(*this)
  , m_csFace(face::makeNullFace(FaceUri("contentstore://")))
{
  m_faceTable.addReserved(m_csFace, face::FACEID_CONTENT_STORE);

  m_faceTable.afterAdd.connect([this] (const Face& face) {
    face.afterReceiveInterest.connect(
      [this, &face] (const Interest& interest, const EndpointId& endpointId) {
        this->startProcessInterest(FaceEndpoint(face, endpointId), interest);
      });
    face.afterReceiveData.connect(
      [this, &face] (const Data& data, const EndpointId& endpointId) {
        this->startProcessData(FaceEndpoint(face, endpointId), data);
      });
    face.afterReceiveNack.connect(
      [this, &face] (const lp::Nack& nack, const EndpointId& endpointId) {
        this->startProcessNack(FaceEndpoint(face, endpointId), nack);
      });
    face.onDroppedInterest.connect(
      [this, &face] (const Interest& interest) {
        this->onDroppedInterest(FaceEndpoint(face, 0), interest);
      });
  });

  m_faceTable.beforeRemove.connect([this] (const Face& face) {
    cleanupOnFaceRemoval(m_nameTree, m_fib, m_pit, face);
  });

  m_fib.afterNewNextHop.connect([&] (const Name& prefix, const fib::NextHop& nextHop) {
    this->startProcessNewNextHop(prefix, nextHop);
  });

  m_strategyChoice.setDefaultStrategy(getDefaultStrategyName());
}

Forwarder::~Forwarder() = default;

void
Forwarder::onIncomingInterest(const FaceEndpoint& ingress, const Interest& interest)
{
  //GetCurrentNodeLocation();
  // receive Interest
  NFD_LOG_DEBUG("onIncomingInterest in=" << ingress << " interest=" << interest.getName());
  interest.setTag(make_shared<lp::IncomingFaceIdTag>(ingress.face.getId()));
  ++m_counters.nInInterests;

  // /localhost scope control
  bool isViolatingLocalhost = ingress.face.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL &&
                              scope_prefix::LOCALHOST.isPrefixOf(interest.getName());
  if (isViolatingLocalhost) {
    NFD_LOG_DEBUG("onIncomingInterest in=" << ingress
                  << " interest=" << interest.getName() << " violates /localhost");
    // (drop)
    return;
  }

  // detect duplicate Nonce with Dead Nonce List
  bool hasDuplicateNonceInDnl = m_deadNonceList.has(interest.getName(), interest.getNonce());
  if (hasDuplicateNonceInDnl) {
    // goto Interest loop pipeline
    this->onInterestLoop(ingress, interest);
    return;
  }

  // strip forwarding hint if Interest has reached producer region
  if (!interest.getForwardingHint().empty() &&
      m_networkRegionTable.isInProducerRegion(interest.getForwardingHint())) {
    NFD_LOG_DEBUG("onIncomingInterest in=" << ingress
                  << " interest=" << interest.getName() << " reaching-producer-region");
    const_cast<Interest&>(interest).setForwardingHint({});
  }

  //Atif:Code 
  //getSTValues();

  // PIT insert
  shared_ptr<pit::Entry> pitEntry = m_pit.insert(interest).first;

  // detect duplicate Nonce in PIT entry
  int dnw = fw::findDuplicateNonce(*pitEntry, interest.getNonce(), ingress.face);
  bool hasDuplicateNonceInPit = dnw != fw::DUPLICATE_NONCE_NONE;
  if (ingress.face.getLinkType() == ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    // for p2p face: duplicate Nonce from same incoming face is not loop
    hasDuplicateNonceInPit = hasDuplicateNonceInPit && !(dnw & fw::DUPLICATE_NONCE_IN_SAME);
  }
  if (hasDuplicateNonceInPit) {
    // goto Interest loop pipeline
    this->onInterestLoop(ingress, interest);
    this->dispatchToStrategy(*pitEntry,
      [&] (fw::Strategy& strategy) { strategy.afterReceiveLoopedInterest(ingress, interest, *pitEntry); });
    return;
  }

  // is pending?
  if (!pitEntry->hasInRecords()) {
    m_cs.find(interest,
              bind(&Forwarder::onContentStoreHit, this, ingress, pitEntry, _1, _2),
              bind(&Forwarder::onContentStoreMiss, this, ingress, pitEntry, _1));
  }
  else {
    this->onContentStoreMiss(ingress, pitEntry, interest);
  }
}

void
Forwarder::onInterestLoop(const FaceEndpoint& ingress, const Interest& interest)
{
  // if multi-access or ad hoc face, drop
  if (ingress.face.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    NFD_LOG_DEBUG("onInterestLoop in=" << ingress
                  << " interest=" << interest.getName() << " drop");
    return;
  }

  NFD_LOG_DEBUG("onInterestLoop in=" << ingress << " interest=" << interest.getName()
                << " send-Nack-duplicate");

  // send Nack with reason=DUPLICATE
  // note: Don't enter outgoing Nack pipeline because it needs an in-record.
  lp::Nack nack(interest);
  nack.setReason(lp::NackReason::DUPLICATE);
  ingress.face.sendNack(nack, ingress.endpoint);
}

void
Forwarder::onContentStoreMiss(const FaceEndpoint& ingress,
                              const shared_ptr<pit::Entry>& pitEntry, const Interest& interest)
{
  NFD_LOG_DEBUG("onContentStoreMiss interest=" << interest.getName());
  ++m_counters.nCsMisses;
  afterCsMiss(interest);

  // insert in-record
  pitEntry->insertOrUpdateInRecord(ingress.face, interest);

  // set PIT expiry timer to the time that the last PIT in-record expires
  auto lastExpiring = std::max_element(pitEntry->in_begin(), pitEntry->in_end(),
                                       [] (const auto& a, const auto& b) {
                                         return a.getExpiry() < b.getExpiry();
                                       });
  auto lastExpiryFromNow = lastExpiring->getExpiry() - time::steady_clock::now();
  this->setExpiryTimer(pitEntry, time::duration_cast<time::milliseconds>(lastExpiryFromNow));

  // has NextHopFaceId?
  auto nextHopTag = interest.getTag<lp::NextHopFaceIdTag>();
  if (nextHopTag != nullptr) {
    // chosen NextHop face exists?
    Face* nextHopFace = m_faceTable.get(*nextHopTag);
    if (nextHopFace != nullptr) {
      NFD_LOG_DEBUG("onContentStoreMiss interest=" << interest.getName()
                    << " nexthop-faceid=" << nextHopFace->getId());
      // go to outgoing Interest pipeline
      // scope control is unnecessary, because privileged app explicitly wants to forward
      this->onOutgoingInterest(pitEntry, FaceEndpoint(*nextHopFace, 0), interest);
    }
    return;
  }

  // dispatch to strategy: after incoming Interest
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) {
      strategy.afterReceiveInterest(FaceEndpoint(ingress.face, 0), interest, pitEntry);
    });
}

void
Forwarder::onContentStoreHit(const FaceEndpoint& ingress, const shared_ptr<pit::Entry>& pitEntry,
                             const Interest& interest, const Data& data)
{
  NFD_LOG_DEBUG("onContentStoreHit interest=" << interest.getName());
  ++m_counters.nCsHits;
  afterCsHit(interest, data);

  data.setTag(make_shared<lp::IncomingFaceIdTag>(face::FACEID_CONTENT_STORE));
  // FIXME Should we lookup PIT for other Interests that also match the data?

  pitEntry->isSatisfied = true;
  pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

  // set PIT expiry timer to now
  this->setExpiryTimer(pitEntry, 0_ms);

  beforeSatisfyInterest(*pitEntry, *m_csFace, data);
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) { strategy.beforeSatisfyInterest(pitEntry, FaceEndpoint(*m_csFace, 0), data); });

  // dispatch to strategy: after Content Store hit
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) { strategy.afterContentStoreHit(pitEntry, ingress, data); });
}

void
Forwarder::onOutgoingInterest(const shared_ptr<pit::Entry>& pitEntry,
                              const FaceEndpoint& egress, const Interest& interest)
{
  NFD_LOG_DEBUG("onOutgoingInterest out=" << egress << " interest=" << pitEntry->getName());

  // insert out-record
  pitEntry->insertOrUpdateOutRecord(egress.face, interest);

  // send Interest
  egress.face.sendInterest(interest, egress.endpoint);
  ++m_counters.nOutInterests;
}

void
Forwarder::onInterestFinalize(const shared_ptr<pit::Entry>& pitEntry)
{
  NFD_LOG_DEBUG("onInterestFinalize interest=" << pitEntry->getName()
                << (pitEntry->isSatisfied ? " satisfied" : " unsatisfied"));

  if (!pitEntry->isSatisfied) {
    beforeExpirePendingInterest(*pitEntry);
  }

  // Dead Nonce List insert if necessary
  this->insertDeadNonceList(*pitEntry, nullptr);

  // Increment satisfied/unsatisfied Interests counter
  if (pitEntry->isSatisfied) {
    ++m_counters.nSatisfiedInterests;
  }
  else {
    ++m_counters.nUnsatisfiedInterests;
  }

  // PIT delete
  pitEntry->expiryTimer.cancel();
  m_pit.erase(pitEntry.get());
}

void
Forwarder::onIncomingData(const FaceEndpoint& ingress, const Data& data)
{
  // std::cout<<"Data Name: "<<data.getName().toUri()<<std::endl;
  std::string data_name=data.getName().toUri();
  int node_id=GetCurrentNode()->GetId();
  int timerValue=0;
  ns3::Ptr<ns3::Node> currentNode = GetCurrentNode();
  if (currentNode->GetId()!=0)
  {
    if (data.getName().toUri().find("denm") != std::string::npos) 
    {

      bool isValidForForwarding=TemporalSpatialValidation(data);

      
      if (isValidForForwarding)
      {
        // duplication check and event removal
        

        auto ev_association=GetEventNameAssociation(node_id,data_name);
        if (ev_association.node_id==-1) // -1 node id represent that no record
        {

          timerValue=GetTimerValue(data);
          n_packet_transmissions++;
          std::cout<<"ndn.Forwarder Total Packet Processed  Node-Id: "<<GetCurrentNode()->GetId()<<" Transmissions: "<<n_packet_transmissions<<std::endl;
        }
        else
        { // data is duplicated, remove association and relevant event 
          std::cout<<"ndn.Forwarder onIncomingData Data is duplicated emove association and relevant event. Node-Id: "<<GetCurrentNode()->GetId()<<std::endl;
          RemoveEventNameAssociation(ev_association.event_id,node_id,data_name);
        }
      }
      else
      {
          return; //drop
      }
    }
  }
  // receive Data Atif-Code:  
  //std::cout<<"ndn.Forwarder onIncomingData()  I have received the data on face type: "<<ingress.face.getLinkType()<<"on node node id: "<<GetCurrentNode()->GetId()<<std::endl;
  
  NFD_LOG_DEBUG("onIncomingData in=" << ingress << " data=" << data.getName());
  data.setTag(make_shared<lp::IncomingFaceIdTag>(ingress.face.getId()));
  ++m_counters.nInData;

  // /localhost scope control
  bool isViolatingLocalhost = ingress.face.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL &&
                              scope_prefix::LOCALHOST.isPrefixOf(data.getName());
  if (isViolatingLocalhost) {
    NFD_LOG_DEBUG("onIncomingData in=" << ingress << " data=" << data.getName() << " violates /localhost");
    // (drop)
    return;
  }

  // PIT match
  pit::DataMatchResult pitMatches = m_pit.findAllDataMatches(data);
  if (pitMatches.size() == 0) {
    // goto Data unsolicited pipeline
    this->onDataUnsolicited(ingress, data);

    // Atif-Code Forwarding unsolicited Data on the ingress face (adhoc)

    if (ns3::Simulator::GetContext() < 1000) // this check prevents 4535459 value of Context, I am not sure why it returns 4535459
    {
      ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext()); // Get Current node from simulator context
      ns3::Ptr<ns3::ndn::L3Protocol> l3Object = node->GetObject<ns3::ndn::L3Protocol>(); // Getting l3 Object

      //std::cout << "Node Id :" << node->GetId() << std::endl;

      // iteratation over face table
      for (auto & face : l3Object->getForwarder()->m_faceTable)
      {

        // std::cout << "Node Id: "<<GetCurrentNode()->GetId()<<" Face Id :" << face.getId() 
        // <<"Face count: " <<l3Object->getForwarder()->m_faceTable.size()<< std::endl;
        if(face.getId()!=258){
         //this->onOutgoingData(data, FaceEndpoint(face,face.getId()));
          ns3::Time time = ns3::MilliSeconds (timerValue);
          ns3::EventId eventId = ns3::Simulator::Schedule (time, &Forwarder::onOutgoingData, this, data, FaceEndpoint(face,face.getId()));
          
         SetEventNameAssociation(eventId,node_id,data_name,timerValue);
        }

      }
    }



    return;
  }

  // CS insert
  m_cs.insert(data);

  // when only one PIT entry is matched, trigger strategy: after receive Data
  if (pitMatches.size() == 1) {
    auto& pitEntry = pitMatches.front();

    NFD_LOG_DEBUG("onIncomingData matching=" << pitEntry->getName());

    // set PIT expiry timer to now
    this->setExpiryTimer(pitEntry, 0_ms);

    beforeSatisfyInterest(*pitEntry, ingress.face, data);
    // trigger strategy: after receive Data
    this->dispatchToStrategy(*pitEntry,
      [&] (fw::Strategy& strategy) { strategy.afterReceiveData(pitEntry, ingress, data); });

    // mark PIT satisfied
    pitEntry->isSatisfied = true;
    pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

    // Dead Nonce List insert if necessary (for out-record of inFace)
    this->insertDeadNonceList(*pitEntry, &ingress.face);

    // delete PIT entry's out-record
    pitEntry->deleteOutRecord(ingress.face);
  }
  // when more than one PIT entry is matched, trigger strategy: before satisfy Interest,
  // and send Data to all matched out faces
  else {
    std::set<std::pair<Face*, EndpointId>> pendingDownstreams;
    auto now = time::steady_clock::now();

    for (const auto& pitEntry : pitMatches) {
      NFD_LOG_DEBUG("onIncomingData matching=" << pitEntry->getName());

      // remember pending downstreams
      for (const pit::InRecord& inRecord : pitEntry->getInRecords()) {
        if (inRecord.getExpiry() > now) {
          pendingDownstreams.emplace(&inRecord.getFace(), 0);
        }
      }

      // set PIT expiry timer to now
      this->setExpiryTimer(pitEntry, 0_ms);

      // invoke PIT satisfy callback
      beforeSatisfyInterest(*pitEntry, ingress.face, data);
      this->dispatchToStrategy(*pitEntry,
        [&] (fw::Strategy& strategy) { strategy.beforeSatisfyInterest(pitEntry, ingress, data); });

      // mark PIT satisfied
      pitEntry->isSatisfied = true;
      pitEntry->dataFreshnessPeriod = data.getFreshnessPeriod();

      // Dead Nonce List insert if necessary (for out-record of inFace)
      this->insertDeadNonceList(*pitEntry, &ingress.face);

      // clear PIT entry's in and out records
      pitEntry->clearInRecords();
      pitEntry->deleteOutRecord(ingress.face);
    }

    // foreach pending downstream
    for (const auto& pendingDownstream : pendingDownstreams) {
      if (pendingDownstream.first->getId() == ingress.face.getId() &&
          pendingDownstream.second == ingress.endpoint &&
          pendingDownstream.first->getLinkType() != ndn::nfd::LINK_TYPE_AD_HOC) {
        continue;
      }
      // goto outgoing Data pipeline
      this->onOutgoingData(data, FaceEndpoint(*pendingDownstream.first, pendingDownstream.second));
    }
  }
}

void
Forwarder::onDataUnsolicited(const FaceEndpoint& ingress, const Data& data)
{
  // Atif-Code 
 // std::cout<<"ndn.Forwarder onDataunsolicted() unsolicted data received named: "<<data.getName()<<" node-id: "<<GetCurrentNode()->GetId()<<std::endl;
  
  // accept to cache?
  fw::UnsolicitedDataDecision decision = m_unsolicitedDataPolicy->decide(ingress.face, data);
  if (decision == fw::UnsolicitedDataDecision::CACHE) {
    // CS insert
    m_cs.insert(data, true);
  }

  NFD_LOG_DEBUG("onDataUnsolicited in=" << ingress << " data=" << data.getName() << " decision=" << decision);
}

void
Forwarder::onOutgoingData(const Data& data, const FaceEndpoint& egress)
{
   // Atif-Code: 
  //std::cout<<"ndn.Forwarder onOutgoingData()  I am validating for the forwarding of the Data: Link Type:"<<egress.face.getLinkType()<<std::endl;
  if (egress.face.getId() == face::INVALID_FACEID) {
    NFD_LOG_WARN("onOutgoingData out=(invalid) data=" << data.getName());
    return;
  }
  NFD_LOG_DEBUG("onOutgoingData out=" << egress << " data=" << data.getName());

  // /localhost scope control
  bool isViolatingLocalhost = egress.face.getScope() == ndn::nfd::FACE_SCOPE_NON_LOCAL &&
                              scope_prefix::LOCALHOST.isPrefixOf(data.getName());
  if (isViolatingLocalhost) {
    NFD_LOG_DEBUG("onOutgoingData out=" << egress << " data=" << data.getName() << " violates /localhost");
    // (drop)
    return;
  }

  // Atif-Code:
  //std::cout<<"ndn.Forwarder onOutgoingData()  I am forwarding the Data"<<std::endl;
  // TODO traffic manager

  // send Data
  egress.face.sendData(data, egress.endpoint);
  ++m_counters.nOutData;
}

void
Forwarder::onIncomingNack(const FaceEndpoint& ingress, const lp::Nack& nack)
{
  // receive Nack
  nack.setTag(make_shared<lp::IncomingFaceIdTag>(ingress.face.getId()));
  ++m_counters.nInNacks;

  // if multi-access or ad hoc face, drop
  if (ingress.face.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    NFD_LOG_DEBUG("onIncomingNack in=" << ingress
                  << " nack=" << nack.getInterest().getName() << "~" << nack.getReason()
                  << " link-type=" << ingress.face.getLinkType());
    return;
  }

  // PIT match
  shared_ptr<pit::Entry> pitEntry = m_pit.find(nack.getInterest());
  // if no PIT entry found, drop
  if (pitEntry == nullptr) {
    NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                  << "~" << nack.getReason() << " no-PIT-entry");
    return;
  }

  // has out-record?
  auto outRecord = pitEntry->getOutRecord(ingress.face);
  // if no out-record found, drop
  if (outRecord == pitEntry->out_end()) {
    NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                  << "~" << nack.getReason() << " no-out-record");
    return;
  }

  // if out-record has different Nonce, drop
  if (nack.getInterest().getNonce() != outRecord->getLastNonce()) {
    NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                  << "~" << nack.getReason() << " wrong-Nonce " << nack.getInterest().getNonce()
                  << "!=" << outRecord->getLastNonce());
    return;
  }

  NFD_LOG_DEBUG("onIncomingNack in=" << ingress << " nack=" << nack.getInterest().getName()
                << "~" << nack.getReason() << " OK");

  // record Nack on out-record
  outRecord->setIncomingNack(nack);

  // set PIT expiry timer to now when all out-record receive Nack
  if (!fw::hasPendingOutRecords(*pitEntry)) {
    this->setExpiryTimer(pitEntry, 0_ms);
  }

  // trigger strategy: after receive NACK
  this->dispatchToStrategy(*pitEntry,
    [&] (fw::Strategy& strategy) { strategy.afterReceiveNack(ingress, nack, pitEntry); });
}

void
Forwarder::onOutgoingNack(const shared_ptr<pit::Entry>& pitEntry,
                          const FaceEndpoint& egress, const lp::NackHeader& nack)
{
  if (egress.face.getId() == face::INVALID_FACEID) {
    NFD_LOG_WARN("onOutgoingNack out=(invalid)"
                 << " nack=" << pitEntry->getInterest().getName() << "~" << nack.getReason());
    return;
  }

  // has in-record?
  auto inRecord = pitEntry->getInRecord(egress.face);

  // if no in-record found, drop
  if (inRecord == pitEntry->in_end()) {
    NFD_LOG_DEBUG("onOutgoingNack out=" << egress
                  << " nack=" << pitEntry->getInterest().getName()
                  << "~" << nack.getReason() << " no-in-record");
    return;
  }

  // if multi-access or ad hoc face, drop
  if (egress.face.getLinkType() != ndn::nfd::LINK_TYPE_POINT_TO_POINT) {
    NFD_LOG_DEBUG("onOutgoingNack out=" << egress
                  << " nack=" << pitEntry->getInterest().getName() << "~" << nack.getReason()
                  << " link-type=" << egress.face.getLinkType());
    return;
  }

  NFD_LOG_DEBUG("onOutgoingNack out=" << egress
                << " nack=" << pitEntry->getInterest().getName()
                << "~" << nack.getReason() << " OK");

  // create Nack packet with the Interest from in-record
  lp::Nack nackPkt(inRecord->getInterest());
  nackPkt.setHeader(nack);

  // erase in-record
  pitEntry->deleteInRecord(egress.face);

  // send Nack on face
  egress.face.sendNack(nackPkt, egress.endpoint);
  ++m_counters.nOutNacks;
}

void
Forwarder::onDroppedInterest(const FaceEndpoint& egress, const Interest& interest)
{
  m_strategyChoice.findEffectiveStrategy(interest.getName()).onDroppedInterest(egress, interest);
}

void
Forwarder::onNewNextHop(const Name& prefix, const fib::NextHop& nextHop)
{
  const auto affectedEntries = this->getNameTree().partialEnumerate(prefix,
    [&] (const name_tree::Entry& nte) -> std::pair<bool, bool> {
      const fib::Entry* fibEntry = nte.getFibEntry();
      const fw::Strategy* strategy = nullptr;
      if (nte.getStrategyChoiceEntry() != nullptr) {
        strategy = &nte.getStrategyChoiceEntry()->getStrategy();
      }
      // current nte has buffered Interests but no fibEntry (except for the root nte) and the strategy
      // enables new nexthop behavior, we enumerate the current nte and keep visiting its children.
      if (nte.getName().size() == 0 ||
          (strategy != nullptr && strategy->wantNewNextHopTrigger() &&
          fibEntry == nullptr && nte.hasPitEntries())) {
        return {true, true};
      }
      // we don't need the current nte (no pitEntry or strategy doesn't support new nexthop), but
      // if the current nte has no fibEntry, it's still possible that its children are affected by
      // the new nexthop.
      else if (fibEntry == nullptr) {
        return {false, true};
      }
      // if the current nte has a fibEntry, we ignore the current nte and don't visit its
      // children because they are already covered by the current nte's fibEntry.
      else {
        return {false, false};
      }
    });

  for (const auto& nte : affectedEntries) {
    for (const auto& pitEntry : nte.getPitEntries()) {
      this->dispatchToStrategy(*pitEntry,
        [&] (fw::Strategy& strategy) {
          strategy.afterNewNextHop(nextHop, pitEntry);
        });
    }
  }
}

void
Forwarder::setExpiryTimer(const shared_ptr<pit::Entry>& pitEntry, time::milliseconds duration)
{
  BOOST_ASSERT(pitEntry);
  BOOST_ASSERT(duration >= 0_ms);

  pitEntry->expiryTimer.cancel();
  pitEntry->expiryTimer = getScheduler().schedule(duration, [=] { onInterestFinalize(pitEntry); });
}

void
Forwarder::insertDeadNonceList(pit::Entry& pitEntry, Face* upstream)
{
  // need Dead Nonce List insert?
  bool needDnl = true;
  if (pitEntry.isSatisfied) {
    BOOST_ASSERT(pitEntry.dataFreshnessPeriod >= 0_ms);
    needDnl = static_cast<bool>(pitEntry.getInterest().getMustBeFresh()) &&
              pitEntry.dataFreshnessPeriod < m_deadNonceList.getLifetime();
  }

  if (!needDnl) {
    return;
  }

  // Dead Nonce List insert
  if (upstream == nullptr) {
    // insert all outgoing Nonces
    const auto& outRecords = pitEntry.getOutRecords();
    std::for_each(outRecords.begin(), outRecords.end(), [&] (const auto& outRecord) {
      m_deadNonceList.add(pitEntry.getName(), outRecord.getLastNonce());
    });
  }
  else {
    // insert outgoing Nonce of a specific face
    auto outRecord = pitEntry.getOutRecord(*upstream);
    if (outRecord != pitEntry.getOutRecords().end()) {
      m_deadNonceList.add(pitEntry.getName(), outRecord->getLastNonce());
    }
  }
}

void 
Forwarder::SetCurrentNodeLocationInDataPacket(const Data& data){
  std::tuple<double,double,double> currenNodeLocation=CurrentNodeLocation();
  lp::GeoTag geoTag(currenNodeLocation);
  data.setTag<lp::GeoTag>(std::make_shared<lp::GeoTag>(geoTag));
}

int 
Forwarder::GetTimerValue(Data data){

  ns3::Ptr<ns3::UniformRandomVariable> m_rand(ns3::CreateObject<ns3::UniformRandomVariable>());
  std::tuple<double, double, double> current_node_location=CurrentNodeLocation();
  std::tuple<double, double, double>  event_location=GetEventLocationFromDataName(data);
  std::tuple<double, double, double> previous_node_location=GetNodeLocationFromDataPacket(data);

  double distance_previous_node=DistanceCalculate(std::get<0>(previous_node_location),std::get<1>(previous_node_location),std::get<0>(event_location),std::get<0>(event_location));
  double distance_current_node=DistanceCalculate(std::get<0>(current_node_location),std::get<1>(current_node_location),std::get<0>(event_location),std::get<0>(event_location));
  double delay_random=m_rand->GetValue(0, 2);
  double forward_timer=delay_max*((1-(((distance_previous_node)-(distance_current_node))/rr_max)))+delay_random;
  
  std::cout<<"Forwarder::GetTimerValue: Timer Value:  "<<std::ceil(forward_timer)<<std::endl;
  
  return abs(std::ceil(forward_timer));
}

std::tuple<double, double, double> 
Forwarder::GetEventLocationFromDataName(Data data){

  std::vector<std::string> nameComponents= SplitString(data.getName().toUri(),'/');
  std::string eventLocation= nameComponents[3];
  std::vector<std::string> event_location=SplitString(eventLocation,'-');

  std::tuple<double, double, double> location={std::stod(event_location[0]),std::stod(event_location[1]),0};
  return location;
}


std::tuple<double, double, double> 
Forwarder::GetNodeLocationFromDataPacket(Data data){
  std::shared_ptr<lp::GeoTag> tag = data.getTag<lp::GeoTag>();
  std::tuple<double, double, double> location;
   if(tag != nullptr)
   {
    location=tag->getPos();
   }
   return location;
}

bool 
Forwarder::TemporalSpatialValidation(Data data){

  bool isValidationPassed=false;

  std::vector<std::string> nameComponents=SplitString(data.getName().toUri(),'/');
  std::string applicationType= nameComponents[1];
  std::string contentTpe= nameComponents[2];
  std::string eventLocation= nameComponents[3];
  std::string eventTime= nameComponents[4];
  
  Forwarder::STValue stRange=getSingleSTValue(std::stoi(applicationType),std::stoi(contentTpe));
  bool timeValidity=TimeValidity(std::stoi(eventTime),stRange.temporalRange);
  bool spatialValidity=SpatialValidity(eventLocation,stRange.spatialRange);
  bool angleValidity=AngleValidity(eventLocation,100);
  if (timeValidity)
  {
    std::cout<<"Temporal Validity Passed"<<std::endl;
    if (spatialValidity)
    {
       std::cout<<"Spatial Validity Passed"<<std::endl;
      if (angleValidity)
      {
       std::cout<<"Angle Validity Passed"<<std::endl;
       isValidationPassed=true;
        
      }
      
    }
    
  }
  return isValidationPassed;
}


bool
Forwarder::TimeValidity(int eventTime, int timeThresholdByApplicationType){
  int currentTime=CurrentTime();
  std::cout<<"Temporal DIfference: "<<(currentTime-eventTime)<<std::endl;
  if ((currentTime-eventTime)<timeThresholdByApplicationType)
  {
    return true;
  }
  return false;
}

bool
Forwarder::AngleValidity(std::string eventLocation,  int angleThresholdByApplicationType)
{
  std::vector<std::string> eventLocationCollection=SplitString(eventLocation,'-');
  std::tuple<double,double,double> currentNodeLocation=CurrentNodeLocation();
  double angle=abs(AngleCalculate(std::stod(eventLocationCollection[0]),std::stod(eventLocationCollection[1]),std::get<0>(currentNodeLocation),std::get<1>(currentNodeLocation)));
  std::cout<<"Angle Value is: "<<angle<<std::endl;
  if (angle<angleThresholdByApplicationType)
  {
    return true;
  }
  return false;
}

bool
Forwarder::SpatialValidity(std::string eventLocation, int distanceThresholdByApplicationType){
  
  std::vector<std::string> eventLocationCollection=SplitString(eventLocation,'-');
  std::tuple<double,double,double> currentNodeLocation=CurrentNodeLocation();
  
  // std::cout<<"nfd.SpatialValidity() Event Location X: "<<std::stod(eventLocationCollection[0])<<std::endl;
  // std::cout<<"nfd.SpatialValidity() Event Location Y: "<<std::stod(eventLocationCollection[1])<<std::endl;
  // std::cout<<"nfd.SpatialValidity() Node Location X: "<<std::get<0>(currentNodeLocation)<<std::endl;
  // std::cout<<"nfd.SpatialValidity() Node Location Y: "<<std::get<1>(currentNodeLocation)<<std::endl;

  double distance=DistanceCalculate(std::stod(eventLocationCollection[0]),std::stod(eventLocationCollection[1]),std::get<0>(currentNodeLocation),std::get<1>(currentNodeLocation));
  
  std::cout<<"Distance of current node from producer: "<<distance<<std::endl;

  if (distance<distanceThresholdByApplicationType)
  {
    return true;
  }
  return false;
}

double 
Forwarder::DistanceCalculate(double x1, double y1, double x2, double y2)
{
	double x = x1 - x2; //calculating number to square in next step
	double y = y1 - y2;
	double dist;

	dist = pow(x, 2) + pow(y, 2);       //calculating Euclidean distance
	dist = sqrt(dist);                  

	return dist;
}
double 
Forwarder::AngleCalculate(double x1, double y1, double x2, double y2)
{
	double angle=atan2((y2-y1),(x2-x1));            
  angle=(angle*180)/3.14;
	return angle;
}
int
Forwarder::CurrentTime()
{
ndn::time::steady_clock::TimePoint now = ::ndn::time::steady_clock::now(); 
ndn::time::milliseconds milliseconds = ::ndn::time::duration_cast<::ndn::time::milliseconds>(now.time_since_epoch());
return milliseconds.count(); 
}

std::vector<std::string> 
Forwarder::SplitString(std::string stringValue,  char c)
{
	std::string buff{""};
	std::vector<std::string> stringCollection;
	
	for(auto n:stringValue)
	{
		if(n != c) 
      buff+=n; 
    else
		  if(n == c && buff != "") 
      { 
        stringCollection.push_back(buff); 
        buff = ""; 
      }
	}
	if(buff != "") stringCollection.push_back(buff);
	
	return stringCollection;
}


void 
Forwarder::PrintLocations(const Data& data){

  ns3::Ptr<ns3::Node> currentNode= GetCurrentNode();
  if (currentNode->GetId()==0)
  {
    //Atif-Code: Printing current location of the node
    CurrentNodeLocation();

     //Atif-Code: getting geo tag
    std::shared_ptr<lp::GeoTag> tag = data.getTag<lp::GeoTag>();
    if(tag == nullptr)
    {
        std::cout<<"My Custom Tag value is: null"<<std::endl;
    }
    else
    {
      std::tuple<double, double, double> location=tag->getPos();
      std::cout<<"ndn.forwrder PrintLocations()  Geo tag x location: "<<std::get<0>(location)<<"node-id:  "<<GetCurrentNode()->GetId()<<std::endl;
      std::cout<<"ndn.forwrder PrintLocations()  Geo tag y location: "<<std::get<1>(location)<<"node-id:  "<<GetCurrentNode()->GetId()<<std::endl;
      std::cout<<"ndn.forwrder PrintLocations()  Geo tag z location: "<<std::get<2>(location)<<"node-id:  "<<GetCurrentNode()->GetId()<<std::endl;
    }
  }
}

ns3::Ptr<ns3::Node> 
Forwarder::GetCurrentNode()
{
  ns3::Ptr<ns3::Node> currentNode;
  if (ns3::Simulator::GetContext() < 100000) 
  {
        currentNode = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
  }
  return currentNode;
}

std::tuple<double,double,double>
Forwarder::CurrentNodeLocation()
{
    std::tuple<double,double,double> currentLocation;
    if (ns3::Simulator::GetContext() < 1000000) {

        ns3::Ptr<ns3::Node> node = ns3::NodeList::GetNode(ns3::Simulator::GetContext());
        uint32_t nodeId = node->GetId();
        //std::cout<<"ndn.Forwarder getCurrentNodeLocation(): node-id:  "<<nodeId<<std::endl;
        ns3::Ptr<ns3::MobilityModel> mobility = node->GetObject<ns3::MobilityModel>();
        if (mobility!=nullptr)
        {
          // std::cout<<"ndn.Forwarder getCurrentNodeLocation(): x-postion:  "<<mobility->GetPosition().x<<std::endl;
          // std::cout<<"ndn.Forwarder getCurrentNodeLocation(): y-postion:  "<<mobility->GetPosition().y<<std::endl;
          // std::cout<<"ndn.Forwarder getCurrentNodeLocation(): z-postion:  "<<mobility->GetPosition().z<<std::endl;
          currentLocation = {mobility->GetPosition().x,mobility->GetPosition().y,mobility->GetPosition().z};
        }
        else
        {
          std::cout<<"ndn.Forwarder getCurrentNodeLocation(): mobility return nullptr."<<std::endl;
        }
   }
   return currentLocation;
}

Forwarder::STValue
Forwarder::getSingleSTValue(int applicationType, int contentType)
{
 std::vector<Forwarder::STValue> st_valueCollection = getSTValues();
 Forwarder::STValue stValueItem;
 for (std::vector<STValue>::iterator it = st_valueCollection.begin() ; it != st_valueCollection.end(); ++it){
   if (it->appType==applicationType && it->contentType==contentType)
   {
     return *it;
   } 
 }
}

std::vector<Forwarder::STValue>
Forwarder::getSTValues()
{
  //static table to the nodes for scope comparsion 
  std::vector<Forwarder::STValue> st_valueCollection; 

  //st_valueCollection.push_back({appType,contentType, spatialRange, temporalRange});
  st_valueCollection.push_back({0,0, 201, 20});
  st_valueCollection.push_back({0,1, 201, 20});
  st_valueCollection.push_back({0,2, 201, 20});
  st_valueCollection.push_back({0,3, 201, 20});
  
  st_valueCollection.push_back({1,0, 201, 20});
  st_valueCollection.push_back({1,1, 201, 20});
  st_valueCollection.push_back({1,2, 201, 20});
  st_valueCollection.push_back({1,3, 201, 20});

  st_valueCollection.push_back({2,0, 201, 20});
  st_valueCollection.push_back({2,1, 201, 20});
  st_valueCollection.push_back({2,2, 201, 20});
  st_valueCollection.push_back({2,3, 201, 20});

  st_valueCollection.push_back({3,0, 201, 20});
  st_valueCollection.push_back({3,1, 201, 20});
  st_valueCollection.push_back({3,2, 201, 20});
  st_valueCollection.push_back({3,3, 201, 20});

 
  // for (std::vector<Forwarder::STValue>::iterator it=st_valueCollection.begin(); it!=st_valueCollection.end();++it)
  // {
  //   // std::cout<<"ndn.Forwarder getSTValues(): printing static table values."<<std::endl;
  //   // std::cout<<"ndn.Forwarder getSTValues(): printing static table values: appType:"<<it->appType<<std::endl;
  //   // std::cout<<"ndn.Forwarder getSTValues(): printing static table values: contentType."<<it->contentType<<std::endl;
  //   // std::cout<<"ndn.Forwarder getSTValues(): printing static table values: spatialRange."<<it->spatialRange<<std::endl;
  //   // std::cout<<"ndn.Forwarder getSTValues(): printing static table values: temporalRange."<<it->temporalRange<<std::endl;
  // }
  return st_valueCollection;
}

Forwarder::EventNameAssociation
Forwarder::GetEventNameAssociation( int node_id, std::string data_name)
{ 
 ns3::EventId eventId;
 Forwarder::EventNameAssociation empty_en_assocication = {eventId,-1,"",-1};
 for (std::vector<EventNameAssociation>::iterator it = event_name_assoc_collection.begin() ; it != event_name_assoc_collection.end(); ++it)
 {
   if (it->node_id==node_id && it->data_name==data_name)
   {
     empty_en_assocication={it->event_id,it->node_id,it->data_name,it->timer_value};
     break;
   } 
 }
    return empty_en_assocication;
}
void
Forwarder::SetEventNameAssociation(ns3::EventId  event_id, int node_id, std::string data_name,  int timer_value){
  event_name_assoc_collection.push_back({event_id, node_id, data_name,timer_value});
}
void
Forwarder::RemoveEventNameAssociation(ns3::EventId  event_id, int node_id, std::string data_name){

  // Forwarder::EventNameAssociation removedValue=GetEventNameAssociation(node_id,data_name);
  // event_name_assoc_collection.erase(std::remove(event_name_assoc_collection.begin(), event_name_assoc_collection.end(), removedValue), event_name_assoc_collection.end()); 
  ns3::Simulator::Remove(event_id);
}

} // namespace nfd
