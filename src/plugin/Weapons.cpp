#include "plugin/Weapons.hpp"

#include "net/LookMessages.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Items.hpp"
#include "plugin/StateMachineBoard.hpp"

#include <RED4ext/Scripting/Natives/Generated/game/StatsObjectID.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/TweakDB.hpp>

#include <algorithm>
#include <cstdio>

namespace nightcoop_mac::weapons
{
namespace
{
constexpr int32_t UpperBodyReload = 3;
constexpr int32_t UpperBodyAim = 6;
constexpr int32_t WeaponReload = 2;
constexpr int32_t WeaponShoot = 8;
constexpr uint16_t AimFlag = 0x04;
constexpr uint16_t ShootFlag = 0x40;
constexpr uint16_t ReloadFlag = 0x80;
constexpr uint16_t SilencedFlag = 0x8000;
constexpr uint32_t MostRoundsPerShotDrop = 10;
constexpr uint32_t MostShotsPerState = 4;
constexpr uint32_t ShotCountJumpMeansRestart = 1000;
constexpr uint32_t MostPlaceTries = 5;
constexpr uint32_t FxActionShoot = 16;
constexpr uint32_t FxActionSilencedShoot = 17;
constexpr int32_t NpcWeaponOwner = 1;

struct OwnCalls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetAllBlackboardDefs = nullptr;
    RED4ext::CBaseFunction* GetUint = nullptr;
    RED4ext::CBaseFunction* SetUint = nullptr;
    RED4ext::CBaseFunction* GetSharedData = nullptr;
    RED4ext::CBaseFunction* GetEntityId = nullptr;
    RED4ext::CBaseFunction* GetStatsSystem = nullptr;
    RED4ext::CBaseFunction* GetStatValue = nullptr;
    RED4ext::CProperty* RightHand = nullptr;
    RED4ext::CProperty* LeftHand = nullptr;
    RED4ext::CProperty* WeaponDataDefs = nullptr;
    RED4ext::CProperty* MagazineId = nullptr;
    uint32_t SilentKillStat = 0;
    bool KnowsSilentKill = false;
    RED4ext::Handle<RED4ext::IScriptable> Defs;
};

struct HeldCalls
{
    bool Resolved = false;
    bool Usable = false;
    bool EffectsUsable = false;
    RED4ext::CBaseFunction* QueueEvent = nullptr;
    RED4ext::CBaseFunction* SetWeaponEffects = nullptr;
    RED4ext::CBaseFunction* SetWeaponOwner = nullptr;
    RED4ext::CBaseFunction* TriggerWeaponEffects = nullptr;
    RED4ext::CClass* ExternalEventClass = nullptr;
    RED4ext::CProperty* ExternalEventName = nullptr;
};

struct HandReading
{
    uint64_t Record = 0;
    bool MagazineKnown = false;
    uint32_t Magazine = 0;
    uint32_t Shots = 0;
};

OwnCalls Own;
HeldCalls Partner;
HandReading OwnRight;
HandReading OwnLeft;

bool PropertyIs(RED4ext::CProperty* property, const char* typeName)
{
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName;
}

RED4ext::CProperty* PropertyOf(const char* className, const char* propertyName, const char* typeName)
{
    auto* owner = RED4ext::CRTTISystem::Get()->GetClass(className);
    auto* property = owner ? owner->GetProperty(propertyName) : nullptr;
    return PropertyIs(property, typeName) ? property : nullptr;
}

bool ResolveOwn(const LogFunction& log)
{
    if (Own.Resolved)
    {
        return Own.Usable;
    }
    Own.Resolved = true;
    Own.GetAllBlackboardDefs = RED4ext::CRTTISystem::Get()->GetFunction("GetAllBlackboardDefs");
    Own.GetUint = game::FindFunction("gameIBlackboard", "GetUint");
    Own.SetUint = game::FindFunction("gameIBlackboard", "SetUint");
    Own.GetSharedData = game::FindFunction("gameweaponObject", "GetSharedData");
    Own.GetEntityId = game::FindFunction("entEntity", "GetEntityID");
    Own.GetStatsSystem = game::FindFunction("ScriptGameInstance", "GetStatsSystem");
    Own.GetStatValue = game::FindFunction("gameStatsSystem", "GetStatValue");
    Own.RightHand = PropertyOf("PlayerPuppet", "equippedRightHandWeapon", "whandle:gameweaponObject");
    Own.LeftHand = PropertyOf("PlayerPuppet", "equippedLeftHandWeapon", "whandle:gameweaponObject");
    Own.WeaponDataDefs = PropertyOf("gamebbAllScriptDefinitions", "Weapon", "handle:WeaponDataDef");
    Own.MagazineId = PropertyOf("WeaponDataDef", "MagazineAmmoCount", "gamebbScriptID_Uint32");
    if (auto* stats = RED4ext::CRTTISystem::Get()->GetEnum("gamedataStatType"))
    {
        const RED4ext::CName silentKill("CanSilentKill");
        for (uint32_t index = 0; index < stats->hashList.Size() && index < stats->valueList.Size(); ++index)
        {
            if (stats->hashList[index] == silentKill)
            {
                Own.SilentKillStat = static_cast<uint32_t>(stats->valueList[index]);
                Own.KnowsSilentKill = true;
            }
        }
    }
    Own.KnowsSilentKill = Own.KnowsSilentKill && game::ParamsAre(Own.GetStatsSystem, {"ScriptGameInstance"}) &&
                          game::ParamsAre(Own.GetStatValue, {"gameStatsObjectID", "gamedataStatType"}) && game::ParamsAre(Own.GetEntityId, {});
    Own.Usable = game::ParamsAre(Own.GetAllBlackboardDefs, {}) &&
                 game::ParamsAre(Own.GetUint, {"gamebbScriptID_Uint32"}) && game::ParamsAre(Own.GetSharedData, {}) && items::Ready(log) &&
                 Own.RightHand && Own.LeftHand && Own.WeaponDataDefs && Own.MagazineId;
    log(Own.Usable ? std::string("weapons: our weapons, shots and weapon states go out in PlayerState") +
                         (Own.KnowsSilentKill ? "" : " (silencers cant be told apart)")
                   : "weapons: the player weapon or blackboard calls look different than expected, partners wont see our guns");
    return Own.Usable;
}

void* IdIn(RED4ext::CProperty* defsProperty, RED4ext::CProperty* idProperty)
{
    auto* definition = defsProperty->GetValuePtr<RED4ext::Handle<RED4ext::IScriptable>>(Own.Defs.instance);
    return definition && definition->instance ? idProperty->GetValuePtr<uint8_t>(definition->instance) : nullptr;
}

bool BoardValue(RED4ext::Handle<RED4ext::IScriptable>& board, RED4ext::CBaseFunction* getter, void* id, void* value)
{
    if (!board || !id)
    {
        return false;
    }
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, id);
    return game::CallMethod(board.instance, getter, value, args);
}

