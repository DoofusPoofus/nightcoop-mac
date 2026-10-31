#include "plugin/NpcStates.hpp"

#include "plugin/GameCalls.hpp"
#include "plugin/Items.hpp"

#include <RED4ext/Scripting/Natives/Generated/game/StatsObjectID.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <cmath>

namespace nightcoop_mac::npc_states
{
namespace
{
const char* StateNames[StateCount] = {"StanceState", "UpperBodyState", "HighLevelState", "BehaviorState",
                                      "HitReactionModeState", "DefenseMode", "PhaseState", "LocomotionMode"};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetStatesComponent = nullptr;
    RED4ext::CBaseFunction* Getters[StateCount] = {};
    RED4ext::CBaseFunction* Setters[StateCount] = {};
    RED4ext::CBaseFunction* GetAttitudeAgent = nullptr;
    RED4ext::CBaseFunction* GetAttitudeTowards = nullptr;
    RED4ext::CBaseFunction* SetAttitudeTowards = nullptr;
    RED4ext::CBaseFunction* GetStatPoolsSystem = nullptr;
    RED4ext::CBaseFunction* GetStatPoolValue = nullptr;
    RED4ext::CBaseFunction* SetStatPoolValue = nullptr;
    RED4ext::CBaseFunction* IsIncapacitated = nullptr;
    RED4ext::CBaseFunction* GetTimeDilationValue = nullptr;
    RED4ext::CBaseFunction* IsIgnoringGlobalTimeDilation = nullptr;
    RED4ext::CBaseFunction* GetPSComponentName = nullptr;
    RED4ext::CBaseFunction* GetPersistencySystem = nullptr;
    RED4ext::CBaseFunction* GetConstAccessToPSObject = nullptr;
    RED4ext::CBaseFunction* GetSpawnerID = nullptr;
    RED4ext::CBaseFunction* GetOwnerCommunityEntryName = nullptr;
    RED4ext::CBaseFunction* GetRecordID = nullptr;
    RED4ext::CClass* ScriptedPuppetClass = nullptr;
    RED4ext::CClass* NpcClass = nullptr;
    RED4ext::CClass* PuppetClass = nullptr;
    uint32_t HealthPool = 0;
};

Calls Game;
std::string Missing;

bool IsA(RED4ext::Handle<RED4ext::IScriptable>& object, RED4ext::CClass* wantedClass)
{
    auto* objectClass = object ? object->GetType() : nullptr;
    return objectClass && wantedClass && objectClass->IsA(wantedClass);
}

RED4ext::Handle<RED4ext::IScriptable> StatesComponentOf(RED4ext::Handle<RED4ext::IScriptable>& npc)
{
    RED4ext::Handle<RED4ext::IScriptable> component;
    if (!IsA(npc, Game.ScriptedPuppetClass))
    {
        return component;
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(npc.instance, Game.GetStatesComponent, &component, noArgs);
    return component;
}

RED4ext::Handle<RED4ext::IScriptable> StatPools()
{
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> pools;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    game::CallStatic(Game.GetStatPoolsSystem, &pools, args);
    return pools;
}

uint64_t HandSlot(bool rightHand)
{
    return rightHand ? RED4ext::TweakDBID("AttachmentSlots.WeaponRight").value : RED4ext::TweakDBID("AttachmentSlots.WeaponLeft").value;
}
}

bool Ready()
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    Game.GetStatesComponent = game::FindFunction("ScriptedPuppet", "GetStatesComponent");
    bool statesFound = game::ParamsAre(Game.GetStatesComponent, {}) && game::ReturnIs(Game.GetStatesComponent, "handle:NPCStatesComponent");
    for (int slot = 0; slot < StateCount; ++slot)
    {
        Game.Getters[slot] = game::FindFunction("gameAINetStateComponent", (std::string("GetReplicated") + StateNames[slot]).c_str());
        Game.Setters[slot] = game::FindFunction("gameAINetStateComponent", (std::string("SetReplicated") + StateNames[slot]).c_str());
        statesFound = statesFound && game::ParamsAre(Game.Getters[slot], {}) && game::ReturnIs(Game.Getters[slot], "Int32") &&
                      game::ParamsAre(Game.Setters[slot], {"Int32"});
    }
    Game.GetAttitudeAgent = game::FindFunction("gamePuppet", "GetAttitude");
    Game.GetAttitudeTowards = game::FindFunction("gameAttitudeAgent", "GetAttitudeTowards");
    Game.SetAttitudeTowards = game::FindFunction("gameAttitudeAgent", "SetAttitudeTowards");
    Game.GetStatPoolsSystem = game::FindFunction("ScriptGameInstance", "GetStatPoolsSystem");
    Game.GetStatPoolValue = game::FindFunction("gameStatPoolsSystem", "GetStatPoolValue");
    Game.SetStatPoolValue = game::FindFunction("gameStatPoolsSystem", "RequestSettingStatPoolValue");
    Game.IsIncapacitated = game::FindFunction("NPCPuppet", "IsIncapacitated");
    Game.GetTimeDilationValue = game::FindFunction("gameTimeDilatable", "GetTimeDilationValue");
    Game.IsIgnoringGlobalTimeDilation = game::FindFunction("gameTimeDilatable", "IsIgnoringGlobalTimeDilation");
    Game.GetPSComponentName = game::FindFunction("gameEntityStubComponentPS", "GetPSComponentName");
    Game.GetPersistencySystem = game::FindFunction("ScriptGameInstance", "GetPersistencySystem");
    Game.GetConstAccessToPSObject = game::FindFunction("gamePersistencySystem", "GetConstAccessToPSObject");
    Game.GetSpawnerID = game::FindFunction("gameEntityStubComponentPS", "GetSpawnerID");
    Game.GetOwnerCommunityEntryName = game::FindFunction("gameEntityStubComponentPS", "GetOwnerCommunityEntryName");
    Game.GetRecordID = game::FindFunction("gamePuppetBase", "GetRecordID");
    Game.ScriptedPuppetClass = rtti->GetClass("ScriptedPuppet");
    Game.NpcClass = rtti->GetClass("NPCPuppet");
    Game.PuppetClass = rtti->GetClass("gamePuppet");
    if (auto* pools = rtti->GetEnum("gamedataStatPoolType"))
    {
        for (uint32_t index = 0; index < pools->hashList.Size() && index < pools->valueList.Size(); ++index)
        {
            if (pools->hashList[index] == RED4ext::CName("Health"))
            {
                Game.HealthPool = static_cast<uint32_t>(pools->valueList[index]);
            }
        }
    }
    if (!statesFound)
    {
        Game.GetStatesComponent = nullptr;
        Missing += " states";
    }
    if (!game::ParamsAre(Game.GetAttitudeAgent, {}) || !game::ReturnIs(Game.GetAttitudeAgent, "handle:gameAttitudeAgent"))
    {
        Missing += " GetAttitude";
    }
    if (!game::ParamsAre(Game.GetAttitudeTowards, {"handle:gameAttitudeAgent"}))
    {
        Missing += " GetAttitudeTowards";
    }
    if (!game::ParamsAre(Game.SetAttitudeTowards, {"handle:gameAttitudeAgent", "EAIAttitude"}))
    {
        Missing += " SetAttitudeTowards";
    }
    if (Missing.find("Attitude") != std::string::npos)
    {
        Game.GetAttitudeAgent = nullptr;
    }
    if (!game::ParamsAre(Game.SetStatPoolValue, {"gameStatsObjectID", "gamedataStatPoolType", "Float", "whandle:gameObject", "Bool", "Bool"}))
    {
        Game.SetStatPoolValue = nullptr;
        Missing += " RequestSettingStatPoolValue";
    }
    if (!game::ParamsAre(Game.IsIncapacitated, {}))
    {
        Game.IsIncapacitated = nullptr;
        Missing += " IsIncapacitated";
    }
    if (!game::ParamsAre(Game.GetTimeDilationValue, {}) || !game::ParamsAre(Game.IsIgnoringGlobalTimeDilation, {}))
    {
        Game.GetTimeDilationValue = nullptr;
        Missing += " time dilation";
    }
    if (!game::ParamsAre(Game.GetPSComponentName, {}) || !game::ParamsAre(Game.GetPersistencySystem, {"ScriptGameInstance"}) ||
        !game::ParamsAre(Game.GetConstAccessToPSObject, {"gamePersistentID", "CName"}) || !game::ParamsAre(Game.GetSpawnerID, {}) ||
        !game::ParamsAre(Game.GetOwnerCommunityEntryName, {}))
    {
        Game.GetPSComponentName = nullptr;
        Missing += " match keys";
    }
    if (!game::ParamsAre(Game.GetRecordID, {}))
    {
        Game.GetRecordID = nullptr;
        Missing += " GetRecordID";
    }
    Game.Usable = Game.ScriptedPuppetClass && Game.NpcClass && game::ParamsAre(Game.GetStatPoolsSystem, {"ScriptGameInstance"}) &&
                  game::ParamsAre(Game.GetStatPoolValue, {"gameStatsObjectID", "gamedataStatPoolType", "Bool"});
    return Game.Usable;
}

