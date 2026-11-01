/*
 npc tags the first cut of nightcoops npctags (NpcTag 220; the script half is NightCoopTags in scripts/NightCoopMac/NightCoopTags.reds)
 evry 200 ms the tags and untags our player made (focus mode tagging system wraps) on npcs that are shared (ours or a partners
 copy here) go to the partners in our world as net ids. a partners tag gets put on our npc of that net id the same way. the
 tag list we know goes to a partner once when it comes into our world (kind 3)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class NpcShare;
class NpcCopies;
namespace net
{
class RelaySession;
}
}

namespace nightcoop_mac::npc_tags
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, const std::vector<uint32_t>& peersInOurWorld, const NpcShare& ours, const NpcCopies& theirs, net::RelaySession& session,
          const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, const NpcShare& ours, const NpcCopies& theirs, const LogFunction& log);
}