void ReadHand(HandReading& hand, RED4ext::Handle<RED4ext::IScriptable>& weapon, uint64_t record)
{
    if (record != hand.Record)
    {
        hand.Record = record;
        hand.MagazineKnown = false;
    }
    if (!record || !weapon)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> sharedData;
    RED4ext::StackArgs_t noArgs;
    uint32_t magazine = 0;
    if (!game::CallMethod(weapon.instance, Own.GetSharedData, &sharedData, noArgs) ||
        !BoardValue(sharedData, Own.GetUint, IdIn(Own.WeaponDataDefs, Own.MagazineId), &magazine))
    {
        return;
    }
    if (hand.MagazineKnown && magazine < hand.Magazine)
    {
        const uint32_t drop = hand.Magazine - magazine;
        hand.Shots += drop > MostRoundsPerShotDrop ? 1 : drop;
    }
    hand.Magazine = magazine;
    hand.MagazineKnown = true;
}

bool IsSilenced(RED4ext::Handle<RED4ext::IScriptable>& weapon)
{
    if (!Own.KnowsSilentKill || !weapon)
    {
        return false;
    }
    uint64_t entityId = 0;
    RED4ext::StackArgs_t noArgs;
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> stats;
    RED4ext::StackArgs_t statsArgs;
    statsArgs.emplace_back(nullptr, &gameInstance);
    if (!game::CallMethod(weapon.instance, Own.GetEntityId, &entityId, noArgs) || !entityId ||
        !game::CallStatic(Own.GetStatsSystem, &stats, statsArgs) || !stats)
    {
        return false;
    }
    RED4ext::game::StatsObjectID statsObject {};
    statsObject.entityHash = entityId;
    statsObject.idType = RED4ext::game::StatIDType::EntityID;
    uint32_t statType = Own.SilentKillStat;
    float value = 0;
    RED4ext::StackArgs_t valueArgs;
    valueArgs.emplace_back(nullptr, &statsObject);
    valueArgs.emplace_back(nullptr, &statType);
    game::CallMethod(stats.instance, Own.GetStatValue, &value, valueArgs);
    return value > 0;
}

