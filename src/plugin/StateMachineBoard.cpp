#include "plugin/StateMachineBoard.hpp"

#include "plugin/GameCalls.hpp"

#include <map>
#include <string>

namespace nightcoop_mac::state_machine_board
{
namespace
{
struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetBoard = nullptr;
    RED4ext::CBaseFunction* GetAllDefs = nullptr;
    RED4ext::CBaseFunction* GetInt = nullptr;
    RED4ext::CProperty* StateMachineDefs = nullptr;
    RED4ext::CClass* StateMachineDef = nullptr;
    RED4ext::Handle<RED4ext::IScriptable> Defs;
    std::map<std::string, RED4ext::CProperty*> Ids;
};

Calls Game;

bool Resolve()
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    Game.GetBoard = game::FindFunction("PlayerPuppet", "GetPlayerStateMachineBlackboard");
    Game.GetAllDefs = rtti->GetFunction("GetAllBlackboardDefs");
    Game.GetInt = game::FindFunction("gameIBlackboard", "GetInt");
    auto* allDefs = rtti->GetClass("gamebbAllScriptDefinitions");
    Game.StateMachineDefs = allDefs ? allDefs->GetProperty("PlayerStateMachine") : nullptr;
    Game.StateMachineDef = rtti->GetClass("PlayerStateMachineDef");
    Game.Usable = game::ParamsAre(Game.GetBoard, {}) && game::ParamsAre(Game.GetAllDefs, {}) && game::ParamsAre(Game.GetInt, {"gamebbScriptID_Int32"}) &&
                  Game.StateMachineDefs && Game.StateMachineDef;
    return Game.Usable;
}

RED4ext::CProperty* IdProperty(const char* name)
{
    auto known = Game.Ids.find(name);
    if (known != Game.Ids.end())
    {
        return known->second;
    }
    auto* property = Game.StateMachineDef->GetProperty(name);
    if (property && (!property->type || std::string(property->type->GetName().ToString()) != "gamebbScriptID_Int32"))
    {
        property = nullptr;
    }
    Game.Ids[name] = property;
    return property;
}
}

bool ReadInt(RED4ext::Handle<RED4ext::IScriptable>& player, const char* name, int32_t& value)
{
    if (!player || !Resolve())
    {
        return false;
    }
    if (!Game.Defs)
    {
        RED4ext::StackArgs_t noArgs;
        game::CallStatic(Game.GetAllDefs, &Game.Defs, noArgs);
    }
    auto* idProperty = IdProperty(name);
    if (!Game.Defs || !idProperty)
    {
        return false;
    }
    auto* definition = Game.StateMachineDefs->GetValuePtr<RED4ext::Handle<RED4ext::IScriptable>>(Game.Defs.instance);
    if (!definition || !definition->instance)
    {
        return false;
    }
    RED4ext::Handle<RED4ext::IScriptable> board;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(player.instance, Game.GetBoard, &board, noArgs) || !board)
    {
        return false;
    }
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, idProperty->GetValuePtr<uint8_t>(definition->instance));
    return game::CallMethod(board.instance, Game.GetInt, &value, args);
}
}
