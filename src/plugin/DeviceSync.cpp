/*
 device sync how it works is up in DeviceSync.hpp. the game side calls all go thru NightCoopDeviceSync only the action copy
 and the rebuild of a partners action walk the rtti here
*/
#include "plugin/DeviceSync.hpp"
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include "net/DeviceMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/PartnerRoster.hpp"
#include "plugin/WorldHandoff.hpp"

#include <ProtocolCore.hpp>
#include <cmath>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/PersistentID.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace nightcoop_mac::device_sync
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t SettleBeforeSendingMs = 150;
constexpr uint64_t StreamWaitMs = 30000;
constexpr uint64_t RetryEveryMs = 1000;
constexpr uint64_t KillWaitMs = 300000;
constexpr uint64_t CheckAfterReplayMs = 800;
constexpr uint64_t StepEveryMs = 500;
constexpr int MostQuestSteps = 3;
constexpr uint64_t TurretAskEveryMs = 1000;
constexpr uint64_t TurretWantWindowMs = 3000;
constexpr uint64_t TurretAnswerFreshMs = 3000;
constexpr uint64_t TurretForgetMs = 15000;
constexpr uint64_t QuietBlastMs = 6000;
constexpr uint64_t DeathPipelineWaitMs = 1000;
constexpr uint64_t BlastWaitMs = 300000;
constexpr uint64_t EchoGuardMs = 1500;
constexpr uint64_t DeathRepeatMs = 5000;
constexpr uint64_t RequestForgetMs = 30000;
constexpr uint64_t SummaryEveryMs = 30000;
constexpr int32_t NoPsHere = 127;
constexpr size_t MostWaiting = 256;
constexpr int32_t BlowQuietly = 0;
constexpr int32_t BlowWithDamage = 1;
constexpr int32_t SetExplodedQuietly = 2;

const std::set<std::string> NotSharedActions = {
    "QuickHackExplodeExplosive", "QuickHackDistractExplosive", "ForceDetonate", "ActivateC4", "DeactivateC4", "DetonateC4", "ToggleTakeOverControl",
    "TogglePersonalLink", "ToggleZoomInteraction", "OpenFullscreenUI", "ToggleOpenComputer", "ToggleNetrunnerDive", "ToggleStreamFeed", "ThumbnailUI",
    "OpenStash", "OpenVendorUI", "OpenWardrobeUI", "OpenInteriorManager", "OpenWorldMapDeviceAction", "FastTravelDeviceAction",
    "BeginArcadeMinigameUI", "UseSecurityLocker", "EnterLadder", "CollectDropPointRewards", "ReserveItemToThisDropPoint", "DepositQuestItems",
    "AddItemForPlayerToPickUp", "CraftItemForTarget", "BuyItemFromVendor", "SellItemToVendor", "DispenceItemFromVendor", "ShowVendor", "Pay",
    "DisposeBody", "TakedownAndDisposeBody", "NonlethalTakedownAndDisposeBody", "StartCall", "PingDevice", "RemoteBreach", "AuthorizeUser",
    "ActionScavenge"};

const std::set<std::string> SameActions = {
    "ToggleON", "QuickHackToggleON", "SetDeviceON", "SetDeviceOFF", "TogglePower", "SetDevicePowered", "SetDeviceUnpowered", "ToggleActivation",
    "ToggleActivate", "ActivateDevice", "DeactivateDevice", "QuickHackToggleActivate", "ForceIgnoreTargets", "SetDeviceAttitude",
    "SetDeviceTagKillMode", "ProgramSetDeviceOff", "ProgramSetDeviceAttitude", "RevealEnemiesProgram", "CameraTagSeenEnemies",
    "SecuritySystemForceAttitudeChange", "TakeOverSecuritySystem", "QuickHackDistraction", "QuickHackHighPitchNoise", "Distraction", "GlitchScreen",
    "ToggleJuryrigTrap", "ToggleLight", "ToggleAlarm", "ToggleBlockade", "QuickHackToggleBlockade", "ToggleGlassTint", "ToggleGlassTintHack",
    "ToggleTiltBlinders", "ToggleEffect", "ToggleAOEEffect", "TogglePlay", "NextStation", "PreviousStation", "InitiateTrafficLightChange",
    "TrafficLightGreen", "TrafficLightRed", "FactQuickHack"};

const std::set<std::string> HostOnlyActions = {
    "OverloadDevice", "OverchargeDevice", "QuickHackAoeDamage", "ChestPressWeightHack", "ServerOverload", "E3Hack_QuestPlayAnimationKillNPC",
    "E3Hack_QuestPlayAnimationWeightLift"};

const char* const PsClassesNotShared[] = {
    "DoorControllerPS", "LiftControllerPS", "ElevatorFloorTerminalControllerPS", "VehicleComponentPS", "DisposalDeviceControllerPS",
    "StashControllerPS", "WardrobeControllerPS", "DropPointControllerPS", "DataTermControllerPS"};

const char* const MachinePsClasses[] = {"VendingMachineControllerPS", "ArcadeMachineControllerPS", "SlotMachineControllerPS"};

struct DeviceKey
{
    uint64_t EntityHash = 0;
    uint64_t ComponentName = 0;
    auto operator<=>(const DeviceKey&) const = default;
};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Configure = nullptr;
    RED4ext::CBaseFunction* ClearOutgoing = nullptr;
    RED4ext::CBaseFunction* StateOf = nullptr;
    RED4ext::CBaseFunction* Prepare = nullptr;
    RED4ext::CBaseFunction* Run = nullptr;
    RED4ext::CBaseFunction* StepTo = nullptr;
    RED4ext::CBaseFunction* MarkSkill = nullptr;
    RED4ext::CBaseFunction* MarkQuickHacked = nullptr;
    RED4ext::CBaseFunction* Kill = nullptr;
    RED4ext::CBaseFunction* SetTurretCalm = nullptr;
    RED4ext::CBaseFunction* TurretState = nullptr;
    RED4ext::CBaseFunction* ReevaluateTurret = nullptr;
    RED4ext::CBaseFunction* Blast = nullptr;
    RED4ext::CBaseFunction* EndQuiet = nullptr;
    RED4ext::CBaseFunction* FallbackBlow = nullptr;
    RED4ext::CProperty* DoneActions = nullptr;
    RED4ext::CProperty* DoneIds = nullptr;
    RED4ext::CProperty* DoneClasses = nullptr;
    RED4ext::CProperty* DoneStates = nullptr;
    RED4ext::CProperty* DeadIds = nullptr;
    RED4ext::CProperty* DeadClasses = nullptr;
    RED4ext::CProperty* TurretWants = nullptr;
    RED4ext::CProperty* BlastDevices = nullptr;
    RED4ext::CProperty* BlastPositions = nullptr;
    RED4ext::CProperty* BlastOurs = nullptr;
    RED4ext::CClass* ScriptableDeviceAction = nullptr;
    RED4ext::CClass* DeviceAction = nullptr;
    RED4ext::CClass* ActionProperty = nullptr;
    RED4ext::CProperty* Duration = nullptr;
    RED4ext::CProperty* IsQuickHack = nullptr;
    RED4ext::CProperty* IsTargetDead = nullptr;
    RED4ext::CProperty* ActionProp = nullptr;
    RED4ext::CProperty* PropName = nullptr;
    RED4ext::CProperty* PropTypeName = nullptr;
    RED4ext::CProperty* PropFlags = nullptr;
    RED4ext::CProperty* PropValues[3] = {nullptr, nullptr, nullptr};
    std::vector<RED4ext::CClass*> PsClassesNotShared;
    std::vector<RED4ext::CClass*> MachinePsClasses;
};

struct Context
{
    RED4ext::IScriptable* Service = nullptr;
    net::RelaySession& Session;
    const LogFunction& Log;
    uint64_t NowMs = 0;
    bool WeAreHost = false;
    uint32_t OurPeer = 0;
    std::vector<uint32_t> Partners;
};

struct OurAction
{
    DeviceKey Key;
    net::DeviceSync Message;
    std::string ActionName;
    uint64_t SeenMs = 0;
};

struct DeviceRecord
{
    uint64_t Version = 0;
    uint64_t PsClass = 0;
};

struct FollowJob
{
    DeviceKey Key;
    uint64_t PsClass = 0;
    int8_t Target = net::kDeviceStateUnknown;
    int Steps = 0;
    uint64_t NextMs = 0;
    uint64_t GiveUpMs = 0;
};

struct WaitingWord
{
    net::DeviceSync Message;
    uint64_t NextTryMs = 0;
    uint64_t GiveUpMs = 0;
};

struct PendingKill
{
    DeviceKey Key;
    uint64_t PsClass = 0;
    uint64_t NextTryMs = 0;
    uint64_t GiveUpMs = 0;
};