bool HasTag(uint64_t record, const char* tag)
{
    const RED4ext::CName wanted(tag);
    for (const auto& name : items::Names(record, ".tags"))
    {
        if (name == wanted)
        {
            return true;
        }
    }
    return false;
}

std::string RecordText(uint64_t record)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(record));
    return text;
}

bool ResolveHeld(const LogFunction& log)
{
    if (Partner.Resolved)
    {
        return Partner.Usable;
    }
    Partner.Resolved = true;
    Partner.QueueEvent = game::FindFunction("entEntity", "QueueEvent");
    Partner.SetWeaponEffects = game::FindFunction("gameweaponObject", "SetWeaponEffects");
    Partner.SetWeaponOwner = game::FindFunction("gameweaponObject", "SetWeaponOwner;Int32");
    Partner.TriggerWeaponEffects = game::FindFunction("gameweaponObject", "TriggerWeaponEffects");
    Partner.ExternalEventClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimExternalEvent");
    Partner.ExternalEventName = Partner.ExternalEventClass ? Partner.ExternalEventClass->GetProperty("name") : nullptr;
    Partner.Usable = items::Ready(log);
    Partner.EffectsUsable = game::ParamsAre(Partner.QueueEvent, {"handle:redEvent"}) &&
                            game::ParamsAre(Partner.SetWeaponEffects, {"array:whandle:gamedataWeaponVFXAction_Record"}) &&
                            game::ParamsAre(Partner.SetWeaponOwner, {"Int32"}) &&
                            game::ParamsAre(Partner.TriggerWeaponEffects, {"whandle:gameweaponObject", "gamedataFxAction", "handle:worldEffectBlackboard"}) &&
                            PropertyIs(Partner.ExternalEventName, "CName");
    if (!Partner.Usable)
    {
        log("weapons: no item calls, partners keep empty hands");
    }
    else if (Partner.EffectsUsable)
    {
        log("weapons: partner guns go in their bodys hands and their shots play (Shoot anim event, muzzle flash)");
    }
    else
    {
        log("weapons: partner guns go in their bodys hands but the shot effects look different than expected, no shots (" +
            game::Describe(Partner.QueueEvent) + ", " + game::Describe(Partner.SetWeaponEffects) + ", " + game::Describe(Partner.SetWeaponOwner) + ", " +
            game::Describe(Partner.TriggerWeaponEffects) + (PropertyIs(Partner.ExternalEventName, "CName") ? "" : ", no entAnimExternalEvent.name") + ")");
    }
    return Partner.Usable;
}

