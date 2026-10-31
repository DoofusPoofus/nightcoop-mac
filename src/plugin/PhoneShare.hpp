/*
 phone share the first cut of nightcoops phonesync (PhoneSync 133; the script half is NightCoopPhone in
 scripts/NightCoopMac/NightCoopPhone.reds)
 host with partners in its world: evry call phase of its phone (ring / start / end of quest calls and its own dials) goes to
 evryone as a commit (commit number + call id that stays the same while the contact and who calls stay the same)
 guest in the hosts world (or phone any_world in session.txt for tests): its own quest calls are swallowed and the hosts
 commits get played on its phone. a call the guest dials goes to the host as a request and the host plays it which makes it
 a commit for evryone. commits seen before are skipped
 not done yet: answering / rejecting a ringing call (action 2) refusals and the call state after a load
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

namespace nightcoop_mac::phone_share
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool weAreHost, bool partnerInOurWorld, bool guestShares, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool guestShares, net::RelaySession& session, const LogFunction& log);
}
