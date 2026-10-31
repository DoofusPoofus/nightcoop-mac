/*
 npc handoffs the receiving half of nightcoops npc handoff (NpcHandoff 26 / NpcHandoffAck 27 / NpcClaim 28)
 a partner offers us one of its shared npcs: when we have a body for it (its stand in or our own npc as its puppet) that body
 gets its own ai back and becomes one of our shared npcs under the same net id (npc share) then we answer accepted with its
 entity id and tell evryone with NpcClaim kind 3. no body means refused (reason 1)
 an accepted answer to our own offer turns our npc into the takers puppet (npc share + npc copies). a claim moves a copy to
 its new owner
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
namespace net
{
class RelaySession;
}
}

namespace nightcoop_mac::npc_handoffs
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, NpcCopies& copies, NpcShare& ourNpcs, net::RelaySession& session,
               const LogFunction& log);
}
