#include "plugin/CutsceneBodies.hpp"

#include "plugin/GameCalls.hpp"
#include "plugin/RemoteBodies.hpp"
#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/SessionMessages.hpp"

#include <ProtocolCore.hpp>

#include <RED4ext/RED4ext.hpp>

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace nightcoop_mac::cutscene_bodies
{
namespace
{
constexpr uint64_t LookEveryMs = 500;

uint64_t NextLookMs = 0;
uint64_t InSaveSinceMs = 0;
bool Hidden = false;
std::map<uint32_t, bool> HiddenNear;
std::map<uint32_t, bool> BodyHidden;
bool InScene = false;

struct LiveScene
{
    uint8_t Category = 0;
    float Entry[3] = {0, 0, 0};
};

std::map<uint64_t, LiveScene> HostScenes;
std::set<uint64_t> ScenesWePlay;

std::vector<uint8_t> WriteScene(uint8_t kind, uint64_t scene, uint8_t category, const float* place)
{
    nc::proto::Writer writer(nc::proto::MsgType::QuestScene);
    writer.PutU8(kind);
    writer.PutU64(scene);
    writer.PutU8(category);
    for (int axis = 0; axis < 3; ++axis)
    {
        writer.PutF32(place[axis]);
    }
    writer.PutU64(0);
    writer.PutU8(kind == 3 ? 1 : 0);
    return writer.Data();
}
}

bool HandlesScenes(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestScene);
}

void OnSceneMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (weAreHost || fromPeer != 0 || reader.ReadHeader() != nc::proto::MsgType::QuestScene)
    {
        return;
    }
    const uint8_t kind = reader.GetU8();
    const uint64_t scene = reader.GetU64();
    const uint8_t category = reader.GetU8();
    float entry[3];
    for (float& axis : entry)
    {
        axis = reader.GetF32();
    }
    reader.GetU64();
    reader.GetU8();
    if (!reader.Ok() || reader.Remaining() != 0 || scene == 0 || !std::isfinite(entry[0]) || !std::isfinite(entry[1]) || !std::isfinite(entry[2]))
    {
        return;
    }
    if (kind == 1 || kind == 6)
    {
        auto& live = HostScenes[scene];
        live.Category = category;
        std::copy(entry, entry + 3, live.Entry);
    }
    else if (kind == 2)
    {
        HostScenes.erase(scene);
        ScenesWePlay.erase(scene);
    }
}

void TellHostScenes(bool inHostWorld, const net::PlayerState* ourPlayer, net::RelaySession& session, const LogFunction& log)
{
    if (!inHostWorld || !ourPlayer || session.GetStatus().MyPeerNumber == 0)
    {
        ScenesWePlay.clear();
        return;
    }
    const float place[3] = {ourPlayer->PositionX, ourPlayer->PositionY, ourPlayer->PositionZ};
    if (InScene)
    {
        for (const auto& [scene, live] : HostScenes)
        {
            const float x = live.Entry[0] - place[0];
            const float y = live.Entry[1] - place[1];
            const float z = live.Entry[2] - place[2];
            if (std::sqrt(x * x + y * y + z * z) <= 30.0f && ScenesWePlay.insert(scene).second)
            {
                session.SendGameMessage(WriteScene(3, scene, live.Category, place), 0);
                char line[96];
                std::snprintf(line, sizeof(line), "cutscenes: we play the hosts scene %#llx too", static_cast<unsigned long long>(scene));
                log(line);
            }
        }
        return;
    }
    for (const uint64_t scene : ScenesWePlay)
    {
        const auto live = HostScenes.find(scene);
        session.SendGameMessage(WriteScene(4, scene, live != HostScenes.end() ? live->second.Category : 0, place), 0);
    }
    ScenesWePlay.clear();
}

void Tick(uint64_t nowMs, bool inSave, bool testHide, const std::vector<RemotePeerView>& views, const net::PlayerState* ourPlayer, RemoteBodies& bodies,
          const LogFunction& log)
{
    if (!inSave)
    {
        InSaveSinceMs = 0;
        Hidden = false;
        InScene = false;
        HostScenes.clear();
        HiddenNear.clear();
        BodyHidden.clear();
        return;
    }
    if (!InSaveSinceMs)
    {
        InSaveSinceMs = nowMs;
    }
    if (nowMs < NextLookMs)
    {
        return;
    }
    NextLookMs = nowMs + LookEveryMs;
    static auto* inCutscene = game::FindFunction("NightCoopCutscenes", "InCutscene");
    static auto* setBodyHidden = game::FindFunction("NightCoopCutscenes", "SetBodyHidden");
    auto service = game::ScriptService("NightCoopCutscenes");
    if (!inCutscene || !setBodyHidden || !service)
    {
        return;
    }
    bool hide = false;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, inCutscene, &hide, noArgs);
    const uint64_t intoSave = nowMs - InSaveSinceMs;
    hide = hide || (testHide && intoSave >= 20000 && intoSave < 40000);
    InScene = hide;
    int32_t toggled = 0;
    for (const auto& view : views)
    {
        bool& near = HiddenNear[view.PeerNumber];
        if (ourPlayer)
        {
            const float x = view.State.PositionX - ourPlayer->PositionX;
            const float y = view.State.PositionY - ourPlayer->PositionY;
            const float z = view.State.PositionZ - ourPlayer->PositionZ;
            const float meters = std::sqrt(x * x + y * y + z * z);
            near = meters < 0.5f ? true : meters > 1.0f ? false : near;
        }
        bool hideThis = hide || near;
        bool& wasHidden = BodyHidden[view.PeerNumber];
        if (!hideThis && !wasHidden)
        {
            continue;
        }
        RED4ext::Handle<RED4ext::IScriptable> body;
        if (!bodies.GetBody(view.PeerNumber, body) || !body)
        {
            continue;
        }
        int32_t changed = 0;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &body);
        args.emplace_back(nullptr, &hideThis);
        game::CallMethod(service.instance, setBodyHidden, &changed, args);
        toggled += changed;
        if (near != wasHidden && !hide && changed)
        {
            log("cutscenes: peer " + std::to_string(view.PeerNumber) + (near ? "s body stands inside ours, hidden" : "s body stepped away, shown again"));
        }
        wasHidden = hideThis;
    }
    if (hide != Hidden)
    {
        Hidden = hide;
        log(std::string("cutscenes: ") + (hide ? "a scene holds our player, partner bodies hidden" : "the scene let go, partner bodies shown again") + " (" +
            std::to_string(toggled) + " visual component(s) toggled)");
    }
}
}