struct TurretWatch
{
    uint64_t WantedMs = 0;
    uint64_t AskedMs = 0;
    uint8_t Answer = net::kTurretNotThere;
    uint64_t AnswerMs = 0;
    bool LettingGo = false;
};

struct WaitingBlast
{
    net::BlastedDevice Device;
    uint64_t NextTryMs = 0;
    uint64_t GiveUpMs = 0;
};

struct Counters
{
    uint32_t OursSent = 0;
    uint32_t NotShared = 0;
    uint32_t Taken = 0;
    uint32_t Refused = 0;
    uint32_t Followed = 0;
    uint32_t GaveUp = 0;
    uint32_t Stale = 0;
    uint32_t Dropped = 0;
    uint32_t TurretAsks = 0;
    uint32_t TurretAnswers = 0;
    uint32_t BlastsSent = 0;
    uint32_t BlastsTaken = 0;
};

Calls Game;
uint64_t NextTickMs = 0;
bool Shared = false;
bool WeWereHost = false;
uint32_t OurPeerNumber = 0;
std::vector<uint32_t> CurrentPartners;
uint16_t RequestCounter = 0;
std::map<uint16_t, uint64_t> OurOpenRequests;
std::vector<OurAction> OurActions;
std::map<DeviceKey, DeviceRecord> Devices;
std::vector<FollowJob> FollowJobs;
std::vector<WaitingWord> WaitingForStream;
std::vector<PendingKill> PendingKills;
std::map<DeviceKey, uint64_t> EchoGuardUntilMs;
std::map<DeviceKey, uint64_t> DeathSentMs;
std::map<uint64_t, TurretWatch> Turrets;
std::map<uint64_t, std::array<float, 3>> BlownDevices;
std::map<uint64_t, uint64_t> QuietUntilMs;
std::map<uint64_t, uint64_t> FallbackAtMs;
std::vector<WaitingBlast> WaitingBlasts;
std::set<uint32_t> PeersWithSnapshot;
Counters Count;
Counters CountAtSummary;
uint64_t NextSummaryMs = 0;

uint64_t SteadyMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string KeyText(uint64_t key)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(key));
    return text;
}

std::string NameText(uint64_t nameHash)
{
    const char* text = RED4ext::CName(nameHash).ToString();
    return text && *text ? std::string(text) : KeyText(nameHash);
}

std::string StateName(int8_t state)
{
    switch (state)
    {
    case net::kDeviceDisabled:
        return "disabled";
    case net::kDeviceUnpowered:
        return "unpowered";
    case net::kDeviceOff:
        return "off";
    case net::kDeviceOn:
        return "on";
    case 2:
        return "invalid";
    default:
        return "unknown";
    }
}

std::string SkillName(uint8_t skill)
{
    return skill == 1 ? "hacking" : skill == 2 ? "engineering" : skill == 3 ? "demolition" : "skill";
}

std::string DeviceText(const DeviceKey& key, uint64_t psClass)
{
    return NameText(psClass) + " " + KeyText(key.EntityHash);
}

int8_t WireState(int32_t state)
{
    return state >= net::kDeviceDisabled && state <= 2 ? static_cast<int8_t>(state) : net::kDeviceStateUnknown;
}

DeviceKey KeyOf(const net::DeviceSync& message)
{
    return {message.EntityHash, message.ComponentName};
}

RED4ext::CProperty* FindProperty(RED4ext::CClass* owner, RED4ext::CName name)
{
    for (auto* classType = owner; classType; classType = classType->parent)
    {
        for (auto* property : classType->props)
        {
            if (property && property->name == name)
            {
                return property;
            }
        }
    }
    return nullptr;
}

RED4ext::CProperty* TypedProperty(RED4ext::CClass* owner, const char* name, const char* typeName)
{
    auto* property = FindProperty(owner, RED4ext::CName(name));
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName ? property : nullptr;
}

std::vector<RED4ext::CClass*> ClassesNamed(const char* const* names, size_t count)
{
    std::vector<RED4ext::CClass*> classes;
    for (size_t index = 0; index < count; ++index)
    {
        if (auto* classType = RED4ext::CRTTISystem::Get()->GetClass(names[index]))
        {
            classes.push_back(classType);
        }
    }
    return classes;
}

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* syncClass = rtti->GetClass("NightCoopDeviceSync");
    Game.Configure = game::FindFunction("NightCoopDeviceSync", "Configure");
    Game.ClearOutgoing = game::FindFunction("NightCoopDeviceSync", "ClearOutgoing");
    Game.StateOf = game::FindFunction("NightCoopDeviceSync", "StateOf");
    Game.Prepare = game::FindFunction("NightCoopDeviceSync", "Prepare");
    Game.Run = game::FindFunction("NightCoopDeviceSync", "Run");
    Game.StepTo = game::FindFunction("NightCoopDeviceSync", "StepTo");
    Game.MarkSkill = game::FindFunction("NightCoopDeviceSync", "MarkSkill");
    Game.MarkQuickHacked = game::FindFunction("NightCoopDeviceSync", "MarkQuickHacked");
    Game.Kill = game::FindFunction("NightCoopDeviceSync", "Kill");
    Game.SetTurretCalm = game::FindFunction("NightCoopDeviceSync", "SetTurretCalm");
    Game.TurretState = game::FindFunction("NightCoopDeviceSync", "TurretState");
    Game.ReevaluateTurret = game::FindFunction("NightCoopDeviceSync", "ReevaluateTurret");
    Game.Blast = game::FindFunction("NightCoopDeviceSync", "Blast");
    Game.EndQuiet = game::FindFunction("NightCoopDeviceSync", "EndQuiet");
    Game.FallbackBlow = game::FindFunction("NightCoopDeviceSync", "FallbackBlow");
    Game.DoneActions = TypedProperty(syncClass, "doneActions", "array:handle:ScriptableDeviceAction");
    Game.DoneIds = TypedProperty(syncClass, "doneIds", "array:gamePersistentID");
    Game.DoneClasses = TypedProperty(syncClass, "doneClasses", "array:CName");
    Game.DoneStates = TypedProperty(syncClass, "doneStates", "array:Int32");
    Game.DeadIds = TypedProperty(syncClass, "deadIds", "array:gamePersistentID");
    Game.DeadClasses = TypedProperty(syncClass, "deadClasses", "array:CName");
    Game.TurretWants = TypedProperty(syncClass, "turretWants", "array:entEntityID");
    Game.BlastDevices = TypedProperty(syncClass, "blastDevices", "array:entEntityID");
    Game.BlastPositions = TypedProperty(syncClass, "blastPositions", "array:Vector4");
    Game.BlastOurs = TypedProperty(syncClass, "blastOurs", "array:Bool");
    Game.ScriptableDeviceAction = rtti->GetClass("ScriptableDeviceAction");
    Game.DeviceAction = rtti->GetClass("gamedeviceAction");
    Game.ActionProperty = rtti->GetClass("gamedeviceActionProperty");
    Game.Duration = TypedProperty(Game.ScriptableDeviceAction, "duration", "Float");
    Game.IsQuickHack = TypedProperty(Game.ScriptableDeviceAction, "isQuickHack", "Bool");
    Game.IsTargetDead = TypedProperty(Game.ScriptableDeviceAction, "isTargetDead", "Bool");
    Game.ActionProp = FindProperty(Game.ScriptableDeviceAction, RED4ext::CName("prop"));
    if (Game.ActionProp && (!Game.ActionProp->type || Game.ActionProp->type->GetType() != RED4ext::rtti::ERTTIType::Handle))
    {
        Game.ActionProp = nullptr;
    }
    Game.PropName = TypedProperty(Game.ActionProperty, "name", "CName");
    Game.PropTypeName = TypedProperty(Game.ActionProperty, "typeName", "CName");
    Game.PropFlags = FindProperty(Game.ActionProperty, RED4ext::CName("flags"));
    Game.PropValues[0] = TypedProperty(Game.ActionProperty, "first", "Variant");
    Game.PropValues[1] = TypedProperty(Game.ActionProperty, "second", "Variant");
    Game.PropValues[2] = TypedProperty(Game.ActionProperty, "third", "Variant");
    Game.PsClassesNotShared = ClassesNamed(PsClassesNotShared, std::size(PsClassesNotShared));
    Game.MachinePsClasses = ClassesNamed(MachinePsClasses, std::size(MachinePsClasses));
    const bool scriptOk =
        game::ParamsAre(Game.Configure, {"Bool"}) && game::ParamsAre(Game.ClearOutgoing, {}) &&
        game::ParamsAre(Game.StateOf, {"gamePersistentID", "CName"}) && game::ReturnIs(Game.StateOf, "Int32") &&
        game::ParamsAre(Game.Prepare, {"handle:ScriptableDeviceAction", "gamePersistentID", "CName"}) &&
        game::ParamsAre(Game.Run, {"handle:ScriptableDeviceAction", "gamePersistentID", "CName"}) &&
        game::ParamsAre(Game.StepTo, {"gamePersistentID", "CName", "Int32"}) && game::ParamsAre(Game.MarkSkill, {"gamePersistentID", "CName", "Int32"}) &&
        game::ParamsAre(Game.MarkQuickHacked, {"gamePersistentID", "CName", "CName"}) && game::ParamsAre(Game.Kill, {"gamePersistentID", "CName"}) &&
        game::ReturnIs(Game.Kill, "Int32") && game::ParamsAre(Game.SetTurretCalm, {"entEntityID", "Bool"}) &&
        game::ParamsAre(Game.TurretState, {"entEntityID"}) && game::ReturnIs(Game.TurretState, "Int32") &&
        game::ParamsAre(Game.ReevaluateTurret, {"entEntityID"}) &&
        game::ParamsAre(Game.Blast, {"entEntityID", "Float", "Float", "Float", "Int32"}) && game::ReturnIs(Game.Blast, "Int32") &&
        game::ParamsAre(Game.EndQuiet, {"entEntityID"}) && game::ParamsAre(Game.FallbackBlow, {"entEntityID"}) &&
        game::ReturnIs(Game.FallbackBlow, "Bool") && Game.DoneActions && Game.DoneIds && Game.DoneClasses && Game.DoneStates && Game.DeadIds &&
        Game.DeadClasses && Game.TurretWants && Game.BlastDevices && Game.BlastPositions && Game.BlastOurs;
    const bool actionsOk = Game.ScriptableDeviceAction && Game.DeviceAction;
    Game.Usable = scriptOk && actionsOk;
    if (!scriptOk)
    {
        log("devices: no NightCoopDeviceSync script service, evry player keeps their own devices");
    }
    else if (!actionsOk)
    {
        log("devices: no ScriptableDeviceAction / gamedeviceAction here, evry player keeps their own devices");
    }
    else
    {
        log(std::string("devices: shared while a world is shared (cameras, turrets, traps, terminals, barrels") +
            (Game.ActionProp && Game.ActionProperty && Game.PropValues[0] ? ")" : "; action props are left out, gamedeviceActionProperty looks different)"));
    }
    return Game.Usable;
}