void QueueAnimEvent(RED4ext::Handle<RED4ext::IScriptable>& entity, const char* eventName)
{
    auto* animEvent = static_cast<RED4ext::IScriptable*>(Partner.ExternalEventClass->CreateInstance(true));
    if (!animEvent)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> eventHandle(animEvent);
    *Partner.ExternalEventName->GetValuePtr<RED4ext::CName>(animEvent) = RED4ext::CName(eventName);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &eventHandle);
    game::CallMethod(entity.instance, Partner.QueueEvent, nullptr, args);
}

uint32_t SetUpEffects(RED4ext::Handle<RED4ext::IScriptable>& weapon, uint64_t record)
{
    auto* tweakDb = RED4ext::TweakDB::Get();
    const uint64_t effectSet = items::ForeignKey(items::ForeignKey(record, ".fxPackage"), ".npc_vfx_set");
    RED4ext::DynArray<RED4ext::WeakHandle<RED4ext::IScriptable>> actions;
    for (const uint64_t action : items::ForeignKeys(effectSet, ".actions"))
    {
        auto actionRecord = tweakDb ? tweakDb->GetRecord(RED4ext::TweakDBID(net::RecordWithoutOffset(action))) : RED4ext::Handle<RED4ext::IScriptable>();
        if (actionRecord)
        {
            actions.PushBack(RED4ext::WeakHandle<RED4ext::IScriptable>(actionRecord));
        }
    }
    if (actions.Size())
    {
        RED4ext::StackArgs_t effectArgs;
        effectArgs.emplace_back(nullptr, &actions);
        game::CallMethod(weapon.instance, Partner.SetWeaponEffects, nullptr, effectArgs);
    }
    int32_t owner = NpcWeaponOwner;
    RED4ext::StackArgs_t ownerArgs;
    ownerArgs.emplace_back(nullptr, &owner);
    game::CallMethod(weapon.instance, Partner.SetWeaponOwner, nullptr, ownerArgs);
    return actions.Size();
}

void FlashMuzzle(RED4ext::Handle<RED4ext::IScriptable>& weapon, bool silenced)
{
    RED4ext::WeakHandle<RED4ext::IScriptable> weakWeapon(weapon);
    uint32_t action = silenced ? FxActionSilencedShoot : FxActionShoot;
    RED4ext::Handle<RED4ext::IScriptable> noBlackboard;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &weakWeapon);
    args.emplace_back(nullptr, &action);
    args.emplace_back(nullptr, &noBlackboard);
    game::CallStatic(Partner.TriggerWeaponEffects, nullptr, args);
}

void PutInHand(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, HandHeld& hand)
{
    const items::ItemId item = items::ItemFromRecord(hand.Record);
    if (!items::Has(transactions, body, item))
    {
        items::Give(transactions, body, item);
    }
    items::PutInSlot(transactions, body, hand.Slot, item, true);
}

