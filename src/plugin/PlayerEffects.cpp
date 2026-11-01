#include "plugin/PlayerEffects.hpp"

#include "net/PlayerExtrasMessages.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <utility>

namespace nightcoop_mac::player_effects
{
namespace
{
struct alignas(16) EffectTransform
{
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Z = 0;
    int32_t Unused = 0;
    float Orientation[4] = {0, 0, 0, 1};
};
static_assert(sizeof(EffectTransform) == 32);

constexpr float FixedPointScale = 131072.0f;
constexpr size_t RememberedEffects = 256;

std::deque<std::pair<uint32_t, uint32_t>> SeenEffects;
uint32_t EffectsShown = 0;

bool Show(const net::PlayerEffect& effect)
{
    static auto* getFxSystem = game::FindFunction("ScriptGameInstance", "GetFxSystem");
    static auto* spawnEffect = game::FindFunction("gameFxSystem", "SpawnEffect");
    static const bool usable = game::ParamsAre(getFxSystem, {"ScriptGameInstance"}) && game::ParamsAre(spawnEffect, {"gameFxResource", "WorldTransform", "Bool"});
    if (!usable)
    {
        return false;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> fxSystem;
    RED4ext::StackArgs_t systemArgs;
    systemArgs.emplace_back(nullptr, &gameInstance);
    if (!game::CallStatic(getFxSystem, &fxSystem, systemArgs) || !fxSystem)
    {
        return false;
    }
    uint64_t resource = effect.Resource;
    EffectTransform transform;
    transform.X = static_cast<int32_t>(effect.Position[0] * FixedPointScale);
    transform.Y = static_cast<int32_t>(effect.Position[1] * FixedPointScale);
    transform.Z = static_cast<int32_t>(effect.Position[2] * FixedPointScale);
    std::copy(effect.Orientation, effect.Orientation + 4, transform.Orientation);
    bool ignoreTimeDilation = (effect.Flags & 1) != 0;
    RED4ext::Handle<RED4ext::IScriptable> instance;
    RED4ext::StackArgs_t spawnArgs;
    spawnArgs.emplace_back(nullptr, &resource);
    spawnArgs.emplace_back(nullptr, &transform);
    spawnArgs.emplace_back(nullptr, &ignoreTimeDilation);
    return game::CallMethod(fxSystem.instance, spawnEffect, &instance, spawnArgs) && instance;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::PlayerFx);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, const LogFunction& log)
{
    std::vector<net::PlayerEffect> effects;
    if (!net::ReadPlayerFx(message, effects) || !sameWorld)
    {
        return;
    }
    for (const auto& effect : effects)
    {
        const std::pair<uint32_t, uint32_t> key {fromPeer, effect.Number};
        if (std::find(SeenEffects.begin(), SeenEffects.end(), key) != SeenEffects.end())
        {
            continue;
        }
        SeenEffects.push_back(key);
        if (SeenEffects.size() > RememberedEffects)
        {
            SeenEffects.pop_front();
        }
        const bool shown = Show(effect);
        EffectsShown += shown ? 1 : 0;
        if (EffectsShown <= 5 || !shown)
        {
            char line[200];
            std::snprintf(line, sizeof(line), "effects: peer %u effect %#llx at %.1f %.1f %.1f %s", fromPeer, static_cast<unsigned long long>(effect.Resource),
                          effect.Position[0], effect.Position[1], effect.Position[2], shown ? "shown here (picture and sound, no damage)" : "did not show here");
            log(line);
        }
    }
}
}