RED4ext::game::PersistentID PersistentIdOf(const DeviceKey& key)
{
    RED4ext::game::PersistentID id;
    id.entityID = RED4ext::ent::EntityID(key.EntityHash);
    id.componentName = RED4ext::CName(key.ComponentName);
    return id;
}

bool CallForDevice(const Context& context, RED4ext::CBaseFunction* function, const DeviceKey& key, uint64_t psClass, void* result,
                   void* extraArgument = nullptr)
{
    auto id = PersistentIdOf(key);
    RED4ext::CName className(psClass);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &id);
    args.emplace_back(nullptr, &className);
    if (extraArgument)
    {
        args.emplace_back(nullptr, extraArgument);
    }
    return game::CallMethod(context.Service, function, result, args);
}

bool CallForEntity(const Context& context, RED4ext::CBaseFunction* function, uint64_t entityHash, void* result, void* extraArgument = nullptr)
{
    RED4ext::ent::EntityID entity(entityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entity);
    if (extraArgument)
    {
        args.emplace_back(nullptr, extraArgument);
    }
    return game::CallMethod(context.Service, function, result, args);
}

int32_t StateOf(const Context& context, const DeviceKey& key, uint64_t psClass)
{
    int32_t state = NoPsHere;
    CallForDevice(context, Game.StateOf, key, psClass, &state);
    return state;
}

void StepTo(const Context& context, const DeviceKey& key, uint64_t psClass, int8_t target)
{
    int32_t state = target;
    bool stepped = false;
    CallForDevice(context, Game.StepTo, key, psClass, &stepped, &state);
}

bool ScalarFieldType(RED4ext::rtti::IType* type)
{
    if (!type)
    {
        return false;
    }
    const uint32_t size = type->GetSize();
    if (size == 0 || size > net::kMostDeviceValueBytes)
    {
        return false;
    }
    switch (type->GetType())
    {
    case RED4ext::rtti::ERTTIType::Name:
    case RED4ext::rtti::ERTTIType::Fundamental:
    case RED4ext::rtti::ERTTIType::Enum:
        return true;
    case RED4ext::rtti::ERTTIType::Class:
        return type->GetName() == RED4ext::CName("entEntityID");
    case RED4ext::rtti::ERTTIType::Simple:
        return type->GetName() == RED4ext::CName("TweakDBID");
    default:
        return false;
    }
}

void CopyAction(RED4ext::IScriptable* action, net::DeviceSync& message)
{
    auto* actionClass = action->GetType();
    message.ActionClass = actionClass->GetName().hash;
    for (auto* classType = actionClass; classType && message.Fields.size() < net::kMostDeviceFields; classType = classType->parent)
    {
        for (auto* property : classType->props)
        {
            if (message.Fields.size() >= net::kMostDeviceFields)
            {
                break;
            }
            if (!property || !ScalarFieldType(property->type))
            {
                continue;
            }
            const auto* bytes = property->GetValuePtr<uint8_t>(action);
            message.Fields.push_back({property->name.hash, property->type->GetName().hash, {bytes, bytes + property->type->GetSize()}});
        }
        if (classType == Game.DeviceAction)
        {
            break;
        }
    }
    message.Duration = Game.Duration ? *Game.Duration->GetValuePtr<float>(action) : 0.0f;
    if (!(message.Duration > 0.0f && message.Duration < 3600.0f))
    {
        message.Duration = 0.0f;
    }
    if (Game.IsQuickHack && *Game.IsQuickHack->GetValuePtr<bool>(action))
    {
        message.Flags |= net::kDeviceQuickHack;
    }
    if (!Game.ActionProp)
    {
        return;
    }
    const auto& prop = *Game.ActionProp->GetValuePtr<RED4ext::Handle<RED4ext::IScriptable>>(action);
    if (!prop || !Game.ActionProperty || !prop->GetType() || !prop->GetType()->IsA(Game.ActionProperty))
    {
        return;
    }
    message.HasProp = true;
    message.PropName = Game.PropName ? Game.PropName->GetValuePtr<RED4ext::CName>(prop.instance)->hash : 0;
    message.PropTypeName = Game.PropTypeName ? Game.PropTypeName->GetValuePtr<RED4ext::CName>(prop.instance)->hash : 0;
    message.PropFlags = Game.PropFlags && Game.PropFlags->type && Game.PropFlags->type->GetSize() ? *Game.PropFlags->GetValuePtr<uint8_t>(prop.instance) : 0;
    for (uint64_t index = 0; index < 3; ++index)
    {
        if (!Game.PropValues[index])
        {
            continue;
        }
        const auto* variant = Game.PropValues[index]->GetValuePtr<RED4ext::Variant>(prop.instance);
        auto* valueType = variant->GetType();
        const auto* bytes = static_cast<const uint8_t*>(variant->GetDataPtr());
        if (ScalarFieldType(valueType) && bytes)
        {
            message.PropValues.push_back({index, valueType->GetName().hash, {bytes, bytes + valueType->GetSize()}});
        }
    }
}

void WriteScalar(RED4ext::rtti::IType* type, void* target, const std::vector<uint8_t>& bytes)
{
    if (type->GetName() == RED4ext::CName("Bool"))
    {
        *static_cast<uint8_t*>(target) = bytes[0] != 0 ? 1 : 0;
        return;
    }
    std::memcpy(target, bytes.data(), bytes.size());
}

void AttachProp(RED4ext::IScriptable* action, const net::DeviceSync& message)
{
    if (!message.HasProp || !Game.ActionProperty || !Game.ActionProp)
    {
        return;
    }
    auto* prop = static_cast<RED4ext::IScriptable*>(Game.ActionProperty->CreateInstance(true));
    if (!prop)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> propHandle(prop);
    if (Game.PropName)
    {
        *Game.PropName->GetValuePtr<RED4ext::CName>(prop) = RED4ext::CName(message.PropName);
    }
    if (Game.PropTypeName)
    {
        *Game.PropTypeName->GetValuePtr<RED4ext::CName>(prop) = RED4ext::CName(message.PropTypeName);
    }
    if (Game.PropFlags && Game.PropFlags->type && Game.PropFlags->type->GetSize())
    {
        *Game.PropFlags->GetValuePtr<uint8_t>(prop) = message.PropFlags;
    }
    for (const auto& value : message.PropValues)
    {
        auto* property = value.Index < 3 ? Game.PropValues[value.Index] : nullptr;
        auto* valueType = RED4ext::CRTTISystem::Get()->GetType(RED4ext::CName(value.TypeName));
        if (!property || !ScalarFieldType(valueType) || valueType->GetSize() != value.Bytes.size())
        {
            continue;
        }
        uint8_t buffer[net::kMostDeviceValueBytes] = {};
        WriteScalar(valueType, buffer, value.Bytes);
        property->GetValuePtr<RED4ext::Variant>(prop)->Fill(valueType, buffer);
    }
    *Game.ActionProp->GetValuePtr<RED4ext::Handle<RED4ext::IScriptable>>(action) = propHandle;
}