void HoldHand(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, HandHeld& hand,
              uint64_t record, bool rightHand, uint64_t nowMs, const LogFunction& log)
{
    const char* handName = rightHand ? "right" : "left";
    const std::string who = "weapons: peer " + std::to_string(peerNumber);
    const uint64_t wanted = net::RecordWithoutOffset(record);
    if (wanted != hand.Record)
    {
        if (hand.Slot)
        {
            items::EmptySlot(transactions, body, hand.Slot);
        }
        const bool shotsKnown = hand.ShotsKnown;
        const uint32_t shotsSeen = hand.ShotsSeen;
        hand = HandHeld {};
        hand.ShotsKnown = shotsKnown;
        hand.ShotsSeen = shotsSeen;
        hand.Record = wanted;
        if (!wanted)
        {
            log(who + " put the " + handName + " hand weapon away");
            return;
        }
        if (!items::RecordExists(wanted))
        {
            log(who + " holds " + RecordText(wanted) + " in the " + handName + " hand, no such record here so the hand stays empty");
            hand.GaveUp = true;
            return;
        }
        const uint64_t ownSlot = rightHand ? RED4ext::TweakDBID("AttachmentSlots.WeaponRight").value : RED4ext::TweakDBID("AttachmentSlots.WeaponLeft").value;
        const uint64_t placementSlot = rightHand ? items::PlacementSlot(wanted) : 0;
        hand.Slot = placementSlot ? placementSlot : ownSlot;
        PutInHand(transactions, body, hand);
        hand.NextCheckMs = nowMs + 1000;
        log(who + " holds " + RecordText(wanted) + " in the " + handName + " hand, put on their body");
        return;
    }
    if (!wanted || hand.GaveUp || nowMs < hand.NextCheckMs)
    {
        return;
    }
    if (net::RecordWithoutOffset(items::ItemInSlot(transactions, body, hand.Slot).Record) == wanted)
    {
        if (!hand.Verified)
        {
            hand.Verified = true;
            log(who + "s " + handName + " hand weapon " + RecordText(wanted) + " is in the slot" +
                (hand.PlaceTries ? " after " + std::to_string(hand.PlaceTries) + " retry(s)" : ""));
        }
        hand.PlaceTries = 0;
        hand.NextCheckMs = nowMs + 5000;
        return;
    }
    if (++hand.PlaceTries > MostPlaceTries)
    {
        hand.GaveUp = true;
        log(who + "s " + handName + " hand weapon " + RecordText(wanted) + " never stayed in the slot, gave up");
        return;
    }
    hand.Verified = false;
    hand.EffectsReady = false;
    PutInHand(transactions, body, hand);
    hand.NextCheckMs = nowMs + 1000;
}

uint32_t PlayShots(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body,
                   HandHeld& hand, uint32_t shotCount, bool silenced, const LogFunction& log)
{
    if (!hand.ShotsKnown)
    {
        hand.ShotsKnown = true;
        hand.ShotsSeen = shotCount;
        return 0;
    }
    uint32_t fresh = shotCount - hand.ShotsSeen;
    hand.ShotsSeen = shotCount;
    if (!fresh || fresh > ShotCountJumpMeansRestart || !hand.Record || hand.GaveUp || !Partner.EffectsUsable)
    {
        return 0;
    }
    fresh = std::min(fresh, MostShotsPerState);
    auto weapon = items::ObjectInSlot(transactions, body, hand.Slot);
    if (!weapon)
    {
        return 0;
    }
    if (!hand.EffectsReady)
    {
        hand.EffectsReady = true;
        const uint32_t actions = SetUpEffects(weapon, hand.Record);
        log("weapons: peer " + std::to_string(peerNumber) + "s weapon " + RecordText(hand.Record) + " got " + std::to_string(actions) +
            " npc effect action(s) and the npc owner for its shots");
    }
    for (uint32_t shot = 0; shot < fresh; ++shot)
    {
        QueueAnimEvent(body, "Shoot");
        QueueAnimEvent(weapon, "Shoot");
        FlashMuzzle(weapon, silenced);
    }
    return fresh;
}
}

