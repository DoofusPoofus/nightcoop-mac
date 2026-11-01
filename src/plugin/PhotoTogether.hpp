/*
 photo together the first cut of nightcoops PhotoTogether (209; the script half is NightCoopPhoto in
 scripts/NightCoopMac/NightCoopPhoto.reds)
 evry 500 ms we look if our photo mode is open. when that changes and a partner is in our world (or we are in the hosts) it
 goes to evryone as PhotoTogether (u8 1 open 0 closed). a partner opening photo mode in our world shows a hint here
 not done yet: opening our photo mode on their word (no script way found) and the photo pose doubles (type 226)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::photo_together
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool sameWorld, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::string& nickname, const std::vector<uint8_t>& message, bool sameWorld, const LogFunction& log);
}
