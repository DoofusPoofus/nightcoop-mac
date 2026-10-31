#include "plugin/NpcCopies.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/NpcCombat.hpp"
#include "plugin/NpcStates.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>
#include <RED4ext/TweakDB.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nightcoop_mac
{
namespace
{
constexpr uint64_t ShowDelayMs = 100;
constexpr uint64_t PlaceEveryMs = 33;
constexpr size_t MostSamples = 16;
constexpr uint64_t ForgetAfterMs = 10000;
constexpr uint64_t RetrySpawnAfterMs = 10000;
constexpr uint64_t CorpseStaysMs = 10000;
constexpr float MoveAtLeastSquared = 0.02f * 0.02f;
constexpr float TurnAtLeastDegrees = 0.5f;
constexpr uint8_t DiedReason = 3;
constexpr size_t MostPendingAnimBatches = 32;
constexpr uint64_t AnimWantAtMostEveryMs = 1000;
constexpr uint64_t ReportEveryMs = 10000;
constexpr uint64_t StatesEveryMs = 200;
constexpr uint64_t HandCheckEveryMs = 2000;
constexpr uint32_t MostHandTries = 5;
constexpr float HealthStepPercent = 1.0f;
constexpr float LowestLivingHealth = 1.0f;
const char* CopyTag = "NightCoopNpcCopy";
const char* ComponentsTurnedOff[] = {"AIHumanComponent",          "senseComponent", "ReactionManagerComponent", "StimBroadcasterComponent",
                                     "AITargetTrackerComponent", "moveComponent",  "moveMotionPlannerComponent"};

struct EulerAnglesLayout
{
    float Roll;
    float Pitch;
    float Yaw;
};

struct QuaternionLayout
{
    float I;
    float J;
    float K;
    float R;
};

template<typename Value>
bool WriteSpecProperty(RED4ext::CClass* specClass, void* spec, const char* name, const char* typeName, const Value& value)
{
    auto* property = specClass->GetProperty(name);
    if (!property || !property->type || std::string(property->type->GetName().ToString()) != typeName)
    {
        return false;
    }
    std::memcpy(property->GetValuePtr<uint8_t>(spec), &value, sizeof(Value));
    return true;
}

std::string NetIdText(uint32_t netId)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%#010x", netId);
    return text;
}

float YawGap(float first, float second)
{
    float gap = std::fmod(std::fabs(first - second), 360.0f);
    return gap > 180.0f ? 360.0f - gap : gap;
}
}

bool NpcCopies::Resolve()
{
    if (m_resolved)
    {
        return m_usable;
    }
    m_resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    m_getDynamicEntitySystem = game::FindFunction("ScriptGameInstance", "GetDynamicEntitySystem");
    m_createEntity = game::FindFunction("DynamicEntitySystem", "CreateEntity");
    m_getEntity = game::FindFunction("DynamicEntitySystem", "GetEntity");
    m_deleteEntity = game::FindFunction("DynamicEntitySystem", "DeleteEntity");
    m_getTeleportationFacility = game::FindFunction("ScriptGameInstance", "GetTeleportationFacility");
    m_teleport = game::FindFunction("gameTeleportationFacility", "Teleport");
    m_getComponents = game::FindFunction("entEntity", "GetComponents");
    m_toggle = game::FindFunction("entIComponent", "Toggle");
    m_setDisableRagdoll = game::FindFunction("NPCPuppet", "SetDisableRagdoll");
    m_kill = game::FindFunction("NPCPuppet", "Kill");
    m_getPlayer = rtti->GetFunction("GetPlayer;GameInstance");
    npc_states::Ready();
    m_specClass = rtti->GetClass("DynamicEntitySpec");
    m_npcClass = rtti->GetClass("NPCPuppet");
    for (const char* className : ComponentsTurnedOff)
    {
        if (auto* componentClass = rtti->GetClass(className))
        {
            m_offClasses.push_back(componentClass);
        }
    }
    m_usable = game::ParamsAre(m_getDynamicEntitySystem, {}) && game::ParamsAre(m_createEntity, {"handle:DynamicEntitySpec"}) &&
               game::ParamsAre(m_getEntity, {"entEntityID"}) && game::ParamsAre(m_deleteEntity, {"entEntityID"}) &&
               game::ParamsAre(m_getTeleportationFacility, {"ScriptGameInstance"}) &&
               game::ParamsAre(m_teleport, {"handle:gameObject", "Vector4", "EulerAngles"}) && game::ParamsAre(m_getComponents, {}) &&
               game::ParamsAre(m_toggle, {"Bool"}) && m_specClass && m_npcClass;
    if (!game::ParamsAre(m_setDisableRagdoll, {"Bool", "Bool", "Bool"}))
    {
        m_setDisableRagdoll = nullptr;
    }
    if (!game::ParamsAre(m_kill, {"whandle:gameObject", "Bool", "Bool"}))
    {
        m_kill = nullptr;
    }
    m_log(m_usable ? "npcs: partner npcs show up here as stand ins (" + std::to_string(m_offClasses.size()) + " of 7 ai component kinds get turned off on them)"
                   : "npcs: codeware, teleport or component calls look different than expected, no partner npcs");
    return m_usable;
}