bool ReadOwnWeapons(RED4ext::Handle<RED4ext::IScriptable>& player, OwnWeapons& weapons, const LogFunction& log)
{
    if (!player || !ResolveOwn(log))
    {
        return false;
    }
    static auto* playerPuppetClass = RED4ext::CRTTISystem::Get()->GetClass("PlayerPuppet");
    auto* playerClass = player.instance->GetType();
    if (!playerClass || !playerPuppetClass || !playerClass->IsA(playerPuppetClass))
    {
        return false;
    }
    if (!Own.Defs)
    {
        RED4ext::StackArgs_t noArgs;
        game::CallStatic(Own.GetAllBlackboardDefs, &Own.Defs, noArgs);
        if (!Own.Defs)
        {
            return false;
        }
    }
    weapons = OwnWeapons {};
    state_machine_board::ReadInt(player, "RightWeapon", weapons.RightState);
    state_machine_board::ReadInt(player, "LeftWeapon", weapons.LeftState);
    state_machine_board::ReadInt(player, "UpperBody", weapons.UpperBody);
    auto right = Own.RightHand->GetValuePtr<RED4ext::WeakHandle<RED4ext::IScriptable>>(player.instance)->Lock();
    auto left = Own.LeftHand->GetValuePtr<RED4ext::WeakHandle<RED4ext::IScriptable>>(player.instance)->Lock();
    const uint64_t rightRecord = right ? net::RecordWithoutOffset(items::ItemOf(right).Record) : 0;
    const uint64_t leftRecord = left ? net::RecordWithoutOffset(items::ItemOf(left).Record) : 0;
    ReadHand(OwnRight, right, rightRecord);
    ReadHand(OwnLeft, left, leftRecord);
    weapons.RightRecord = rightRecord;
    weapons.RightShots = OwnRight.Shots;
    weapons.LeftRecord = leftRecord && HasTag(leftRecord, "DualWield") ? leftRecord : 0;
    weapons.LeftShots = OwnLeft.Shots;
    weapons.Silenced = IsSilenced(right);
    return true;
}

void PutInState(const OwnWeapons& weapons, net::PlayerState& state)
{
    const bool leftInUse = weapons.LeftRecord != 0;
    state.RightWeaponRecord = weapons.RightRecord;
    state.RightShotCount = weapons.RightShots;
    state.WeaponState = static_cast<uint8_t>(std::clamp(weapons.RightState, 0, 255));
    state.UpperBodyState = static_cast<uint8_t>(std::clamp(weapons.UpperBody, 0, 255));
    state.LeftWeaponRecord = weapons.LeftRecord;
    state.LeftShotCount = weapons.LeftShots;
    state.LeftWeaponState = leftInUse ? static_cast<uint8_t>(std::clamp(weapons.LeftState, 0, 255)) : 0;
    uint16_t flags = state.Flags;
    if (weapons.UpperBody == UpperBodyAim)
    {
        flags |= AimFlag;
    }
    if (weapons.UpperBody == UpperBodyReload || weapons.RightState == WeaponReload || (leftInUse && weapons.LeftState == WeaponReload))
    {
        flags |= ReloadFlag;
    }
    if (weapons.RightState == WeaponShoot || (leftInUse && weapons.LeftState == WeaponShoot))
    {
        flags |= ShootFlag;
    }
    if (weapons.Silenced)
    {
        flags |= SilencedFlag;
    }
    state.Flags = flags;
}

bool DrawTestWeapon(RED4ext::Handle<RED4ext::IScriptable>& player, const char* recordName, const LogFunction& log)
{
    static auto* equipItem = game::FindFunction("EquipmentSystemPlayerData", "EquipItem;ItemIDBoolBool");
    static auto* drawItem = game::FindFunction("EquipmentSystemPlayerData", "DrawItem;ItemIDgameEquipAnimationType");
    if (!player || !ResolveOwn(log) || !game::ParamsAre(equipItem, {"gameItemID", "Bool", "Bool"}) ||
        !game::ParamsAre(drawItem, {"gameItemID", "gameEquipAnimationType"}))
    {
        log("test weapon: no EquipItem / DrawItem like expected (" + game::Describe(equipItem) + ", " + game::Describe(drawItem) + ")");
        return false;
    }
    auto transactions = items::TransactionSystem();
    auto playerData = items::EquipmentData(player);
    if (!transactions || !playerData)
    {
        return false;
    }
    items::ItemId item = items::ItemFromRecord(RED4ext::TweakDBID(recordName).value);
    if (!items::Has(transactions, player, item))
    {
        items::Give(transactions, player, item);
    }
    bool blockActiveSlotsUpdate = false;
    bool forceEquipWeapon = true;
    RED4ext::StackArgs_t equipArgs;
    equipArgs.emplace_back(nullptr, &item);
    equipArgs.emplace_back(nullptr, &blockActiveSlotsUpdate);
    equipArgs.emplace_back(nullptr, &forceEquipWeapon);
    game::CallMethod(playerData.instance, equipItem, nullptr, equipArgs);
    uint32_t defaultAnimation = 0;
    RED4ext::StackArgs_t drawArgs;
    drawArgs.emplace_back(nullptr, &item);
    drawArgs.emplace_back(nullptr, &defaultAnimation);
    game::CallMethod(playerData.instance, drawItem, nullptr, drawArgs);
    log(std::string("test weapon: ") + recordName + " given, equipped and drawn");
    return true;
}