bool Replay(const Context& context, const net::DeviceSync& message, std::string& problem)
{
    auto* actionClass = RED4ext::CRTTISystem::Get()->GetClass(RED4ext::CName(message.ActionClass));
    if (!actionClass || !actionClass->IsA(Game.ScriptableDeviceAction) || actionClass->flags.isAbstract)
    {
        problem = "no such action class here";
        return false;
    }
    auto* action = static_cast<RED4ext::IScriptable*>(actionClass->CreateInstance(true));
    if (!action)
    {
        problem = "the action could not be made";
        return false;
    }
    RED4ext::Handle<RED4ext::IScriptable> actionHandle(action);
    const DeviceKey key = KeyOf(message);
    bool ready = false;
    auto id = PersistentIdOf(key);
    RED4ext::CName className(message.PsClass);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &actionHandle);
    args.emplace_back(nullptr, &id);
    args.emplace_back(nullptr, &className);
    if (!game::CallMethod(context.Service, Game.Prepare, &ready, args) || !ready)
    {
        problem = "its ps is not here";
        return false;
    }
    uint32_t written = 0;
    for (const auto& field : message.Fields)
    {
        auto* property = FindProperty(actionClass, RED4ext::CName(field.Name));
        if (!property || !ScalarFieldType(property->type) || property->type->GetSize() != field.Bytes.size() ||
            property->type->GetName().hash != field.TypeName)
        {
            continue;
        }
        WriteScalar(property->type, property->GetValuePtr<uint8_t>(action), field.Bytes);
        ++written;
    }
    if (written == 0)
    {
        problem = "none of its fields fit here";
        return false;
    }
    AttachProp(action, message);
    bool ran = false;
    if (!game::CallMethod(context.Service, Game.Run, &ran, args) || !ran)
    {
        problem = "ExecutePSAction failed";
        return false;
    }
    EchoGuardUntilMs[key] = context.NowMs + EchoGuardMs;
    return true;
}

uint64_t ActionNameOf(const net::DeviceSync& message)
{
    const uint64_t actionNameField = RED4ext::CName("actionName").hash;
    for (const auto& field : message.Fields)
    {
        if (field.Name == actionNameField && field.Bytes.size() == sizeof(uint64_t))
        {
            uint64_t name = 0;
            std::memcpy(&name, field.Bytes.data(), sizeof(name));
            return name;
        }
    }
    return message.ActionClass;
}

void MarkQuickHacked(const Context& context, const net::DeviceSync& message)
{
    RED4ext::CName name(ActionNameOf(message));
    bool marked = false;
    CallForDevice(context, Game.MarkQuickHacked, KeyOf(message), message.PsClass, &marked, &name);
}

bool PsClassIsOneOf(uint64_t psClass, const std::vector<RED4ext::CClass*>& classes)
{
    auto* psType = RED4ext::CRTTISystem::Get()->GetClass(RED4ext::CName(psClass));
    return psType && std::any_of(classes.begin(), classes.end(), [psType](RED4ext::CClass* listed) { return psType->IsA(listed); });
}

uint8_t Classify(const std::string& actionName, uint64_t psClass, uint8_t& skill)
{
    skill = 0;
    if (PsClassIsOneOf(psClass, Game.MachinePsClasses))
    {
        return actionName == "QuickHackDistraction" || actionName == "GlitchScreen" ? net::kDeviceSameAction : net::kDeviceNotShared;
    }
    if (actionName.empty() || NotSharedActions.count(actionName) || actionName.rfind("Spiderbot", 0) == 0 || actionName.rfind("Vehicle", 0) == 0 ||
        PsClassIsOneOf(psClass, Game.PsClassesNotShared))
    {
        return net::kDeviceNotShared;
    }
    if (actionName == "ActionHacking" || actionName == "ActionEngineering" || actionName == "ActionDemolition")
    {
        skill = actionName == "ActionHacking" ? 1 : actionName == "ActionEngineering" ? 2 : 3;
        return net::kDeviceSkillCheck;
    }
    if (SameActions.count(actionName))
    {
        return net::kDeviceSameAction;
    }
    if (HostOnlyActions.count(actionName))
    {
        return net::kDeviceSameActionHostOnly;
    }
    return net::kDeviceStateOnly;
}

uint64_t BumpVersion(const Context& context, DeviceRecord& record)
{
    record.Version = std::max(record.Version + 1, context.Session.SessionClockMs());
    return record.Version;
}

uint8_t StateFlags(bool timed, int8_t after)
{
    return (timed ? net::kDeviceTimed : 0) | (!timed && net::DeviceStateKnown(after) ? net::kDeviceLasting : 0);
}

uint32_t SendToPartners(const Context& context, const net::DeviceSync& message, uint32_t skipPeer)
{
    const auto bytes = net::WriteDeviceSync(message);
    uint32_t sent = 0;
    for (uint32_t peer : context.Partners)
    {
        if (peer != skipPeer && peer != context.OurPeer)
        {
            context.Session.SendGameMessage(bytes, static_cast<int32_t>(peer));
            ++sent;
        }
    }
    return sent;
}

void Follow(const Context& context, const DeviceKey& key, uint64_t psClass, int8_t target, uint64_t firstCheckDelayMs)
{
    std::erase_if(FollowJobs, [&key](const FollowJob& job) { return job.Key == key; });
    FollowJobs.push_back({key, psClass, target, 0, context.NowMs + firstCheckDelayMs, context.NowMs + StreamWaitMs});
}

std::string Kill(const Context& context, const DeviceKey& key, uint64_t psClass)
{
    int32_t result = 3;
    CallForDevice(context, Game.Kill, key, psClass, &result);
    if (result == 0)
    {
        return "destroyed here too (Device.Die)";
    }
    if (result == 1)
    {
        return "destroyed here already";
    }
    std::erase_if(PendingKills, [&key](const PendingKill& kill) { return kill.Key == key; });
    PendingKills.push_back({key, psClass, context.NowMs + RetryEveryMs, context.NowMs + KillWaitMs});
    return result == 2 ? "not in our stream: disabled by its quest action, destroyed when it streams in"
                       : "its ps is not here yet, destroyed once it shows up";
}

std::string Apply(const Context& context, const net::DeviceSync& message, int8_t ours)
{
    const DeviceKey key = KeyOf(message);
    const bool timed = (message.Flags & net::kDeviceTimed) != 0;
    const bool afterKnown = net::DeviceStateKnown(message.StateAfter);
    const std::string actionName = NameText(message.ActionClass);
    std::string what;
    if (message.ActionKind == net::kDeviceSameAction || message.ActionKind == net::kDeviceSameActionHostOnly)
    {
        const bool togglesFromAnotherState = net::DeviceStateKnown(message.StateBefore) && message.StateBefore != message.StateAfter &&
                                             net::DeviceStateKnown(ours) && ours != message.StateBefore;
        std::string problem;
        if (togglesFromAnotherState && afterKnown)
        {
            Follow(context, key, message.PsClass, message.StateAfter, 0);
            what = "ours was " + StateName(ours) + " not " + StateName(message.StateBefore) + ", so " + actionName + " is not repeated, forced " +
                   StateName(message.StateAfter);
        }
        else if (Replay(context, message, problem))
        {
            if (!timed && afterKnown)
            {
                Follow(context, key, message.PsClass, message.StateAfter, CheckAfterReplayMs);
            }
            what = "the same " + actionName + " here";
        }
        else
        {
            if (afterKnown && message.StateAfter != ours)
            {
                Follow(context, key, message.PsClass, message.StateAfter, 0);
            }
            what = "the same " + actionName + " could not be made here (" + problem + ")";
        }
    }
    else if (message.ActionKind == net::kDeviceSkillCheck)
    {
        bool marked = false;
        int32_t skill = message.Skill;
        CallForDevice(context, Game.MarkSkill, key, message.PsClass, &marked, &skill);
        if (afterKnown && message.StateAfter != ours)
        {
            Follow(context, key, message.PsClass, message.StateAfter, 0);
        }
        what = marked ? "its " + SkillName(message.Skill) + " check is passed here too" : "its skill check could not be marked here";
    }
    else if (message.ActionKind == net::kDeviceDestroyed)
    {
        return Kill(context, key, message.PsClass);
    }
    else if (afterKnown && message.StateAfter != ours)
    {
        Follow(context, key, message.PsClass, message.StateAfter, 0);
        what = "ours " + StateName(ours) + " -> " + StateName(message.StateAfter);
    }
    else
    {
        what = "already so";
    }
    if ((message.Flags & net::kDeviceQuickHack) && (message.ActionKind == net::kDeviceStateOnly || message.ActionKind == net::kDeviceSkillCheck))
    {
        MarkQuickHacked(context, message);
        what += "; marked as quickhacked";
    }
    return what;
}