std::string MissingCalls()
{
    Ready();
    return Missing.empty() ? "none" : Missing.substr(1);
}

bool ReadStates(RED4ext::Handle<RED4ext::IScriptable>& npc, uint8_t (&states)[StateCount])
{
    if (!Ready() || !Game.GetStatesComponent)
    {
        return false;
    }
    auto component = StatesComponentOf(npc);
    if (!component)
    {
        return false;
    }
    RED4ext::StackArgs_t noArgs;
    for (int slot = 0; slot < StateCount; ++slot)
    {
        int32_t value = 0;
        game::CallMethod(component.instance, Game.Getters[slot], &value, noArgs);
        states[slot] = static_cast<uint8_t>(value);
    }
    return true;
}

bool WriteState(RED4ext::Handle<RED4ext::IScriptable>& npc, int slot, uint8_t value)
{
    if (!Ready() || !Game.GetStatesComponent || slot < 0 || slot >= StateCount)
    {
        return false;
    }
    auto component = StatesComponentOf(npc);
    if (!component)
    {
        return false;
    }
    int32_t newState = value;
    bool changed = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &newState);
    return game::CallMethod(component.instance, Game.Setters[slot], &changed, args);
}

RED4ext::Handle<RED4ext::IScriptable> AttitudeAgentOf(RED4ext::Handle<RED4ext::IScriptable>& puppet)
{
    RED4ext::Handle<RED4ext::IScriptable> agent;
    if (!Ready() || !Game.GetAttitudeAgent || !IsA(puppet, Game.PuppetClass))
    {
        return agent;
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(puppet.instance, Game.GetAttitudeAgent, &agent, noArgs);
    return agent;
}

uint8_t AttitudeTowards(RED4ext::Handle<RED4ext::IScriptable>& npcAgent, RED4ext::Handle<RED4ext::IScriptable>& otherAgent)
{
    if (!Ready() || !Game.GetAttitudeAgent || !npcAgent || !otherAgent)
    {
        return AttitudeNeutral;
    }
    uint32_t attitude = AttitudeNeutral;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &otherAgent);
    game::CallMethod(npcAgent.instance, Game.GetAttitudeTowards, &attitude, args);
    return attitude <= 2 ? static_cast<uint8_t>(attitude) : AttitudeNeutral;
}

