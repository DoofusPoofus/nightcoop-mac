#include "plugin/SceneSkip.hpp"

#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>

namespace nightcoop_mac::scene_skip
{
namespace
{
constexpr uint8_t LineSkipType = 118;
constexpr uint64_t SkipHereEveryMs = 5000;
constexpr uint64_t LookEveryMs = 500;

uint64_t NextLookMs = 0;
uint64_t NextSkipHereMs = 0;
int32_t LastWhyNot = -1;
uint64_t FastForwardUntilMs = 0;

bool CallFastForward(bool on)
{
    static auto* fastForward = game::FindFunction("NightCoopSceneSkip", "FastForward");
    auto service = game::ScriptService("NightCoopSceneSkip");
    bool done = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &on);
    return fastForward && service && game::CallMethod(service.instance, fastForward, &done, args) && done;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == LineSkipType || messageType == static_cast<uint8_t>(nc::proto::MsgType::SceneSkipHere);
}

void Tick(uint64_t nowMs, bool inSave, bool sameWorld, net::RelaySession& session, const LogFunction& log)
{
    if (FastForwardUntilMs && (nowMs >= FastForwardUntilMs || !inSave))
    {
        FastForwardUntilMs = 0;
        CallFastForward(false);
        log("scene skip: caught up with the partners skip");
    }
    if (!inSave || !sameWorld || nowMs < NextLookMs || !session.GetStatus().InSession)
    {
        return;
    }
    NextLookMs = nowMs + LookEveryMs;
    static auto* whyNot = game::FindFunction("NightCoopSceneSkip", "WhyNot");
    auto service = game::ScriptService("NightCoopSceneSkip");
    int32_t reason = 0;
    RED4ext::StackArgs_t noArgs;
    if (!whyNot || !service || !game::CallMethod(service.instance, whyNot, &reason, noArgs))
    {
        return;
    }
    if (reason == LastWhyNot && nowMs < NextSkipHereMs)
    {
        return;
    }
    LastWhyNot = reason;
    NextSkipHereMs = nowMs + SkipHereEveryMs;
    nc::proto::Writer here(nc::proto::MsgType::SceneSkipHere);
    here.PutU32(session.GetStatus().MyPeerNumber);
    here.PutU64(0);
    here.PutU8(0);
    here.PutU64(0);
    here.PutU16(0);
    here.PutU8(static_cast<uint8_t>(std::clamp(reason, 0, 8)));
    here.PutU64(0);
    session.SendGameMessage(here.Data(), session.GetStatus().MyPeerNumber == 0 ? net::kEveryPeer : 0);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, uint64_t nowMs, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (static_cast<uint8_t>(reader.ReadHeader()) != LineSkipType || !sameWorld)
    {
        return;
    }
    const uint32_t presser = reader.GetU32();
    const uint64_t scene = reader.GetU64();
    const uint32_t ms = reader.GetU32();
    if (!reader.Ok() || scene == 0 || ms > 100000)
    {
        return;
    }
    const uint64_t forMs = std::clamp<uint64_t>(ms, 1000, 10000);
    if (CallFastForward(true))
    {
        FastForwardUntilMs = nowMs + forMs;
        log("scene skip: peer " + std::to_string(presser) + " skipped " + std::to_string(ms) + " ms of a scene, ours runs fast for " + std::to_string(forMs) + " ms");
    }
    else
    {
        log("scene skip: peer " + std::to_string(presser) + " skipped a scene, no scene of ours to fast forward (from peer " + std::to_string(fromPeer) + ")");
    }
}
}
