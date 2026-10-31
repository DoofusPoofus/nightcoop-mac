#include "plugin/GameClock.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <cmath>

namespace nightcoop_mac::game_clock
{
namespace
{
constexpr uint64_t SendEveryMs = 5000;
constexpr int64_t LeaveAloneWithinSeconds = 120;

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetTimeSystem = nullptr;
    RED4ext::CBaseFunction* GetGameTime = nullptr;
    RED4ext::CBaseFunction* SetGameTimeBySeconds = nullptr;
};

Calls Game;
uint64_t NextSendMs = 0;
uint32_t ClockSets = 0;

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    Game.GetTimeSystem = game::FindFunction("ScriptGameInstance", "GetTimeSystem");
    Game.GetGameTime = game::FindFunction("gameTimeSystem", "GetGameTime");
    Game.SetGameTimeBySeconds = game::FindFunction("gameTimeSystem", "SetGameTimeBySeconds");
    Game.Usable = game::ParamsAre(Game.GetTimeSystem, {"ScriptGameInstance"}) && game::ParamsAre(Game.GetGameTime, {}) &&
                  game::ReturnIs(Game.GetGameTime, "GameTime") && game::ParamsAre(Game.SetGameTimeBySeconds, {"Int32"});
    log(Game.Usable ? "game clock: the time of day follows the host like in nightcoop"
                    : "game clock: the time system calls look different than expected (" + game::Describe(Game.GetGameTime) + ", " +
                          game::Describe(Game.SetGameTimeBySeconds) + "), evry player keeps their own time of day");
    return Game.Usable;
}

RED4ext::Handle<RED4ext::IScriptable> TimeSystem()
{
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> system;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    game::CallStatic(Game.GetTimeSystem, &system, args);
    return system;
}

bool ReadGameSeconds(uint32_t& seconds)
{
    auto timeSystem = TimeSystem();
    RED4ext::StackArgs_t noArgs;
    return timeSystem && game::CallMethod(timeSystem.instance, Game.GetGameTime, &seconds, noArgs);
}
}

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log)
{
    const auto status = session.GetStatus();
    if (!inSave || !status.InSession || status.MyPeerNumber != 0 || nowMs < NextSendMs || !Resolve(log))
    {
        return;
    }
    NextSendMs = nowMs + SendEveryMs;
    uint32_t seconds = 0;
    if (ReadGameSeconds(seconds))
    {
        session.SendGameMessage(net::WriteGameClock(seconds), net::kEveryPeer, false);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool inSave, bool weAreHost, const LogFunction& log)
{
    uint32_t hostSeconds = 0;
    if (fromPeer != 0 || weAreHost || !inSave || !net::ReadGameClock(message, hostSeconds) || !Resolve(log))
    {
        return;
    }
    uint32_t ourSeconds = 0;
    if (!ReadGameSeconds(ourSeconds))
    {
        return;
    }
    const int64_t apart = static_cast<int64_t>(hostSeconds) - static_cast<int64_t>(ourSeconds);
    if (std::llabs(apart) <= LeaveAloneWithinSeconds)
    {
        return;
    }
    auto timeSystem = TimeSystem();
    int32_t seconds = static_cast<int32_t>(hostSeconds);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &seconds);
    if (timeSystem && game::CallMethod(timeSystem.instance, Game.SetGameTimeBySeconds, nullptr, args) && ClockSets++ < 20)
    {
        log("game clock: took the hosts time, ours was " + std::to_string(-apart) + " s off");
    }
}
}
