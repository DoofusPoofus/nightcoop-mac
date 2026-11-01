#include "plugin/NpcTags.hpp"

#include "net/NpcMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/NpcCopies.hpp"
#include "plugin/NpcShare.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <algorithm>
#include <set>
#include <sstream>

namespace nightcoop_mac::npc_tags
{
namespace
{
constexpr uint64_t LookEveryMs = 200;

uint64_t NextLookMs = 0;
std::set<uint32_t> TaggedNetIds;
std::set<uint32_t> PeersTold;
bool WasInSave = false;

uint32_t NetIdOf(uint64_t entityHash, const NpcShare& ours, const NpcCopies& theirs)
{
    const uint32_t ourNetId = ours.NetIdOfEntity(entityHash);
    return ourNetId ? ourNetId : theirs.NetIdOfCopy(entityHash);
}

bool ApplyTag(uint64_t entityHash, bool tagged)
{
    static auto* findEntity = game::FindFunction("ScriptGameInstance", "FindEntityByID");
    static auto* applyTag = game::FindFunction("NightCoopTags", "ApplyTag");
    auto service = game::ScriptService("NightCoopTags");
    if (!findEntity || !applyTag || !service || !entityHash)
    {
        return false;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::Handle<RED4ext::IScriptable> npc;
    RED4ext::StackArgs_t findArgs;
    findArgs.emplace_back(nullptr, &gameInstance);
    findArgs.emplace_back(nullptr, &entityId);
    if (!game::CallStatic(findEntity, &npc, findArgs) || !npc)
    {
        return false;
    }
    bool applied = false;
    RED4ext::StackArgs_t tagArgs;
    tagArgs.emplace_back(nullptr, &npc);
    tagArgs.emplace_back(nullptr, &tagged);
    game::CallMethod(service.instance, applyTag, &applied, tagArgs);
    return applied;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::NpcTag);
}

void Tick(uint64_t nowMs, bool inSave, const std::vector<uint32_t>& peersInOurWorld, const NpcShare& ours, const NpcCopies& theirs, net::RelaySession& session,
          const LogFunction& log)
{
    if (!inSave)
    {
        if (WasInSave)
        {
            TaggedNetIds.clear();
            PeersTold.clear();
        }
        WasInSave = false;
        return;
    }
    WasInSave = true;
    if (nowMs < NextLookMs)
    {
        return;
    }
    NextLookMs = nowMs + LookEveryMs;
    static auto* takeTags = game::FindFunction("NightCoopTags", "TakeTags");
    auto service = game::ScriptService("NightCoopTags");
    if (!takeTags || !service)
    {
        return;
    }
    RED4ext::CString lines;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, takeTags, &lines, noArgs);
    std::vector<uint32_t> tagged;
    std::vector<uint32_t> untagged;
    std::istringstream input(lines.c_str());
    int taggedWord = 0;
    uint64_t entityHash = 0;
    while (input >> taggedWord >> entityHash)
    {
        const uint32_t netId = NetIdOf(entityHash, ours, theirs);
        if (!netId)
        {
            continue;
        }
        (taggedWord ? tagged : untagged).push_back(netId);
        if (taggedWord)
        {
            TaggedNetIds.insert(netId);
        }
        else
        {
            TaggedNetIds.erase(netId);
        }
    }
    if (peersInOurWorld.empty() || !session.GetStatus().InSession)
    {
        return;
    }
    if (!tagged.empty())
    {
        session.SendGameMessage(net::WriteNpcTag(net::kTagSet, tagged));
        log("tags: we tagged " + std::to_string(tagged.size()) + " shared npc(s), the partners see them too");
    }
    if (!untagged.empty())
    {
        session.SendGameMessage(net::WriteNpcTag(net::kTagCleared, untagged));
    }
    for (const uint32_t peer : peersInOurWorld)
    {
        if (PeersTold.insert(peer).second && !TaggedNetIds.empty())
        {
            session.SendGameMessage(net::WriteNpcTag(net::kTagList, {TaggedNetIds.begin(), TaggedNetIds.end()}), static_cast<int32_t>(peer));
        }
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, const NpcShare& ours, const NpcCopies& theirs, const LogFunction& log)
{
    uint8_t kind = 0;
    std::vector<uint32_t> netIds;
    if (!net::ReadNpcTag(message, kind, netIds) || !sameWorld)
    {
        return;
    }
    const bool tagged = kind != net::kTagCleared;
    int applied = 0;
    for (const uint32_t netId : netIds)
    {
        const uint64_t ourEntity = ours.EntityOfNetId(netId);
        if (ApplyTag(ourEntity ? ourEntity : theirs.EntityOfNetId(netId), tagged))
        {
            ++applied;
            if (tagged)
            {
                TaggedNetIds.insert(netId);
            }
            else
            {
                TaggedNetIds.erase(netId);
            }
        }
    }
    log("tags: peer " + std::to_string(fromPeer) + (tagged ? " tagged " : " untagged ") + std::to_string(netIds.size()) + " npc(s), " + std::to_string(applied) +
        " of them are here");
}
}