bool NpcCopies::FindByEntity(uint64_t entityHash, uint32_t& netId, uint32_t& ownerPeer, float& ownerHealth) const
{
    for (const auto& [copyNetId, copy] : m_copies)
    {
        if (copy.EntityHash == entityHash && entityHash)
        {
            netId = copyNetId;
            ownerPeer = copy.OwnerPeer;
            ownerHealth = copy.HasSnapshot ? copy.Latest.Health : 100.0f;
            return true;
        }
    }
    return false;
}

uint64_t NpcCopies::EntityOfNetId(uint32_t netId) const
{
    const auto copy = m_copies.find(netId);
    return copy != m_copies.end() && copy->second.InWorld ? copy->second.EntityHash : 0;
}

RED4ext::Handle<RED4ext::IScriptable> NpcCopies::EntityOf(uint64_t entityHash)
{
    RED4ext::Handle<RED4ext::IScriptable> entity;
    if (!entityHash || !m_dynamicEntitySystem)
    {
        return entity;
    }
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    game::CallMethod(m_dynamicEntitySystem.instance, m_getEntity, &entity, args);
    return entity;
}

void NpcCopies::OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, uint32_t ourPeer)
{
    const uint8_t type = net::MessageTypeOf(message);
    net::NpcSpawn spawn;
    net::NpcSnapshot snapshot;
    net::NpcDespawn despawn;
    net::AnimBatch animBatch;
    if (type == static_cast<uint8_t>(nc::proto::MsgType::NpcAnim) && net::ReadNpcAnim(message, animBatch))
    {
        auto copy = m_copies.find(animBatch.Unknown00);
        if (copy == m_copies.end() || copy->second.OwnerPeer != fromPeer)
        {
            return;
        }
        ++m_animBatches;
        if (animBatch.FullState)
        {
            ++m_animFullStates;
            copy->second.GotFullAnimState = true;
            copy->second.PendingAnim.clear();
        }
        else if (!copy->second.GotFullAnimState)
        {
            ++m_animWaited;
            return;
        }
        if (copy->second.PendingAnim.size() >= MostPendingAnimBatches)
        {
            copy->second.PendingAnim.erase(copy->second.PendingAnim.begin());
        }
        copy->second.PendingAnim.push_back(std::move(animBatch));
        copy->second.LastHeardMs = nowMs;
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::NpcSpawn) && net::ReadNpcSpawn(message, spawn))
    {
        auto existing = m_copies.find(spawn.NetId);
        if (existing != m_copies.end() && existing->second.Spawn.Record != spawn.Record)
        {
            Remove(spawn.NetId, existing->second, "another person under the same number, it is made anew");
            m_copies.erase(existing);
        }
        auto& copy = m_copies[spawn.NetId];
        const bool fresh = copy.LastHeardMs == 0;
        copy.OwnerPeer = fromPeer;
        copy.Spawn = spawn;
        copy.LastHeardMs = nowMs;
        if (fresh)
        {
            Sample first;
            first.ArrivedMs = nowMs;
            std::memcpy(first.Position, spawn.Position, sizeof(first.Position));
            first.Yaw = spawn.Yaw;
            copy.Samples.push_back(first);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::NpcSnapshot) && net::ReadNpcSnapshot(message, snapshot))
    {
        auto copy = m_copies.find(snapshot.NetId);
        if (copy == m_copies.end() || copy->second.OwnerPeer != fromPeer)
        {
            return;
        }
        Sample sample;
        sample.ArrivedMs = nowMs;
        std::memcpy(sample.Position, snapshot.Position, sizeof(sample.Position));
        sample.Yaw = snapshot.Yaw;
        copy->second.Samples.push_back(sample);
        copy->second.Latest = snapshot;
        copy->second.HasSnapshot = true;
        while (copy->second.Samples.size() > MostSamples)
        {
            copy->second.Samples.pop_front();
        }
        copy->second.LastHeardMs = nowMs;
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::NpcDespawn) && net::ReadNpcDespawn(message, despawn))
    {
        auto copy = m_copies.find(despawn.NetId);
        if (copy == m_copies.end() || copy->second.OwnerPeer != fromPeer)
        {
            return;
        }
        if (despawn.Reason == DiedReason)
        {
            const bool weKilledIt = despawn.Killer == ourPeer;
            auto npc = EntityOf(copy->second.EntityHash);
            if (npc)
            {
                Kill(npc, weKilledIt);
            }
            copy->second.RemoveAtMs = nowMs + CorpseStaysMs;
            m_log("npcs: " + NetIdText(despawn.NetId) + " died at its owner" +
                  (weKilledIt ? ", we killed it so our game rewards it" : despawn.Killer != 0xFFFFFFFF ? ", peer " + std::to_string(despawn.Killer) + " killed it" : "") +
                  ", the body stays 10 s");
        }
        else
        {
            copy->second.RemoveAtMs = nowMs;
        }
    }
}

