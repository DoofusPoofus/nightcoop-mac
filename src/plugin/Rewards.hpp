/*
 rewards the first cut of nightcoops rewards module (RewardWord 238 kind 1; the script half is NightCoopRewards in
 scripts/NightCoopMac/NightCoopRewards.reds)
 the host with partners in its world sends evry quest reward its player gets to evryone (reward record + serial). a guest in
 the hosts world skips its own quest node rewards and gets the hosts ones instead (rewards any_world in session.txt does that
 in any world for tests). serials already taken from the host are skipped
 not done yet: kind 2 (partner defeat rewards) and holding words while a guest is between worlds
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

namespace nightcoop_mac::rewards
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool weAreHost, bool partnerInOurWorld, bool takeHostRewards, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool takeHostRewards, const LogFunction& log);
}
