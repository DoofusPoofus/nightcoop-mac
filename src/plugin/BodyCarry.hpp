/*
 body carry nightcoops bodycarry (PlayerCarry 92). the script half is NightCoopBodyCarry in scripts/NightCoopMac/NightCoopBodyCarry.reds
 our side: evry 100 ms the script looks for an npc mounted on our player in leftShoulder (or bothHands a quest carry in the
 arms) and the anim tap gives the Carry anim feature our state machine wrote last (state pickup animation flags). a new child
 is a new carry with a new carry number. it goes out when the state pickup animation or flags change and at least evry second
 with the net id when its one of our npc copies or shared npcs its entity id record look and place. a copy we carry stops
 following its owners snapshots while we hold it. when the child is gone the end goes out (drop / release / dispose from the
 feature dispose when the state machine blackboard said disposal) and 2 more times right after
 partner side: an active kind (pickup carry jump aim with the active bit) looks for our body of that npc: our copy of that
 net id when the carrier owns it our own shared npc or the same entity id in our world with the same record. it gets mounted
 on the partners body in leftShoulder with its ragdoll off and the Carry and Mounting features written on it. an end takes it
 off and 300 ms later it gets put where the partner left it with its ragdoll on or hidden for good after a dispose. a carrier
 that goes quiet for 5 s loses the carry
 not done yet: quest carries in the arms (nightcoop spawns doubles) shoulder doubles when we have no body of the npc
 NpcTakeRequest (91) and community keys
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

namespace nightcoop_mac::body_carry
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, RemoteBodies& bodies, NpcCopies& copies, NpcShare& ourNpcs, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, const LogFunction& log);
}