void NpcCopies::Spawn(uint32_t netId, Copy& copy, uint64_t nowMs)
{
    copy.SpawnRequestedMs = nowMs;
    auto* tweakDb = RED4ext::TweakDB::Get();
    if (!tweakDb || !tweakDb->GetRecord(RED4ext::TweakDBID(copy.Spawn.Record & 0xFFFFFFFFFFULL)))
    {
        m_log("npcs: " + NetIdText(netId) + " has a character record we dont know, no copy");
        return;
    }
    auto* spec = static_cast<RED4ext::IScriptable*>(m_specClass->CreateInstance(true));
    if (!spec)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> specHandle(spec);
    const Sample& latest = copy.Samples.back();
    const float halfYaw = latest.Yaw * 3.14159265f / 360.0f;
    const RED4ext::Vector4 position {latest.Position[0], latest.Position[1], latest.Position[2], 1.0f};
    const QuaternionLayout orientation {0.0f, 0.0f, std::sin(halfYaw), std::cos(halfYaw)};
    const bool written = WriteSpecProperty(m_specClass, spec, "recordID", "TweakDBID", RED4ext::TweakDBID(copy.Spawn.Record & 0xFFFFFFFFFFULL)) &&
                         WriteSpecProperty(m_specClass, spec, "appearanceName", "CName", RED4ext::CName(copy.Spawn.Appearance)) &&
                         WriteSpecProperty(m_specClass, spec, "position", "Vector4", position) &&
                         WriteSpecProperty(m_specClass, spec, "orientation", "Quaternion", orientation) &&
                         WriteSpecProperty(m_specClass, spec, "alwaysSpawned", "Bool", true) &&
                         WriteSpecProperty(m_specClass, spec, "spawnInView", "Bool", true) &&
                         WriteSpecProperty(m_specClass, spec, "active", "Bool", true) &&
                         WriteSpecProperty(m_specClass, spec, "persistState", "Bool", false) &&
                         WriteSpecProperty(m_specClass, spec, "persistSpawn", "Bool", false);
    auto* tags = m_specClass->GetProperty("tags");
    if (!written || !tags)
    {
        m_log("npcs: DynamicEntitySpec has other fields than expected, no partner npcs");
        m_usable = false;
        return;
    }
    tags->GetValuePtr<RED4ext::DynArray<RED4ext::CName>>(spec)->PushBack(RED4ext::CName(CopyTag));
    RED4ext::ent::EntityID entityId;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &specHandle);
    if (game::CallMethod(m_dynamicEntitySystem.instance, m_createEntity, &entityId, args) && entityId.hash)
    {
        copy.EntityHash = entityId.hash;
        m_log("npcs: peer " + std::to_string(copy.OwnerPeer) + " shares npc " + NetIdText(netId) + ", spawning its stand in");
    }
}