bool FakeShot(RED4ext::Handle<RED4ext::IScriptable>& player)
{
    if (!player || !Own.Usable || !Own.Defs || !game::ParamsAre(Own.SetUint, {"gamebbScriptID_Uint32", "Uint32", "Bool"}))
    {
        return false;
    }
    auto weapon = Own.RightHand->GetValuePtr<RED4ext::WeakHandle<RED4ext::IScriptable>>(player.instance)->Lock();
    RED4ext::Handle<RED4ext::IScriptable> sharedData;
    RED4ext::StackArgs_t noArgs;
    void* magazineId = IdIn(Own.WeaponDataDefs, Own.MagazineId);
    uint32_t magazine = 0;
    if (!weapon || !game::CallMethod(weapon.instance, Own.GetSharedData, &sharedData, noArgs) ||
        !BoardValue(sharedData, Own.GetUint, magazineId, &magazine) || !magazine)
    {
        return false;
    }
    uint32_t fewer = magazine - 1;
    bool forceFire = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, magazineId);
    args.emplace_back(nullptr, &fewer);
    args.emplace_back(nullptr, &forceFire);
    return game::CallMethod(sharedData.instance, Own.SetUint, nullptr, args);
}

std::string DescribeOwn(RED4ext::Handle<RED4ext::IScriptable>& player)
{
    if (!player || !Own.Usable || !Own.Defs)
    {
        return "weapon reads not ready";
    }
    int32_t rightState = -100;
    int32_t upperBody = -100;
    state_machine_board::ReadInt(player, "RightWeapon", rightState);
    state_machine_board::ReadInt(player, "UpperBody", upperBody);
    auto weapon = Own.RightHand->GetValuePtr<RED4ext::WeakHandle<RED4ext::IScriptable>>(player.instance)->Lock();
    RED4ext::Handle<RED4ext::IScriptable> sharedData;
    RED4ext::StackArgs_t noArgs;
    uint32_t magazine = 0;
    if (weapon)
    {
        game::CallMethod(weapon.instance, Own.GetSharedData, &sharedData, noArgs);
        BoardValue(sharedData, Own.GetUint, IdIn(Own.WeaponDataDefs, Own.MagazineId), &magazine);
    }
    char text[200];
    std::snprintf(text, sizeof(text), "RightWeapon %d, UpperBody %d, weapon %s, magazine %u", rightState, upperBody,
                  weapon ? RecordText(net::RecordWithoutOffset(items::ItemOf(weapon).Record)).c_str() : "none", magazine);
    return text;
}

void Hold(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state, Held& held, uint64_t nowMs,
          const LogFunction& log)
{
    if (!body || !ResolveHeld(log))
    {
        return;
    }
    auto transactions = items::TransactionSystem();
    if (!transactions)
    {
        return;
    }
    HoldHand(peerNumber, transactions, body, held.Right, state.RightWeaponRecord, true, nowMs, log);
    HoldHand(peerNumber, transactions, body, held.Left, state.LeftWeaponRecord, false, nowMs, log);
    const bool silenced = (state.Flags & SilencedFlag) != 0;
    held.ShotsPlayed += PlayShots(peerNumber, transactions, body, held.Right, state.RightShotCount, silenced, log);
    held.ShotsPlayed += PlayShots(peerNumber, transactions, body, held.Left, state.LeftShotCount, silenced, log);
}
}