uint16_t NextRequestNumber(uint64_t nowMs)
{
    if (RequestCounter == 0)
    {
        RequestCounter = static_cast<uint16_t>(nowMs * 2654435761ULL);
    }
    if (++RequestCounter == 0)
    {
        ++RequestCounter;
    }
    return RequestCounter;
}

void SendOurAction(const Context& context, OurAction& ours)
{
    auto& message = ours.Message;
    message.EntityHash = ours.Key.EntityHash;
    message.ComponentName = ours.Key.ComponentName;
    message.StateAfter = WireState(StateOf(context, ours.Key, message.PsClass));
    const bool timed = message.ActionKind < net::kDeviceSameActionHostOnly && message.Duration > 0.0f;
    const bool quickHack = (message.Flags & net::kDeviceQuickHack) != 0;
    const bool changed = net::DeviceStateKnown(message.StateAfter) && message.StateAfter != message.StateBefore;
    if (message.ActionKind == net::kDeviceStateOnly && !changed && !quickHack)
    {
        return;
    }
    message.MsSinceAction = static_cast<uint32_t>(std::min<uint64_t>(context.NowMs - ours.SeenMs, 0xFFFFFFFFULL));
    message.Flags = (quickHack ? net::kDeviceQuickHack : 0) | (timed ? net::kDeviceTimed : 0);
    message.Peer = context.OurPeer;
    auto& record = Devices[ours.Key];
    record.PsClass = message.PsClass;
    const std::string what = "devices: our " + ours.ActionName + " on " + DeviceText(ours.Key, message.PsClass) + " (" + StateName(message.StateBefore) +
                             " -> " + StateName(message.StateAfter) + (timed ? ", timed" : "") + (quickHack ? ", quickhack" : "") + ")";
    if (!context.WeAreHost)
    {
        message.Kind = net::kDeviceRequest;
        message.Version = record.Version;
        message.RequestNumber = NextRequestNumber(context.NowMs);
        OurOpenRequests[message.RequestNumber] = context.NowMs;
        context.Session.SendGameMessage(net::WriteDeviceSync(message), 0);
        ++Count.OursSent;
        context.Log(what + " told the host (#" + std::to_string(message.RequestNumber) + ")");
        return;
    }
    if (message.ActionKind == net::kDeviceSameAction || message.ActionKind == net::kDeviceSkillCheck || message.ActionKind == net::kDeviceDestroyed)
    {
        message.Kind = net::kDeviceReplay;
    }
    else
    {
        if (!changed || timed)
        {
            return;
        }
        message.Kind = net::kDeviceAnswer;
        message.ActionKind = net::kDeviceStateOnly;
        message.Skill = 0;
        message.Fields.clear();
        message.PropValues.clear();
        message.HasProp = false;
    }
    message.Flags |= StateFlags(timed, message.StateAfter) & net::kDeviceLasting;
    message.Version = BumpVersion(context, record);
    message.RequestNumber = 0;
    const uint32_t guests = SendToPartners(context, message, context.OurPeer);
    ++Count.OursSent;
    context.Log(what + " everybody gets it (version " + std::to_string(message.Version) + ", " + std::to_string(guests) + " guest(s))");
}

void TakeOurActions(const Context& context)
{
    const auto* actions = Game.DoneActions->GetValuePtr<RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>>>(context.Service);
    const auto* ids = Game.DoneIds->GetValuePtr<RED4ext::DynArray<RED4ext::game::PersistentID>>(context.Service);
    const auto* classes = Game.DoneClasses->GetValuePtr<RED4ext::DynArray<RED4ext::CName>>(context.Service);
    const auto* states = Game.DoneStates->GetValuePtr<RED4ext::DynArray<int32_t>>(context.Service);
    const uint32_t count = std::min({actions->Size(), ids->Size(), classes->Size(), states->Size()});
    for (uint32_t index = 0; index < count; ++index)
    {
        const auto& action = (*actions)[index];
        if (!action || !action->GetType() || (Game.IsTargetDead && *Game.IsTargetDead->GetValuePtr<bool>(action.instance)))
        {
            continue;
        }
        OurAction ours;
        ours.Key = {(*ids)[index].entityID.hash, (*ids)[index].componentName.hash};
        ours.Message.PsClass = (*classes)[index].hash;
        ours.Message.StateBefore = WireState((*states)[index]);
        ours.ActionName = NameText(action->GetType()->GetName().hash);
        ours.Message.ActionKind = Classify(ours.ActionName, ours.Message.PsClass, ours.Message.Skill);
        if (ours.Message.ActionKind == net::kDeviceNotShared)
        {
            ++Count.NotShared;
            continue;
        }
        const auto guard = EchoGuardUntilMs.find(ours.Key);
        if (ours.Key.EntityHash == 0 || ours.Message.PsClass == 0 || (guard != EchoGuardUntilMs.end() && context.NowMs < guard->second))
        {
            continue;
        }
        CopyAction(action.instance, ours.Message);
        ours.SeenMs = context.NowMs;
        if (OurActions.size() < MostWaiting)
        {
            OurActions.push_back(std::move(ours));
        }
    }
}

void TakeOurDeaths(const Context& context)
{
    const auto* ids = Game.DeadIds->GetValuePtr<RED4ext::DynArray<RED4ext::game::PersistentID>>(context.Service);
    const auto* classes = Game.DeadClasses->GetValuePtr<RED4ext::DynArray<RED4ext::CName>>(context.Service);
    const uint32_t count = std::min(ids->Size(), classes->Size());
    for (uint32_t index = 0; index < count; ++index)
    {
        OurAction death;
        death.Key = {(*ids)[index].entityID.hash, (*ids)[index].componentName.hash};
        auto& sentMs = DeathSentMs[death.Key];
        if (death.Key.EntityHash == 0 || (sentMs && context.NowMs - sentMs < DeathRepeatMs))
        {
            continue;
        }
        sentMs = context.NowMs;
        death.Message.PsClass = (*classes)[index].hash;
        death.Message.ActionKind = net::kDeviceDestroyed;
        death.ActionName = "destruction";
        death.SeenMs = context.NowMs;
        if (OurActions.size() < MostWaiting)
        {
            OurActions.push_back(std::move(death));
        }
    }
}

void SendSettledActions(const Context& context)
{
    for (auto ours = OurActions.begin(); ours != OurActions.end();)
    {
        if (context.NowMs - ours->SeenMs < SettleBeforeSendingMs)
        {
            ++ours;
            continue;
        }
        SendOurAction(context, *ours);
        ours = OurActions.erase(ours);
    }
}

void TakeGuestAction(const Context& context, uint32_t fromPeer, const net::DeviceSync& message)
{
    const DeviceKey key = KeyOf(message);
    auto& record = Devices[key];
    record.PsClass = message.PsClass;
    const int32_t oursRaw = StateOf(context, key, message.PsClass);
    const int8_t ours = WireState(oursRaw);
    const bool timed = (message.Flags & net::kDeviceTimed) != 0;
    bool taken = false;
    std::string verdict;
    if (oursRaw == NoPsHere)
    {
        verdict = "their state is unknown (no such device here)";
    }
    else if (ours == net::kDeviceDisabled && message.ActionKind != net::kDeviceDestroyed)
    {
        verdict = "refused: disabled here (a quest or destroyed)";
    }
    else if (ours == net::kDeviceUnpowered && message.ActionKind != net::kDeviceDestroyed && message.StateBefore != net::kDeviceUnpowered)
    {
        verdict = "refused: no power here";
    }
    else
    {
        verdict = "taken, " + Apply(context, message, ours);
        taken = true;
    }
    const int8_t after = taken && net::DeviceStateKnown(message.StateAfter) ? message.StateAfter : ours;
    net::DeviceSync answer;
    answer.Kind = net::kDeviceAnswer;
    answer.Peer = fromPeer;
    answer.EntityHash = message.EntityHash;
    answer.ComponentName = message.ComponentName;
    answer.PsClass = message.PsClass;
    answer.StateBefore = ours;
    answer.StateAfter = after;
    answer.Flags = StateFlags(timed, after);
    answer.Version = BumpVersion(context, record);
    answer.RequestNumber = message.RequestNumber;
    answer.MsSinceAction = message.MsSinceAction;
    answer.ActionClass = message.ActionClass;
    context.Session.SendGameMessage(net::WriteDeviceSync(answer), static_cast<int32_t>(fromPeer));
    uint32_t others = 0;
    if (taken && (message.ActionKind == net::kDeviceSameAction || message.ActionKind == net::kDeviceSkillCheck || message.ActionKind == net::kDeviceDestroyed))
    {
        net::DeviceSync replay = message;
        replay.Kind = net::kDeviceReplay;
        replay.Version = answer.Version;
        replay.RequestNumber = 0;
        replay.StateAfter = after;
        replay.Flags = (message.Flags & net::kDeviceQuickHack) | StateFlags(timed, after);
        others = SendToPartners(context, replay, fromPeer);
    }
    else if (taken && after != ours && net::DeviceStateKnown(after) && !timed)
    {
        net::DeviceSync state = answer;
        state.RequestNumber = 0;
        others = SendToPartners(context, state, fromPeer);
    }
    taken ? ++Count.Taken : ++Count.Refused;
    context.Log("devices: peer " + std::to_string(fromPeer) + " did " + NameText(message.ActionClass) + " on " + DeviceText(key, message.PsClass) + " (" +
                StateName(message.StateBefore) + " -> " + StateName(message.StateAfter) + "): " + verdict + ", ours " + StateName(ours) + " -> " +
                StateName(after) + " (version " + std::to_string(answer.Version) + (others ? ", " + std::to_string(others) + " other guest(s)" : "") + ")");
}