bool SetAttitudeTowards(RED4ext::Handle<RED4ext::IScriptable>& npcAgent, RED4ext::Handle<RED4ext::IScriptable>& otherAgent, uint8_t attitude)
{
    if (!Ready() || !Game.GetAttitudeAgent || !npcAgent || !otherAgent || attitude > 2)
    {
        return false;
    }
    uint32_t wanted = attitude;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &otherAgent);
    args.emplace_back(nullptr, &wanted);
    return game::CallMethod(npcAgent.instance, Game.SetAttitudeTowards, nullptr, args);
}

uint64_t HandRecord(RED4ext::Handle<RED4ext::IScriptable>& npc, bool rightHand)
{
    auto transactions = items::TransactionSystem();
    if (!transactions || !npc)
    {
        return 0;
    }
    return items::ItemInSlot(transactions, npc, HandSlot(rightHand)).Record;
}

bool PutInHand(RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t record, bool rightHand)
{
    auto transactions = items::TransactionSystem();
    if (!transactions || !npc || !items::RecordExists(record))
    {
        return false;
    }
    const items::ItemId item = items::ItemFromRecord(record);
    if (!items::Has(transactions, npc, item))
    {
        items::Give(transactions, npc, item);
    }
    const uint64_t placementSlot = rightHand ? items::PlacementSlot(record) : 0;
    return items::PutInSlot(transactions, npc, placementSlot ? placementSlot : HandSlot(rightHand), item, true);
}

void EmptyHand(RED4ext::Handle<RED4ext::IScriptable>& npc, bool rightHand)
{
    auto transactions = items::TransactionSystem();
    if (transactions && npc)
    {
        items::EmptySlot(transactions, npc, HandSlot(rightHand));
    }
}

float HealthPercent(uint64_t entityHash)
{
    float health = 100.0f;
    if (!Ready() || !entityHash)
    {
        return health;
    }
    auto pools = StatPools();
    if (!pools)
    {
        return health;
    }
    RED4ext::game::StatsObjectID statsObject {};
    statsObject.entityHash = entityHash;
    statsObject.idType = RED4ext::game::StatIDType::EntityID;
    uint32_t poolType = Game.HealthPool;
    bool inPercent = true;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &statsObject);
    args.emplace_back(nullptr, &poolType);
    args.emplace_back(nullptr, &inPercent);
    game::CallMethod(pools.instance, Game.GetStatPoolValue, &health, args);
    return std::isfinite(health) ? health : 100.0f;
}

