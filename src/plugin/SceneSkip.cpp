#include "plugin/SceneSkip.hpp"

#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>
#include <cstring>

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
uint64_t PressStartedMs = 0;
constexpr uint64_t OurSceneKey = 0x4E43534B49500001ULL;

std::vector<uint8_t> WriteLineSkip(uint32_t presser, uint32_t ms, uint64_t sharedTime)
{
    nc::proto::Writer skip(static_cast<nc::proto::MsgType>(LineSkipType));
    skip.PutU32(presser);
    skip.PutU64(OurSceneKey);
    skip.PutU32(std::min<uint32_t>(ms, 100000));
    skip.PutU64(sharedTime ? sharedTime : 1);
    skip.PutU8(1);
    skip.PutU8(1);
    skip.PutU8(1);
    skip.PutU64(OurSceneKey);
    skip.PutU16(1);
    return skip.Data();
}

uint8_t MessageTypeOfRaw(const std::vector<uint8_t>& message)
{
    return message.size() > 4 ? message[4] : 0;
}

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
    static auto* pressing = game::FindFunction("NightCoopSceneSkip", "Pressing");
    auto service = game::ScriptService("NightCoopSceneSkip");
    bool held = false;
    RED4ext::StackArgs_t pressArgs;
    if (pressing && service)
    {
        game::CallMethod(service.instance, pressing, &held, pressArgs);
    }
    if (held && !PressStartedMs)
    {
        PressStartedMs = nowMs;
    }
    else if (!held && PressStartedMs)
    {
        const auto ms = static_cast<uint32_t>(nowMs - PressStartedMs);
        PressStartedMs = 0;
        const uint32_t ourPeer = session.GetStatus().MyPeerNumber;
        session.SendGameMessage(WriteLineSkip(ourPeer, ms, session.SessionClockMs()), ourPeer == 0 ? net::kEveryPeer : 0);
        log("scene skip: we skipped " + std::to_string(ms) + " ms of a scene, the partners catch up");
    }
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

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (session.GetStatus().MyPeerNumber == 0 && fromPeer != 0 && static_cast<uint8_t>(MessageTypeOfRaw(message)) == LineSkipType)
    {
        auto relayed = message;
        if (relayed.size() >= 9)
        {
            std::memcpy(relayed.data() + 5, &fromPeer, sizeof(fromPeer));
        }
        for (const auto& [peer, nickname] : session.PeerNicknames())
        {
            if (peer != 0 && peer != fromPeer)
            {
                session.SendGameMessage(relayed, static_cast<int32_t>(peer));
            }
        }
    }
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