void FollowHost(const Context& context, const net::DeviceSync& message, int8_t ours, bool answersUs)
{
    const DeviceKey key = KeyOf(message);
    std::string what;
    if (message.Kind == net::kDeviceReplay)
    {
        what = Apply(context, message, ours);
    }
    else if (net::DeviceStateKnown(message.StateAfter) && message.StateAfter != ours)
    {
        Follow(context, key, message.PsClass, message.StateAfter, 0);
        what = "ours " + StateName(ours) + " -> " + StateName(message.StateAfter);
    }
    else
    {
        if (answersUs)
        {
            return;
        }
        what = "already so";
    }
    if (message.Kind == net::kDeviceAnswer && (message.Flags & net::kDeviceQuickHack) && !answersUs)
    {
        MarkQuickHacked(context, message);
        what += "; marked as quickhacked";
    }
    ++Count.Followed;
    context.Log("devices: " + std::string(answersUs ? "the host answered our " : "the hosts ") + NameText(message.ActionClass) + " on " +
                DeviceText(key, message.PsClass) + " (peer " + std::to_string(message.Peer) + "): " + what + " (version " +
                std::to_string(message.Version) + ")");
}

void TakeHostWord(const Context& context, const net::DeviceSync& message)
{
    const DeviceKey key = KeyOf(message);
    auto& record = Devices[key];
    const bool answersUs = message.Kind == net::kDeviceAnswer && message.RequestNumber != 0 && OurOpenRequests.erase(message.RequestNumber) > 0;
    if (message.Version <= record.Version && !answersUs)
    {
        ++Count.Stale;
        return;
    }
    record.Version = std::max(record.Version, message.Version);
    record.PsClass = message.PsClass;
    const int32_t oursRaw = StateOf(context, key, message.PsClass);
    if (oursRaw == NoPsHere)
    {
        if (WaitingForStream.size() < MostWaiting)
        {
            WaitingForStream.push_back({message, context.NowMs + RetryEveryMs, context.NowMs + StreamWaitMs});
        }
        return;
    }
    FollowHost(context, message, WireState(oursRaw), answersUs);
}

void ServeFollowJobs(const Context& context)
{
    for (auto job = FollowJobs.begin(); job != FollowJobs.end();)
    {
        if (context.NowMs < job->NextMs)
        {
            ++job;
            continue;
        }
        const int32_t state = StateOf(context, job->Key, job->PsClass);
        if (state == job->Target)
        {
            job = FollowJobs.erase(job);
            continue;
        }
        if (state == NoPsHere)
        {
            if (context.NowMs > job->GiveUpMs)
            {
                context.Log("devices: " + DeviceText(job->Key, job->PsClass) + " its ps did not come into our world in time");
                job = FollowJobs.erase(job);
                continue;
            }
            job->NextMs = context.NowMs + RetryEveryMs;
            ++job;
            continue;
        }
        if (job->Steps >= MostQuestSteps)
        {
            ++Count.GaveUp;
            context.Log("devices: " + DeviceText(job->Key, job->PsClass) + " ours stays " + StateName(WireState(state)) +
                        " (our quest or power holds it; theirs is " + StateName(job->Target) + "), gave up after " + std::to_string(job->Steps) +
                        " quest step(s)");
            job = FollowJobs.erase(job);
            continue;
        }
        StepTo(context, job->Key, job->PsClass, job->Target);
        ++job->Steps;
        job->NextMs = context.NowMs + StepEveryMs;
        ++job;
    }
}

void ServeWaitingForStream(const Context& context)
{
    for (auto waiting = WaitingForStream.begin(); waiting != WaitingForStream.end();)
    {
        if (context.NowMs < waiting->NextTryMs)
        {
            ++waiting;
            continue;
        }
        const DeviceKey key = KeyOf(waiting->Message);
        const int32_t state = StateOf(context, key, waiting->Message.PsClass);
        if (state != NoPsHere)
        {
            context.Log("devices: " + DeviceText(key, waiting->Message.PsClass) + " came into our world, following the host now");
            FollowHost(context, waiting->Message, WireState(state), false);
            waiting = WaitingForStream.erase(waiting);
        }
        else if (context.NowMs > waiting->GiveUpMs)
        {
            context.Log("devices: " + DeviceText(key, waiting->Message.PsClass) + " waited " + std::to_string(StreamWaitMs / 1000) +
                        " s for it to stream in, in vain");
            waiting = WaitingForStream.erase(waiting);
        }
        else
        {
            waiting->NextTryMs = context.NowMs + RetryEveryMs;
            ++waiting;
        }
    }
}

void ServePendingKills(const Context& context)
{
    for (auto kill = PendingKills.begin(); kill != PendingKills.end();)
    {
        if (context.NowMs < kill->NextTryMs)
        {
            ++kill;
            continue;
        }
        int32_t result = 3;
        CallForDevice(context, Game.Kill, kill->Key, kill->PsClass, &result);
        if (result == 0 || result == 1)
        {
            context.Log("devices: " + DeviceText(kill->Key, kill->PsClass) + (result == 0 ? " streamed in and got destroyed, as a partners player did"
                                                                                           : " is destroyed here now"));
            kill = PendingKills.erase(kill);
        }
        else if (context.NowMs > kill->GiveUpMs)
        {
            kill = PendingKills.erase(kill);
        }
        else
        {
            kill->NextTryMs = context.NowMs + RetryEveryMs;
            ++kill;
        }
    }
}

void LetTurretGo(const Context& context, uint64_t turretKey, TurretWatch& turret, bool letGo, bool reevaluate)
{
    if (letGo == turret.LettingGo)
    {
        return;
    }
    turret.LettingGo = letGo;
    bool calm = letGo;
    CallForEntity(context, Game.SetTurretCalm, turretKey, nullptr, &calm);
    if (reevaluate)
    {
        bool found = false;
        CallForEntity(context, Game.ReevaluateTurret, turretKey, &found);
    }
}

void TakeTurretWants(const Context& context)
{
    const auto* wants = Game.TurretWants->GetValuePtr<RED4ext::DynArray<RED4ext::ent::EntityID>>(context.Service);
    for (const auto& turret : *wants)
    {
        if (turret.hash)
        {
            Turrets[turret.hash].WantedMs = context.NowMs;
        }
    }
}

void ServeTurrets(const Context& context)
{
    if (context.WeAreHost)
    {
        Turrets.clear();
        return;
    }
    for (auto entry = Turrets.begin(); entry != Turrets.end();)
    {
        auto& turret = entry->second;
        if (context.NowMs - turret.WantedMs <= TurretWantWindowMs && context.NowMs - turret.AskedMs >= TurretAskEveryMs)
        {
            context.Session.SendGameMessage(net::WriteTurretWord({net::kTurretAsk, entry->first, net::kTurretNotThere}), 0);
            turret.AskedMs = context.NowMs;
            ++Count.TurretAsks;
        }
        const bool letGo = turret.Answer == net::kTurretCalm && context.NowMs - turret.AnswerMs <= TurretAnswerFreshMs;
        LetTurretGo(context, entry->first, turret, letGo, false);
        if (!letGo && context.NowMs - turret.WantedMs > TurretForgetMs && context.NowMs - turret.AnswerMs > TurretForgetMs)
        {
            entry = Turrets.erase(entry);
            continue;
        }
        ++entry;
    }
}