void NpcCopies::TakeControl(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc)
{
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> components;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(npc.instance, m_getComponents, &components, noArgs);
    for (auto& component : components)
    {
        auto* componentClass = component ? component->GetType() : nullptr;
        for (auto* offClass : m_offClasses)
        {
            if (componentClass && componentClass->IsA(offClass))
            {
                bool on = false;
                RED4ext::StackArgs_t toggleArgs;
                toggleArgs.emplace_back(nullptr, &on);
                game::CallMethod(component.instance, m_toggle, nullptr, toggleArgs);
                ++copy.ComponentsOff;
                break;
            }
        }
    }
    auto* npcClass = npc->GetType();
    if (m_setDisableRagdoll && npcClass && npcClass->IsA(m_npcClass))
    {
        bool disable = true;
        bool force = true;
        bool leaveEnabled = false;
        RED4ext::StackArgs_t ragdollArgs;
        ragdollArgs.emplace_back(nullptr, &disable);
        ragdollArgs.emplace_back(nullptr, &force);
        ragdollArgs.emplace_back(nullptr, &leaveEnabled);
        game::CallMethod(npc.instance, m_setDisableRagdoll, nullptr, ragdollArgs);
    }
    npc_combat::MarkCopy(npc);
    copy.Marked = true;
    m_log("npcs: stand in of " + NetIdText(netId) + " is in the world, taken under direct control (" + std::to_string(copy.ComponentsOff) +
          " component(s) off)");
}

void NpcCopies::Place(Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs)
{
    if (copy.Samples.empty() || nowMs < copy.NextPlaceMs)
    {
        return;
    }
    copy.NextPlaceMs = nowMs + PlaceEveryMs;
    const uint64_t showMs = nowMs > ShowDelayMs ? nowMs - ShowDelayMs : 0;
    Sample shown = copy.Samples.back();
    for (size_t index = 1; index < copy.Samples.size(); ++index)
    {
        const Sample& before = copy.Samples[index - 1];
        const Sample& after = copy.Samples[index];
        if (showMs >= before.ArrivedMs && showMs <= after.ArrivedMs && after.ArrivedMs > before.ArrivedMs)
        {
            const float blend = static_cast<float>(showMs - before.ArrivedMs) / static_cast<float>(after.ArrivedMs - before.ArrivedMs);
            for (int axis = 0; axis < 3; ++axis)
            {
                shown.Position[axis] = before.Position[axis] + (after.Position[axis] - before.Position[axis]) * blend;
            }
            float turn = std::fmod(after.Yaw - before.Yaw + 540.0f, 360.0f) - 180.0f;
            shown.Yaw = before.Yaw + turn * blend;
            break;
        }
    }
    if (copy.Placed)
    {
        const float deltaX = shown.Position[0] - copy.PlacedPosition[0];
        const float deltaY = shown.Position[1] - copy.PlacedPosition[1];
        const float deltaZ = shown.Position[2] - copy.PlacedPosition[2];
        if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < MoveAtLeastSquared && YawGap(shown.Yaw, copy.PlacedYaw) < TurnAtLeastDegrees)
        {
            return;
        }
    }
    RED4ext::Vector4 position {shown.Position[0], shown.Position[1], shown.Position[2], 1.0f};
    EulerAnglesLayout angles {0.0f, 0.0f, shown.Yaw};
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &npc);
    args.emplace_back(nullptr, &position);
    args.emplace_back(nullptr, &angles);
    if (game::CallMethod(m_teleportationFacility.instance, m_teleport, nullptr, args))
    {
        std::memcpy(copy.PlacedPosition, shown.Position, sizeof(copy.PlacedPosition));
        copy.PlacedYaw = shown.Yaw;
        copy.Placed = true;
        ++copy.Placements;
    }
}

