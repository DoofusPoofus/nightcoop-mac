/*
 fact words the guest half and the host half of nightcoops guest fact words (QuestItemDeposit 175 / DocumentRead 187 / device
 facts 246 and the ack 52; the script half is NightCoopFactWords in scripts/NightCoopMac/NightCoopFactWords.reds)
 guest in the hosts world: facts table 1 gets listed evry 500 ms. when the script says we dropped quest items read a
 document or used a custom device action the facts that changed in the second after it (not our own facts) go to the host
 as that word (16 facts each with a word number) again evry 3 s until the host acks it (5 tries)
 host: a word from a partner in our world gets acked and its facts get set in our world so the story moves for evryone
 not done yet: the deposit hand over (the host relays 175 and guests drop their copies of the quest items)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::fact_words
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool guestInHostWorld, bool testWord, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool peerInOurWorld, net::RelaySession& session, const LogFunction& log);
}