bool SetHealthPercent(uint64_t entityHash, float percent)
{
    if (!Ready() || !Game.SetStatPoolValue || !entityHash || !std::isfinite(percent))
    {
        return false;
    }
    auto pools = StatPools();
    if (!pools)
    {
        return false;
    }
    RED4ext::game::StatsObjectID statsObject {};
    statsObject.entityHash = entityHash;
    statsObject.idType = RED4ext::game::StatIDType::EntityID;
    uint32_t poolType = Game.HealthPool;
    float newValue = percent;
    RED4ext::WeakHandle<RED4ext::IScriptable> noInstigator;
    bool inPercent = true;
    bool ignoreCustomLimit = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &statsObject);
    args.emplace_back(nullptr, &poolType);
    args.emplace_back(nullptr, &newValue);
    args.emplace_back(nullptr, &noInstigator);
    args.emplace_back(nullptr, &inPercent);
    args.emplace_back(nullptr, &ignoreCustomLimit);
    return game::CallMethod(pools.instance, Game.SetStatPoolValue, nullptr, args);
}

bool IsIncapacitated(RED4ext::Handle<RED4ext::IScriptable>& npc)
{
    if (!Ready() || !Game.IsIncapacitated || !IsA(npc, Game.NpcClass))
    {
        return false;
    }
    bool incapacitated = false;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(npc.instance, Game.IsIncapacitated, &incapacitated, noArgs);
    return incapacitated;
}

float TimeDilation(RED4ext::Handle<RED4ext::IScriptable>& npc, bool& ignoresGlobal)
{
    ignoresGlobal = false;
    if (!Ready() || !Game.GetTimeDilationValue || !IsA(npc, Game.ScriptedPuppetClass))
    {
        return 1.0f;
    }
    float dilation = 1.0f;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(npc.instance, Game.GetTimeDilationValue, &dilation, noArgs);
    game::CallMethod(npc.instance, Game.IsIgnoringGlobalTimeDilation, &ignoresGlobal, noArgs);
    return std::isfinite(dilation) && dilation > 0.0f ? dilation : 1.0f;
}
}

namespace nightcoop_mac::npc_states
{
namespace
{
struct PersistentIdLayout
{
    uint64_t EntityHash;
    RED4ext::CName ComponentName;
};
static_assert(sizeof(PersistentIdLayout) == 16);
}

bool MatchKeys(uint64_t entityHash, uint64_t& spawnerKey, uint64_t& communityEntryKey)
{
    spawnerKey = 0;
    communityEntryKey = 0;
    if (!Ready() || !Game.GetPSComponentName || !entityHash)
    {
        return false;
    }
    RED4ext::CName componentName;
    RED4ext::StackArgs_t noArgs;
    game::CallStatic(Game.GetPSComponentName, &componentName, noArgs);
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> persistency;
    RED4ext::StackArgs_t systemArgs;
    systemArgs.emplace_back(nullptr, &gameInstance);
    if (!game::CallStatic(Game.GetPersistencySystem, &persistency, systemArgs) || !persistency)
    {
        return false;
    }
    PersistentIdLayout persistentId {entityHash, componentName};
    RED4ext::CName stubClassName("gameEntityStubComponentPS");
    RED4ext::Handle<RED4ext::IScriptable> stub;
    RED4ext::StackArgs_t accessArgs;
    accessArgs.emplace_back(nullptr, &persistentId);
    accessArgs.emplace_back(nullptr, &stubClassName);
    if (!game::CallMethod(persistency.instance, Game.GetConstAccessToPSObject, &stub, accessArgs) || !stub)
    {
        return false;
    }
    RED4ext::ent::EntityID spawner;
    RED4ext::CName communityEntry;
    const bool spawnerRead = game::CallMethod(stub.instance, Game.GetSpawnerID, &spawner, noArgs);
    const bool entryRead = game::CallMethod(stub.instance, Game.GetOwnerCommunityEntryName, &communityEntry, noArgs);
    spawnerKey = spawnerRead ? spawner.hash : 0;
    communityEntryKey = entryRead ? communityEntry.hash : 0;
    return spawnerRead || entryRead;
}

uint64_t RecordOf(RED4ext::Handle<RED4ext::IScriptable>& npc)
{
    if (!Ready() || !Game.GetRecordID || !npc)
    {
        return 0;
    }
    RED4ext::TweakDBID record;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(npc.instance, Game.GetRecordID, &record, noArgs);
    return record.value & 0xFFFFFFFFFFULL;
}
}
