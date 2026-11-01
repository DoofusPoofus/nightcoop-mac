/*
 npc combat the first cut of nightcoops damageclaims (HitClaim 18 / HitResult 19 the script half is NightCoopCombat in
 scripts/NightCoopMac/NightCoopCombat.reds)
 attacker side: copies of partner npcs get marked in the script service (immortal so our hits only count). evry tick the
 claims the script made from our players hits on copies are taken turned into HitClaim (claim number the copys net id the
 percent lethal when it is at least the health the owner last sent our clock the hit details with the narrow flag filter) and
 sent to the copys owner. HitResult answers get counted
 owner side: a HitClaim for one of our shared npcs (NpcShare) is checked (finite and above 0 not a claim number we already
 took from that peer in the last 1024) and applied by the script as damage from the claimers partner body (killed at 0.15 % or
 when lethal) then HitResult goes back with the applied percent and the health after (4 no such npc 5 it died already)
 the killer of a shared npc goes out in NpcDespawn (NpcShare) the copy of evry partner but the killer gets no kill reward
 PlayerHit: partner bodies are marked in the script service an npc hit on one goes to that partner (the attacker as the net id
 of our shared npc or copy 0 when it is neither). a PlayerHit for us is rebuilt by the script with our copy (or our own npc)
 of the attacker as instigator and queued on our player an unknown attacker is dropped like nightcoop does
 quickhacks: our quickhack stages on a copy go to its owner as HackSync kind 1 and a partners stage on one of our npcs hangs
 the actions start or completion statuses on it (script ApplyHackStage). test_claim also sends a QuickHack.BlindLvl1Hack
 completion evry 5 s
 defeats: a partner body knocking out one of our npcs without killing it (BountyManager.CompleteBounty wrap) sends that partner
 RewardWord kind 2 and its game runs RewardKiller (defeat) on its copy so it gets the reward
 police: one of our police units fighting a partner body (the threat share above) tells that partner once per 10 s
 (PoliceFight) and its game queues a PreventionCombatStartedRequest with its copy of the unit so its own police fight starts
 not done yet: NpcAction hit zones hit reactions the expected hit path and the visual hack mirror
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
void MarkBody(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body);
void ShareThreat(RED4ext::Handle<RED4ext::IScriptable>& npc, RED4ext::Handle<RED4ext::IScriptable>& body);
void OnPartnerDefeat(uint32_t fromPeer, const std::vector<uint8_t>& message, NpcCopies& copies, const LogFunction& log);
void NotePoliceOnBody(uint64_t nowMs, uint32_t peerNumber, uint32_t netId, const float* position, RED4ext::Handle<RED4ext::IScriptable>& npc);
void Tick(uint64_t nowMs, bool inSave, bool testClaims, NpcCopies& copies, NpcShare& ourNpcs, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, NpcCopies& copies, NpcShare& ourNpcs, RemoteBodies& bodies,
               net::RelaySession& session, const LogFunction& log);
}