void TakeTurretWord(const Context& context, uint32_t fromPeer, const net::TurretWord& word)
{
    if (context.WeAreHost && word.Kind == net::kTurretAsk)
    {
        int32_t state = net::kTurretNotThere;
        CallForEntity(context, Game.TurretState, word.TurretKey, &state);
        const auto answer = static_cast<uint8_t>(std::clamp<int32_t>(state, net::kTurretNotThere, net::kTurretInCombat));
        context.Session.SendGameMessage(net::WriteTurretWord({net::kTurretAnswer, word.TurretKey, answer}), static_cast<int32_t>(fromPeer));
        if (Count.TurretAnswers++ < 20)
        {
            context.Log("devices: peer " + std::to_string(fromPeer) + " asks about turret " + KeyText(word.TurretKey) + " (its own one takes its player as a target): here it is " +
                        (answer == net::kTurretInCombat ? "in combat (it has a target)" : answer == net::kTurretCalm ? "calm (no target)" : "not in the hosts world"));
        }
        return;
    }
    if (context.WeAreHost || fromPeer != 0 || word.Kind != net::kTurretAnswer)
    {
        ++Count.Dropped;
        return;
    }
    auto& turret = Turrets[word.TurretKey];
    turret.Answer = word.State;
    turret.AnswerMs = context.NowMs;
    const bool letGo = word.State == net::kTurretCalm;
    if (letGo != turret.LettingGo)
    {
        context.Log("devices: turret " + KeyText(word.TurretKey) +
                    (letGo ? " holds our player while the hosts same turret is calm: it lets our player go"
                           : " let our player go before, and the hosts same turret is not calm now: it may take it back"));
    }
    LetTurretGo(context, word.TurretKey, turret, letGo, true);
}

void SendBlasts(const Context& context, uint8_t kind, std::vector<net::BlastedDevice> devices, int32_t toPeer)
{
    while (!devices.empty())
    {
        const size_t count = std::min(devices.size(), net::kMostBlastsPerMessage);
        net::DeviceBlast blast;
        blast.Kind = kind;
        blast.Devices.assign(devices.begin(), devices.begin() + static_cast<std::ptrdiff_t>(count));
        devices.erase(devices.begin(), devices.begin() + static_cast<std::ptrdiff_t>(count));
        context.Session.SendGameMessage(net::WriteDeviceBlast(blast), toPeer);
    }
}

void TakeOurBlasts(const Context& context)
{
    const auto* devices = Game.BlastDevices->GetValuePtr<RED4ext::DynArray<RED4ext::ent::EntityID>>(context.Service);
    const auto* positions = Game.BlastPositions->GetValuePtr<RED4ext::DynArray<RED4ext::Vector4>>(context.Service);
    const auto* ours = Game.BlastOurs->GetValuePtr<RED4ext::DynArray<bool>>(context.Service);
    const uint32_t count = std::min({devices->Size(), positions->Size(), ours->Size()});
    std::vector<net::BlastedDevice> live;
    for (uint32_t index = 0; index < count; ++index)
    {
        const uint64_t key = (*devices)[index].hash;
        const auto& position = (*positions)[index];
        if (BlownDevices.count(key))
        {
            continue;
        }
        if (!net::BlastKeyIsShared(key))
        {
            context.Log("barrels: " + KeyText(key) + " blew here but it is a spawned device, its id differs on evry machine");
            continue;
        }
        BlownDevices[key] = {position.X, position.Y, position.Z};
        live.push_back({key, {position.X, position.Y, position.Z}, static_cast<uint8_t>((*ours)[index] ? 1 : 0)});
        context.Log("barrels: " + KeyText(key) + " blew here" + ((*ours)[index] ? " by our player" : "") + ", everybody gets it");
    }
    Count.BlastsSent += static_cast<uint32_t>(live.size());
    SendBlasts(context, net::kBlastLive, std::move(live), net::kEveryPeer);
}

void TakeBlast(const Context& context, uint32_t fromPeer, const net::DeviceBlast& blast)
{
    for (const auto& device : blast.Devices)
    {
        ++Count.BlastsTaken;
        if (blast.Kind == net::kBlastLive && BlownDevices.count(device.DeviceKey))
        {
            context.Log("barrels: peer " + std::to_string(fromPeer) + " and we both blew " + KeyText(device.DeviceKey) + ", both did our own damage");
            continue;
        }
        const bool quiet = blast.Kind == net::kBlastSnapshot || device.ByOurPlayer != 0;
        float x = device.Position[0];
        float y = device.Position[1];
        float z = device.Position[2];
        int32_t mode = blast.Kind == net::kBlastSnapshot ? SetExplodedQuietly : quiet ? BlowQuietly : BlowWithDamage;
        int32_t result = 2;
        RED4ext::ent::EntityID entity(device.DeviceKey);
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &entity);
        args.emplace_back(nullptr, &x);
        args.emplace_back(nullptr, &y);
        args.emplace_back(nullptr, &z);
        args.emplace_back(nullptr, &mode);
        game::CallMethod(context.Service, Game.Blast, &result, args);
        std::string what;
        if (result == 0)
        {
            BlownDevices[device.DeviceKey] = {x, y, z};
            if (mode == BlowQuietly)
            {
                QuietUntilMs[device.DeviceKey] = context.NowMs + QuietBlastMs;
            }
            if (mode != SetExplodedQuietly)
            {
                FallbackAtMs[device.DeviceKey] = context.NowMs + DeathPipelineWaitMs;
            }
            what = mode == SetExplodedQuietly ? "set exploded quietly [snapshot]"
                   : mode == BlowQuietly      ? "explodes here without damage (blast, sound, push; damage is the peers)"
                                              : "explodes here with its damage on our npcs and player (not blown by the peers player)";
        }
        else if (result == 1)
        {
            what = "exploded here already";
        }
        else if (result == 2)
        {
            if (WaitingBlasts.size() < MostWaiting)
            {
                WaitingBlasts.push_back({device, context.NowMs + RetryEveryMs, context.NowMs + BlastWaitMs});
            }
            what = "not in our stream, set exploded once it streams in";
        }
        else
        {
            what = "not the same device, left alone";
        }
        context.Log("barrels: peer " + std::to_string(fromPeer) + (blast.Kind == net::kBlastSnapshot ? " says " : " blew ") + KeyText(device.DeviceKey) + ": " + what);
    }
}

void ServeBlasts(const Context& context)
{
    for (auto quiet = QuietUntilMs.begin(); quiet != QuietUntilMs.end();)
    {
        if (context.NowMs < quiet->second)
        {
            ++quiet;
            continue;
        }
        CallForEntity(context, Game.EndQuiet, quiet->first, nullptr);
        quiet = QuietUntilMs.erase(quiet);
    }
    for (auto fallback = FallbackAtMs.begin(); fallback != FallbackAtMs.end();)
    {
        if (context.NowMs < fallback->second)
        {
            ++fallback;
            continue;
        }
        bool blownDirectly = false;
        CallForEntity(context, Game.FallbackBlow, fallback->first, &blownDirectly);
        if (blownDirectly)
        {
            context.Log("barrels: no death pipeline " + std::to_string(DeathPipelineWaitMs) + " ms after Device.Die on " + KeyText(fallback->first) +
                        ": StartExplosionPipeline called directly");
        }
        fallback = FallbackAtMs.erase(fallback);
    }
    for (auto waiting = WaitingBlasts.begin(); waiting != WaitingBlasts.end();)
    {
        if (context.NowMs < waiting->NextTryMs)
        {
            ++waiting;
            continue;
        }
        float x = waiting->Device.Position[0];
        float y = waiting->Device.Position[1];
        float z = waiting->Device.Position[2];
        int32_t mode = SetExplodedQuietly;
        int32_t result = 2;
        RED4ext::ent::EntityID entity(waiting->Device.DeviceKey);
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &entity);
        args.emplace_back(nullptr, &x);
        args.emplace_back(nullptr, &y);
        args.emplace_back(nullptr, &z);
        args.emplace_back(nullptr, &mode);
        game::CallMethod(context.Service, Game.Blast, &result, args);
        if (result != 2 || context.NowMs > waiting->GiveUpMs)
        {
            if (result == 0)
            {
                BlownDevices[waiting->Device.DeviceKey] = {x, y, z};
                context.Log("barrels: " + KeyText(waiting->Device.DeviceKey) + " streamed in and is set exploded, it blew at a partners");
            }
            waiting = WaitingBlasts.erase(waiting);
            continue;
        }
        waiting->NextTryMs = context.NowMs + RetryEveryMs;
        ++waiting;
    }
    if (!context.WeAreHost)
    {
        return;
    }
    std::erase_if(PeersWithSnapshot, [&context](uint32_t peer) {
        return std::find(context.Partners.begin(), context.Partners.end(), peer) == context.Partners.end();
    });
    for (uint32_t peer : context.Partners)
    {
        if (!PeersWithSnapshot.insert(peer).second || BlownDevices.empty())
        {
            continue;
        }
        std::vector<net::BlastedDevice> snapshot;
        for (const auto& [key, position] : BlownDevices)
        {
            snapshot.push_back({key, {position[0], position[1], position[2]}, 0});
        }
        context.Log("barrels: peer " + std::to_string(peer) + " came into our world, snapshot of " + std::to_string(snapshot.size()) + " exploded device(s) sent");
        SendBlasts(context, net::kBlastSnapshot, std::move(snapshot), static_cast<int32_t>(peer));
    }
}