void NpcCopies::PlayAnim(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs, net::RelaySession& session)
{
    if (!copy.GotFullAnimState)
    {
        if (nowMs - copy.LastAnimWantMs >= AnimWantAtMostEveryMs)
        {
            const bool firstAsk = copy.LastAnimWantMs == 0;
            copy.LastAnimWantMs = nowMs;
            session.SendGameMessage(net::WriteNpcAnimWant({netId, true, firstAsk}), static_cast<int32_t>(copy.OwnerPeer));
            ++m_animWants;
        }
        return;
    }
    if (copy.PendingAnim.empty())
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> controller;
    const bool controllerFound = m_animPlayer.ControllerOf(npc, controller);
    for (const auto& batch : copy.PendingAnim)
    {
        for (const auto& entry : batch.Entries)
        {
            if (!controllerFound)
            {
                ++m_animFailed;
                continue;
            }
            switch (m_animPlayer.Play(controller, entry))
            {
            case AnimPlayer::Result::Applied: ++m_animPlayed; break;
            case AnimPlayer::Result::Failed: ++m_animFailed; break;
            case AnimPlayer::Result::NoSetter:
            case AnimPlayer::Result::OtherSlot: ++m_animOther; break;
            }
        }
    }
    copy.PendingAnim.clear();
}

void NpcCopies::ApplyStates(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs, uint32_t ourPeer)
{
    if (!copy.HasSnapshot || nowMs < copy.NextStatesMs)
    {
        return;
    }
    copy.NextStatesMs = nowMs + StatesEveryMs;
    const auto& latest = copy.Latest;
    const float wantedHealth = std::max(latest.Health, LowestLivingHealth);
    if (copy.AppliedHealth < 0.0f || std::fabs(wantedHealth - copy.AppliedHealth) >= HealthStepPercent)
    {
        if (npc_states::SetHealthPercent(copy.EntityHash, wantedHealth))
        {
            copy.AppliedHealth = wantedHealth;
            ++m_healthWrites;
        }
    }
    for (int slot = 0; slot < npc_states::StateCount; ++slot)
    {
        uint8_t wanted = latest.States[slot];
        if (slot == npc_states::HighLevelStateSlot && wanted == npc_states::HighLevelDead)
        {
            continue;
        }
        if (slot == npc_states::StanceStateSlot && wanted == npc_states::StanceCover)
        {
            wanted = npc_states::StanceCrouch;
        }
        if (wanted != copy.AppliedStates[slot] && npc_states::WriteState(npc, slot, wanted))
        {
            copy.AppliedStates[slot] = wanted;
            ++m_stateWrites;
        }
    }
    int wantedAttitude = -1;
    for (const auto& partnerAttitude : latest.PartnerAttitudes)
    {
        if (partnerAttitude.Peer == ourPeer)
        {
            wantedAttitude = partnerAttitude.Attitude;
        }
    }
    if (wantedAttitude >= 0 && wantedAttitude != copy.AppliedAttitude)
    {
        auto copyAgent = npc_states::AttitudeAgentOf(npc);
        if (!copyAgent || !m_playerAgent)
        {
            if (!m_attitudeTroubleLogged)
            {
                m_attitudeTroubleLogged = true;
                m_log(std::string("npcs: cant set a copys attitude, ") + (copyAgent ? "" : "the copy has no attitude agent ") +
                      (m_playerAgent ? "" : "our player has no attitude agent ") + "(npc state calls that look different: " + npc_states::MissingCalls() + ")");
            }
        }
        else if (npc_states::SetAttitudeTowards(copyAgent, m_playerAgent, static_cast<uint8_t>(wantedAttitude)))
        {
            copy.AppliedAttitude = wantedAttitude;
            ++m_attitudeWrites;
            m_log("npcs: copy of " + NetIdText(netId) + " is now " +
                  (wantedAttitude == 2 ? "hostile" : wantedAttitude == 0 ? "friendly" : "neutral") + " to us like its owners npc is to our body");
        }
    }
    ApplyHands(netId, copy, npc, nowMs);
}

