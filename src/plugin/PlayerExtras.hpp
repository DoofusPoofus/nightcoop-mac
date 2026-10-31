/*
 player extras one door in Main for the smaller nightcoop modules: bodycarry (PlayerCarry 92 BodyCarry) teambuild
 (BuildStats 141 TeamBuild) and the parse and drop readers for evry other game message nobody reads yet (MessageDrops)
 Main hands it evry game message no other routing branch took and ticks it once a frame
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class NpcCopies;
class NpcShare;
class RemoteBodies;
namespace net
{
class RelaySession;
}
}

namespace nightcoop_mac::player_extras
{
using LogFunction = std::function<void(const std::string&)>;

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, const LogFunction& log);
void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, RemoteBodies& bodies, NpcCopies& copies, NpcShare& ourNpcs, const LogFunction& log);
}
