#include "plugin/QuestTeleports.hpp"

#include "net/GameMessages.hpp"
#include "net/SessionMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Hud.hpp"
#include "plugin/KeyWatch.hpp"
#include "plugin/TestKeys.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nightcoop_mac::quest_teleports
{
namespace
{
constexpr float NearestOfferMeters = 40.0f;
constexpr float FarthestOfferMeters = 600.0f;
constexpr uint64_t OfferLastsMs = 60000;
constexpr uint64_t HoldForMs = 1000;

bool OfferOpen = false;
uint64_t OfferUntilMs = 0;
float OfferPlace[3] = {0, 0, 0};
float OfferYaw = 0;
uint64_t HeldSinceMs = 0;
uint64_t TestKeyDownAtMs = 0;
bool HaveLastPlace = false;
float LastPlace[3] = {0, 0, 0};

bool MoveTo(const float* place, float yaw)
{
    static auto* moveTo = game::FindFunction("NightCoopTeleport", "MovePlayerTo");
    auto service = game::ScriptService("NightCoopTeleport");
    if (!moveTo || !service)
    {
        return false;
    }
    float x = place[0] + 2.0f * std::cos(yaw * 3.14159265f / 180.0f);
    float y = place[1] + 2.0f * std::sin(yaw * 3.14159265f / 180.0f);
    float z = place[2];
    bool moved = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &x);
    args.emplace_back(nullptr, &y);
    args.emplace_back(nullptr, &z);
    args.emplace_back(nullptr, &yaw);
    return game::CallMethod(service.instance, moveTo, &moved, args) && moved;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestTeleport) || messageType == static_cast<uint8_t>(nc::proto::MsgType::StoryPull);
}

void WatchOurMoves(bool inSave, bool partnersInOurWorld, const net::PlayerState* ourPlayer, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || !ourPlayer)
    {
        HaveLastPlace = false;
        return;
    }
    const float place[3] = {ourPlayer->PositionX, ourPlayer->PositionY, ourPlayer->PositionZ};
    if (HaveLastPlace && partnersInOurWorld && session.GetStatus().MyPeerNumber == 0)
    {
        const float x = place[0] - LastPlace[0];
        const float y = place[1] - LastPlace[1];
        const float z = place[2] - LastPlace[2];
        const float meters = std::sqrt(x * x + y * y + z * z);
        if (meters >= NearestOfferMeters)
        {
            nc::proto::Writer teleport(nc::proto::MsgType::QuestTeleport);
            teleport.PutU8(1);
            for (float axis : place)
            {
                teleport.PutF32(axis);
            }
            teleport.PutF32(ourPlayer->Yaw);
            for (float axis : LastPlace)
            {
                teleport.PutF32(axis);
            }
            session.SendGameMessage(teleport.Data());
            char line[128];
            std::snprintf(line, sizeof(line), "quest teleports: we got moved %.0f m, the partners get an offer", meters);
            log(line);
        }
    }
    std::copy(place, place + 3, LastPlace);
    HaveLastPlace = true;
}

void Tick(uint64_t nowMs, bool inSave, bool testHold, const LogFunction& log)
{
    if (TestKeyDownAtMs && (!OfferOpen || nowMs - TestKeyDownAtMs > 3000))
    {
        PostKey(KeyPageUp, "\xEF\x9C\xAC", false);
        TestKeyDownAtMs = 0;
    }
    if (!OfferOpen)
    {
        return;
    }
    if (testHold && !TestKeyDownAtMs)
    {
        PostKey(KeyPageUp, "\xEF\x9C\xAC", true);
        TestKeyDownAtMs = nowMs;
        log("quest teleports: test_teleport holds PageUp");
    }
    if (!inSave || nowMs >= OfferUntilMs)
    {
        OfferOpen = false;
        return;
    }
    if (!KeyHeld(KeyPageUp))
    {
        HeldSinceMs = 0;
        return;
    }
    if (!HeldSinceMs)
    {
        HeldSinceMs = nowMs;
    }
    if (nowMs - HeldSinceMs < HoldForMs)
    {
        return;
    }
    OfferOpen = false;
    const bool moved = MoveTo(OfferPlace, OfferYaw);
    log(moved ? "quest teleports: we went to the host" : "quest teleports: the move to the host did not start");
    hud::Show(moved ? "You are next to the host" : "Could not move you to the host", 3.0f);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool takeOffers, const net::PlayerState* ourPlayer, uint64_t nowMs, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    const auto type = reader.ReadHeader();
    if (fromPeer != 0 || !takeOffers || !ourPlayer)
    {
        return;
    }
    if (type == nc::proto::MsgType::StoryPull)
    {
        const uint8_t pullKind = reader.GetU8();
        reader.GetU32();
        reader.GetU32();
        reader.GetU64();
        reader.GetU16();
        reader.GetU32();
        reader.GetU32();
        float point[4];
        for (float& value : point)
        {
            value = reader.GetF32();
        }
        if (!reader.Ok() || reader.Remaining() != 0 || pullKind < 1 || pullKind > 3 || !std::all_of(point, point + 4, [](float value) { return std::isfinite(value); }))
        {
            return;
        }
        const float x = point[0] - ourPlayer->PositionX;
        const float y = point[1] - ourPlayer->PositionY;
        const float z = point[2] - ourPlayer->PositionZ;
        const float meters = std::sqrt(x * x + y * y + z * z);
        char line[128];
        std::snprintf(line, sizeof(line), "quest teleports: the host calls us over (story pull kind %u), %.0f m", pullKind, meters);
        log(line);
        if (meters < 10.0f)
        {
            return;
        }
        OfferOpen = true;
        OfferUntilMs = nowMs + OfferLastsMs;
        std::copy(point, point + 3, OfferPlace);
        OfferYaw = point[3];
        HeldSinceMs = 0;
        hud::Show("The host calls you over: hold PageUp to be moved there", 5.0f);
        return;
    }
    if (type != nc::proto::MsgType::QuestTeleport)
    {
        return;
    }
    const uint8_t kind = reader.GetU8();
    float values[7];
    for (float& value : values)
    {
        value = reader.GetF32();
    }
    if (!reader.Ok() || reader.Remaining() != 0 || kind < 1 || kind > 3 || !std::all_of(values, values + 7, [](float value) { return std::isfinite(value); }))
    {
        return;
    }
    const float x = values[0] - ourPlayer->PositionX;
    const float y = values[1] - ourPlayer->PositionY;
    const float z = values[2] - ourPlayer->PositionZ;
    const float meters = std::sqrt(x * x + y * y + z * z);
    char line[128];
    if (meters < NearestOfferMeters || meters > FarthestOfferMeters)
    {
        std::snprintf(line, sizeof(line), "quest teleports: the host moved (kind %u), %.0f m from us, no offer", kind, meters);
        log(line);
        return;
    }
    OfferOpen = true;
    OfferUntilMs = nowMs + OfferLastsMs;
    std::copy(values, values + 3, OfferPlace);
    OfferYaw = values[3];
    HeldSinceMs = 0;
    std::snprintf(line, sizeof(line), "quest teleports: the host moved (kind %u), offered, %.0f m", kind, meters);
    log(line);
    hud::Show(kind == 3 ? "The host's story scene talks: hold PageUp to go there" : "The host was moved: hold PageUp to go to them", 5.0f);
}
}