void ReportNow(const Context& context)
{
    if (context.NowMs < NextSummaryMs)
    {
        return;
    }
    NextSummaryMs = context.NowMs + SummaryEveryMs;
    if (std::memcmp(&Count, &CountAtSummary, sizeof(Count)) == 0)
    {
        return;
    }
    CountAtSummary = Count;
    context.Log("devices: so far " + std::to_string(Count.OursSent) + " action(s) of ours went out (" + std::to_string(Count.NotShared) +
                " not shared), as the host taken " + std::to_string(Count.Taken) + " refused " + std::to_string(Count.Refused) + ", followed " +
                std::to_string(Count.Followed) + " (gave up " + std::to_string(Count.GaveUp) + ", stale " + std::to_string(Count.Stale) + ", dropped " +
                std::to_string(Count.Dropped) + "), turret asks " + std::to_string(Count.TurretAsks) + " answers " + std::to_string(Count.TurretAnswers) +
                ", blasts out " + std::to_string(Count.BlastsSent) + " in " + std::to_string(Count.BlastsTaken));
}

std::vector<uint32_t> PartnersInOurWorld(bool weAreHost, bool shareAlways, net::RelaySession& session)
{
    std::vector<uint32_t> partners;
    if (!weAreHost)
    {
        if (shareAlways || (world_handoff::InHostWorld() && partner_roster::HostInItsWorld()))
        {
            partners.push_back(0);
        }
        return partners;
    }
    for (const auto& [peer, nickname] : session.PeerNicknames())
    {
        if (peer != 0 && (shareAlways || partner_roster::PeerInHostWorld(peer)))
        {
            partners.push_back(peer);
        }
    }
    return partners;
}

void Forget()
{
    OurActions.clear();
    OurOpenRequests.clear();
    Devices.clear();
    FollowJobs.clear();
    WaitingForStream.clear();
    PendingKills.clear();
    EchoGuardUntilMs.clear();
    DeathSentMs.clear();
    Turrets.clear();
    BlownDevices.clear();
    QuietUntilMs.clear();
    FallbackAtMs.clear();
    WaitingBlasts.clear();
    PeersWithSnapshot.clear();
}
}

void Tick(uint64_t nowMs, bool inSave, bool shareAlways, net::RelaySession& session, const LogFunction& log)
{
    if (nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    const auto status = session.GetStatus();
    if (!inSave || !status.InSession || !Resolve(log))
    {
        if (Shared)
        {
            log("devices: not shared any more, evry player keeps their own devices");
            Shared = false;
            Forget();
        }
        return;
    }
    auto service = game::ScriptService("NightCoopDeviceSync");
    if (!service)
    {
        return;
    }
    WeWereHost = status.MyPeerNumber == 0;
    OurPeerNumber = status.MyPeerNumber;
    CurrentPartners = PartnersInOurWorld(WeWereHost, shareAlways, session);
    bool shared = !CurrentPartners.empty();
    RED4ext::StackArgs_t configureArgs;
    configureArgs.emplace_back(nullptr, &shared);
    game::CallMethod(service.instance, Game.Configure, nullptr, configureArgs);
    if (shared != Shared)
    {
        log(shared ? (WeWereHost ? "devices: partners are in our world, our devices are shared" : "devices: we are in the hosts world, its devices lead ours")
                   : "devices: not shared any more, evry player keeps their own devices");
        Shared = shared;
        Forget();
    }
    Context context {service.instance, session, log, nowMs, WeWereHost, OurPeerNumber, CurrentPartners};
    if (shared)
    {
        TakeOurActions(context);
        TakeOurDeaths(context);
        TakeTurretWants(context);
        TakeOurBlasts(context);
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, Game.ClearOutgoing, nullptr, noArgs);
    if (!shared)
    {
        return;
    }
    std::erase_if(OurOpenRequests, [nowMs](const auto& request) { return nowMs - request.second > RequestForgetMs; });
    std::erase_if(EchoGuardUntilMs, [nowMs](const auto& guard) { return nowMs >= guard.second; });
    SendSettledActions(context);
    ServeFollowJobs(context);
    ServeWaitingForStream(context);
    ServePendingKills(context);
    ServeTurrets(context);
    ServeBlasts(context);
    ReportNow(context);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    if (!Game.Usable || !Shared)
    {
        return;
    }
    const bool fromOurWorld = WeWereHost ? std::find(CurrentPartners.begin(), CurrentPartners.end(), fromPeer) != CurrentPartners.end() : fromPeer == 0;
    if (!fromOurWorld)
    {
        ++Count.Dropped;
        return;
    }
    auto service = game::ScriptService("NightCoopDeviceSync");
    if (!service)
    {
        return;
    }
    Context context {service.instance, session, log, SteadyMs(), WeWereHost, OurPeerNumber, CurrentPartners};
    const uint8_t type = net::MessageTypeOf(message);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::DeviceSync))
    {
        net::DeviceSync sync;
        if (!net::ReadDeviceSync(message, sync))
        {
            ++Count.Dropped;
            return;
        }
        if (WeWereHost && sync.Kind == net::kDeviceRequest && sync.Peer == fromPeer)
        {
            TakeGuestAction(context, fromPeer, sync);
        }
        else if (!WeWereHost && sync.Kind != net::kDeviceRequest)
        {
            TakeHostWord(context, sync);
        }
        else
        {
            ++Count.Dropped;
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::TurretWord))
    {
        net::TurretWord word;
        if (net::ReadTurretWord(message, word))
        {
            TakeTurretWord(context, fromPeer, word);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::DeviceBlast))
    {
        net::DeviceBlast blast;
        if (net::ReadDeviceBlast(message, blast))
        {
            TakeBlast(context, fromPeer, blast);
        }
    }
}

void RunTest(uint64_t nowMs, bool inSave, const float* ourPosition, const LogFunction& log)
{
    static uint64_t nextUseMs = 0;
    static std::set<uint64_t> used;
    if (!inSave || !ourPosition)
    {
        return;
    }
    if (!nextUseMs)
    {
        nextUseMs = nowMs + 20000;
    }
    if (nowMs < nextUseMs)
    {
        return;
    }
    nextUseMs = nowMs + 15000;
    static auto* getEntityList = game::FindFunction("ScriptGameInstance", "GetEntityList");
    static auto* getWorldPosition = game::FindFunction("entEntity", "GetWorldPosition");
    static auto* getEntityId = game::FindFunction("entEntity", "GetEntityID");
    static auto* testUse = game::FindFunction("NightCoopDeviceSync", "TestUse");
    static auto* deviceClass = RED4ext::CRTTISystem::Get()->GetClass("Device");
    auto service = game::ScriptService("NightCoopDeviceSync");
    if (!getEntityList || !getWorldPosition || !getEntityId || !testUse || !deviceClass || !service)
    {
        log("test devices: device calls missing");
        return;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> entities;
    RED4ext::StackArgs_t listArgs;
    listArgs.emplace_back(nullptr, &gameInstance);
    game::CallStatic(getEntityList, &entities, listArgs);
    RED4ext::Handle<RED4ext::IScriptable> nearest;
    uint64_t nearestHash = 0;
    float nearestDistance = 50.0f;
    int devices = 0;
    for (auto& entity : entities)
    {
        auto* entityClass = entity ? entity->GetType() : nullptr;
        if (!entityClass || !entityClass->IsA(deviceClass))
        {
            continue;
        }
        ++devices;
        uint64_t entityHash = 0;
        RED4ext::Vector4 place {};
        RED4ext::StackArgs_t noArgs;
        game::CallMethod(entity.instance, getEntityId, &entityHash, noArgs);
        game::CallMethod(entity.instance, getWorldPosition, &place, noArgs);
        const float deltaX = place.X - ourPosition[0];
        const float deltaY = place.Y - ourPosition[1];
        const float deltaZ = place.Z - ourPosition[2];
        const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        if (distance < nearestDistance && !used.count(entityHash))
        {
            nearestDistance = distance;
            nearest = entity;
            nearestHash = entityHash;
        }
    }
    if (!nearest)
    {
        log("test devices: no unused device within 50 m (" + std::to_string(devices) + " devices streamed in)");
        return;
    }
    used.insert(nearestHash);
    RED4ext::CString result;
    RED4ext::StackArgs_t useArgs;
    useArgs.emplace_back(nullptr, &nearest);
    game::CallMethod(service.instance, testUse, &result, useArgs);
    char line[200];
    std::snprintf(line, sizeof(line), "test devices: used device %#llx %.1f m away: %s", static_cast<unsigned long long>(nearestHash), nearestDistance,
                  result.c_str());
    log(line);
}
}
