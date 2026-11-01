/*
 braindance the first cut of nightcoops braindance (Braindance 137 kind 1; the script half is NightCoopBraindance in
 scripts/NightCoopMac/NightCoopBraindance.reds). the host leads the tape: while it is in a braindance (the scenes rewindable
 section) its tape time length speed and direction go to the partners in its world evry 500 ms. a guest in a braindance in the
 hosts world jumps its tape there when it is more than 1 s off and takes the hosts speed and direction
 not done yet: the guests asks (kind 2) exit votes the time map of stretched sections ghosts (149) and shared clues (150)
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

namespace nightcoop_mac::braindance
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, const std::vector<uint32_t>& peersInOurWorld, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool takeHostTape, const LogFunction& log);
}
