/*
 npc combat, the first cut of nightcoops damageclaims (HitClaim 18 / HitResult 19, the script half is NightCoopCombat in
 scripts/NightCoopMac/NightCoopCombat.reds)
 attacker side: copies of partner npcs get marked in the script service (immortal, so our hits only count). evry tick the
 claims the script made from our players hits on copies are taken, turned into HitClaim (claim number, the copys net id, the
 percent, lethal when it is at least the health the owner last sent, our clock, the hit details with the narrow flag filter) and
 sent to the copys owner. HitResult answers get counted
 owner side: a HitClaim for one of our shared npcs (NpcShare) is checked (finite and above 0, not a claim number we already
 took from that peer in the last 1024) and applied by the script as damage from the claimers partner body (killed at 0.15 % or
 when lethal), then HitResult goes back with the applied percent and the health after (4 no such npc, 5 it died already)
 the killer of a shared npc goes out in NpcDespawn (NpcShare), the copy of evry partner but the killer gets no kill reward
 not done yet: NpcAction hit zones, hit reactions and threat on the owner, the expected hit path, xp / bounty wraps, PlayerHit
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

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

namespace nightcoop_mac::npc_combat
{
using LogFunction = std::function<void(const std::string&)>;

void MarkCopy(RED4ext::Handle<RED4ext::IScriptable>& copy);
void ReleaseCopy(RED4ext::Handle<RED4ext::IScriptable>& copy, bool rewardUs);
void ForgetCopy(uint64_t entityHash);
void ForgetAll();
void Tick(uint64_t nowMs, bool inSave, bool testClaims, NpcCopies& copies, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, NpcShare& ourNpcs, RemoteBodies& bodies,
               net::RelaySession& session, const LogFunction& log);
}
