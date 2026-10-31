/*
 world loot, a first cut of nightcoops worldloot (LootState 42, LootTake 43, LootTakeResult 44): wat our player drops shows up
 for partners in our world with the same items, and taking from a partners drop really takes it out of theirs, so one item
 cant be looted twice. the script half is NightCoopWorldLoot in scripts/NightCoopMac/NightCoopLoot.reds
 our drops (kind 2): the script catches our player dropping something, the drop gets a container id of our peer << 24 | a 24
 bit counter and its items get read evry 500 ms (100 ms while our player looks at it). a change sends a LootState with the
 revision + 1, else it goes again evry 5 s. a drop that is gone or empty while we are within 30 m is closed, one that streamed
 out is let go. a partners LootTake takes the items out of our drop (all or nothing) and gets a LootTakeResult, a resent take
 gets the same answer again
 mirrors: an open drop of a partner within 55 m gets spawned here thru their body (the items go to the body and the loot
 manager drops them like nightcoops SpawnMirrorDrop, a playerDropBag when its a bag or more than one item), then kept like
 their list says. wat leaves the mirror was taken by our player: a LootTake to the owner, again evry 2 s until the result,
 given up after 60 s, and the mirror holds still until the owners next revision. a denied take gets rolled back off our
 player. a mirror that vanishes while our player looked at it or stood within 4 m got picked up whole, one that streamed out
 gets watched for so it binds again when it comes back
 not done yet: corpses of shared npcs (kind 1 by npc net id, the ProcessLoot redirect on copies), world corpses (kind 3 by
 community key), held items of copies, loot_personal, the corpse marker hold and the owners tiers (ForceItemTier)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class RemoteBodies;
}

namespace nightcoop_mac::net
{
class RelaySession;
struct PlayerState;
}

namespace nightcoop_mac::world_loot
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool sharing, const net::PlayerState* ourPlayer, net::RelaySession& session, RemoteBodies& bodies,
          const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool ownerInOurWorld, net::RelaySession& session, const LogFunction& log);
bool TestDrop(const std::string& recordName, const LogFunction& log);
std::string Status();
}
