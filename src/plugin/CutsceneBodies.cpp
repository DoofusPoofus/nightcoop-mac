#include "plugin/CutsceneBodies.hpp"

#include "plugin/GameCalls.hpp"
#include "plugin/RemoteBodies.hpp"

#include <RED4ext/RED4ext.hpp>

namespace nightcoop_mac::cutscene_bodies
{
namespace
{
constexpr uint64_t LookEveryMs = 500;

uint64_t NextLookMs = 0;
uint64_t InSaveSinceMs = 0;
bool Hidden = false;
}

void Tick(uint64_t nowMs, bool inSave, bool testHide, const std::vector<uint32_t>& peers, RemoteBodies& bodies, const LogFunction& log)
{
    if (!inSave)
    {
        InSaveSinceMs = 0;
        Hidden = false;
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
    if (!hide && !Hidden)
    {
        return;
    }
    int32_t toggled = 0;
    for (const uint32_t peer : peers)
    {
        RED4ext::Handle<RED4ext::IScriptable> body;
        if (!bodies.GetBody(peer, body) || !body)
        {
            continue;
        }
        int32_t changed = 0;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &body);
        args.emplace_back(nullptr, &hide);
        game::CallMethod(service.instance, setBodyHidden, &changed, args);
        toggled += changed;
    }
    if (hide != Hidden)
    {
        Hidden = hide;
        log(std::string("cutscenes: ") + (hide ? "a scene holds our player, partner bodies hidden" : "the scene let go, partner bodies shown again") + " (" +
            std::to_string(toggled) + " visual component(s) toggled)");
    }
}
}
