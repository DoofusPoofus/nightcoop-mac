/*
 sms replies the first cut of nightcoops smsreply (SmsReply 142; the script half is NightCoopSms in scripts/NightCoopMac/NightCoopSms.reds)
 the host with partners in its world says evry 5 s that a reply chosen by anyone counts for evryone (kind 1 sms_any). a guest in
 the hosts world does not answer a phone chat itself: the reply goes to the host (kind 2) and the host picks it in its journal
 (visited + succeeded) and answers with a verdict (kind 3). the journal sharing that exists brings the answer back to evryone
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

namespace nightcoop_mac::sms_replies
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool weAreHost, bool partnerInOurWorld, bool guestInHostWorld, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool partnerInOurWorld, net::RelaySession& session, const LogFunction& log);
}