void NpcCopies::ApplyHands(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs)
{
    const uint64_t wantedRecords[2] = {copy.Latest.RightHandItem & 0xFFFFFFFFFFULL, copy.Latest.LeftHandItem & 0xFFFFFFFFFFULL};
    for (int hand = 0; hand < 2; ++hand)
    {
        const bool rightHand = hand == 0;
        if (wantedRecords[hand] != copy.HandRecords[hand])
        {
            copy.HandRecords[hand] = wantedRecords[hand];
            copy.HandTries[hand] = 0;
            if (!wantedRecords[hand])
            {
                npc_states::EmptyHand(npc, rightHand);
                continue;
            }
            if (npc_states::PutInHand(npc, wantedRecords[hand], rightHand))
            {
                ++m_handWrites;
                char line[160];
                std::snprintf(line, sizeof(line), "npcs: copy of %s holds %#llx in the %s hand like its owners npc", NetIdText(netId).c_str(),
                              static_cast<unsigned long long>(wantedRecords[hand]), rightHand ? "right" : "left");
                m_log(line);
            }
            copy.NextHandCheckMs = nowMs + HandCheckEveryMs;
        }
    }
    if (nowMs < copy.NextHandCheckMs)
    {
        return;
    }
    copy.NextHandCheckMs = nowMs + HandCheckEveryMs;
    for (int hand = 0; hand < 2; ++hand)
    {
        const bool rightHand = hand == 0;
        if (!copy.HandRecords[hand] || copy.HandTries[hand] >= MostHandTries)
        {
            continue;
        }
        if ((npc_states::HandRecord(npc, rightHand) & 0xFFFFFFFFFFULL) == copy.HandRecords[hand])
        {
            copy.HandTries[hand] = 0;
            continue;
        }
        if (++copy.HandTries[hand] == MostHandTries)
        {
            m_log("npcs: copy of " + NetIdText(netId) + " never kept its " + (rightHand ? "right" : "left") + " hand weapon, gave up");
            continue;
        }
        npc_states::PutInHand(npc, copy.HandRecords[hand], rightHand);
    }
}

void NpcCopies::Report(uint64_t nowMs)
{
    if (nowMs < m_nextReportMs)
    {
        return;
    }
    m_nextReportMs = nowMs + ReportEveryMs;
    if (m_copies.empty() && !m_animBatches)
    {
        return;
    }
    char line[400];
    std::snprintf(line, sizeof(line),
                  "npcs: %zu copy(s), npc anim in 10 s: %u batch(es), %u full state(s), %u waited for a full state, %u played, %u failed, %u no setter, "
                  "%u ask(s) for a full state; %u health, %u state, %u attitude and %u hand write(s)",
                  m_copies.size(), m_animBatches, m_animFullStates, m_animWaited, m_animPlayed, m_animFailed, m_animOther, m_animWants, m_healthWrites,
                  m_stateWrites, m_attitudeWrites, m_handWrites);
    m_log(line);
    m_animBatches = m_animFullStates = m_animWaited = m_animPlayed = m_animFailed = m_animOther = m_animWants = 0;
    m_healthWrites = m_stateWrites = m_attitudeWrites = m_handWrites = 0;
}

void NpcCopies::Kill(RED4ext::Handle<RED4ext::IScriptable>& npc, bool weKilledIt)
{
    auto* npcClass = npc->GetType();
    if (!m_kill || !npcClass || !npcClass->IsA(m_npcClass))
    {
        return;
    }
    npc_combat::ReleaseCopy(npc, weKilledIt);
    RED4ext::Handle<RED4ext::IScriptable> player;
    if (weKilledIt && m_getPlayer)
    {
        RED4ext::ScriptGameInstance gameInstance;
        RED4ext::StackArgs_t playerArgs;
        playerArgs.emplace_back(nullptr, &gameInstance);
        game::CallStatic(m_getPlayer, &player, playerArgs);
    }
    RED4ext::WeakHandle<RED4ext::IScriptable> instigator(player);
    bool skipDeathAnim = false;
    bool disableRagdoll = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &instigator);
    args.emplace_back(nullptr, &skipDeathAnim);
    args.emplace_back(nullptr, &disableRagdoll);
    game::CallMethod(npc.instance, m_kill, nullptr, args);
}

void NpcCopies::Remove(uint32_t netId, Copy& copy, const char* why)
{
    if (copy.Marked)
    {
        npc_combat::ForgetCopy(copy.EntityHash);
    }
    if (copy.EntityHash && m_dynamicEntitySystem)
    {
        RED4ext::ent::EntityID entityId(copy.EntityHash);
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &entityId);
        game::CallMethod(m_dynamicEntitySystem.instance, m_deleteEntity, nullptr, args);
    }
    m_log("npcs: copy of " + NetIdText(netId) + " removed, " + why + " (" + std::to_string(copy.Placements) + " placement(s))");
}

void NpcCopies::Tick(uint64_t nowMs, bool inSave, net::RelaySession& session)
{
    if (!inSave)
    {
        if (!m_copies.empty())
        {
            m_copies.clear();
            npc_combat::ForgetAll();
            m_playerAgent.Reset();
            m_dynamicEntitySystem.Reset();
            m_teleportationFacility.Reset();
        }
        return;
    }
    if (m_copies.empty() || !Resolve())
    {
        return;
    }
    if (!m_dynamicEntitySystem)
    {
        RED4ext::StackArgs_t noArgs;
        game::CallStatic(m_getDynamicEntitySystem, &m_dynamicEntitySystem, noArgs);
    }
    if (!m_teleportationFacility)
    {
        RED4ext::ScriptGameInstance gameInstance;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &gameInstance);
        game::CallStatic(m_getTeleportationFacility, &m_teleportationFacility, args);
    }
    if (!m_dynamicEntitySystem || !m_teleportationFacility)
    {
        return;
    }
    if (m_getPlayer)
    {
        RED4ext::ScriptGameInstance gameInstance;
        RED4ext::Handle<RED4ext::IScriptable> player;
        RED4ext::StackArgs_t playerArgs;
        playerArgs.emplace_back(nullptr, &gameInstance);
        game::CallStatic(m_getPlayer, &player, playerArgs);
        m_playerAgent = npc_states::AttitudeAgentOf(player);
    }
    const uint32_t ourPeer = session.GetStatus().MyPeerNumber;
    for (auto copyEntry = m_copies.begin(); copyEntry != m_copies.end();)
    {
        auto& [netId, copy] = *copyEntry;
        if ((copy.RemoveAtMs && nowMs >= copy.RemoveAtMs) || nowMs - copy.LastHeardMs > ForgetAfterMs)
        {
            Remove(netId, copy, copy.RemoveAtMs ? "its owner let it go" : "nothing heard of it for 10 s");
            copyEntry = m_copies.erase(copyEntry);
            continue;
        }
        if (!copy.EntityHash && (copy.SpawnRequestedMs == 0 || nowMs - copy.SpawnRequestedMs > RetrySpawnAfterMs))
        {
            Spawn(netId, copy, nowMs);
        }
        else if (copy.EntityHash && !copy.RemoveAtMs)
        {
            auto npc = EntityOf(copy.EntityHash);
            if (npc)
            {
                if (!copy.InWorld)
                {
                    copy.InWorld = true;
                    TakeControl(netId, copy, npc);
                }
                if (!copy.Held)
                {
                    Place(copy, npc, nowMs);
                }
                PlayAnim(netId, copy, npc, nowMs, session);
                ApplyStates(netId, copy, npc, nowMs, ourPeer);
            }
        }
        ++copyEntry;
    }
    Report(nowMs);
}

uint64_t NpcCopies::CopyEntity(uint32_t netId, uint32_t& ownerPeer) const
{
    const auto copy = m_copies.find(netId);
    ownerPeer = copy != m_copies.end() ? copy->second.OwnerPeer : 0;
    return copy != m_copies.end() && copy->second.InWorld ? copy->second.EntityHash : 0;
}

uint32_t NpcCopies::NetIdOfCopy(uint64_t entityHash) const
{
    for (const auto& [netId, copy] : m_copies)
    {
        if (entityHash && copy.EntityHash == entityHash)
        {
            return netId;
        }
    }
    return 0;
}

void NpcCopies::SetHeld(uint32_t netId, bool held)
{
    if (const auto copy = m_copies.find(netId); copy != m_copies.end())
    {
        copy->second.Held = held;
    }
}
}
