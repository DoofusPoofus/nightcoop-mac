#include "plugin/RemoteBodies.hpp"

#include "plugin/BodyRecords.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/PartnerBodyBuilder.hpp"
#include "plugin/PartnerLook.hpp"

#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace nightcoop_mac
{
namespace
{
constexpr float SpawnWithinMeters = 150.0f;
constexpr float RemoveBeyondMeters = 200.0f;
constexpr float MoveAtLeastSquaredMeters = 0.0004f;
constexpr float TurnAtLeastDegrees = 0.5f;
constexpr float SnapBeyondMeters = 8.0f;
constexpr uint32_t NpcStanceCrouch = 2;
constexpr uint32_t NpcStanceStand = 3;
constexpr uint32_t NpcUpperBodyAim = 0;
constexpr uint32_t NpcUpperBodyNormal = 6;
constexpr uint32_t NpcUpperBodyReload = 8;
constexpr uint64_t GiveUpSpawningAfterMs = 30000;
constexpr uint64_t RetryFailedSpawnAfterMs = 10000;
constexpr uint64_t WaitForLookMs = 5000;
const char* FemaleBodyRecord = "Character.TPP_Player_Cutscene_Female";
const char* MaleBodyRecord = "Character.TPP_Player_Cutscene_Male";
const char* KnownPlayerTemplates[] = {
    "ep1\\characters\\entities\\player\\player_wa_fpp_ep1.ent",
    "ep1\\characters\\entities\\player\\player_ma_fpp_ep1.ent",
    "base\\characters\\entities\\player\\player_wa_fpp.ent",
    "base\\characters\\entities\\player\\player_ma_fpp.ent",
};
const char* PartnerBodyTag = "NightCoopPartnerBody";

struct EulerAnglesLayout
{
    float Roll;
    float Pitch;
    float Yaw;
};
static_assert(sizeof(EulerAnglesLayout) == 12);

struct QuaternionLayout
{
    float I;
    float J;
    float K;
    float R;
};
static_assert(sizeof(QuaternionLayout) == 16);

float DistanceBetween(const net::PlayerState& first, const net::PlayerState& second)
{
    const float deltaX = first.PositionX - second.PositionX;
    const float deltaY = first.PositionY - second.PositionY;
    const float deltaZ = first.PositionZ - second.PositionZ;
    return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

float YawDifference(float first, float second)
{
    return std::fabs(std::fmod(first - second + 540.0f, 360.0f) - 180.0f);
}

std::string LastPropertyProblem;

template<typename Value>
bool WriteProperty(RED4ext::CClass* classType, void* instance, const char* name, const char* expectedType, const Value& value)
{
    auto* property = classType->GetProperty(name);
    if (!property || !property->type)
    {
        LastPropertyProblem = std::string(name) + " is missing";
        return false;
    }
    const std::string actualType = property->type->GetName().ToString();
    if (actualType != expectedType || property->type->GetSize() != sizeof(Value))
    {
        LastPropertyProblem = std::string(name) + " is " + actualType + " (" + std::to_string(property->type->GetSize()) + " bytes), wanted " +
                              expectedType;
        return false;
    }
    std::memcpy(static_cast<void*>(property->GetValuePtr<Value>(instance)), &value, sizeof(Value));
    return true;
}
}

RemoteBodies::RemoteBodies(LogFunction log, BodySettings settings)
    : m_log(std::move(log))
    , m_settings(std::move(settings))
{
}

bool RemoteBodies::ResolveApi()
{
    m_resolved = true;
    m_getTeleportationFacility = game::FindFunction("ScriptGameInstance", "GetTeleportationFacility");
    m_teleport = game::FindFunction("gameTeleportationFacility", "Teleport");
    m_getDynamicEntitySystem = game::FindFunction("ScriptGameInstance", "GetDynamicEntitySystem");
    m_isReady = game::FindFunction("DynamicEntitySystem", "IsReady");
    m_createEntity = game::FindFunction("DynamicEntitySystem", "CreateEntity");
    m_getEntity = game::FindFunction("DynamicEntitySystem", "GetEntity");
    m_deleteEntity = game::FindFunction("DynamicEntitySystem", "DeleteEntity");
    m_specClass = RED4ext::CRTTISystem::Get()->GetClass("DynamicEntitySpec");
    m_gameObjectClass = RED4ext::CRTTISystem::Get()->GetClass("gameObject");

    const bool teleportFits = game::ParamsAre(m_getTeleportationFacility, {"ScriptGameInstance"}) &&
                              game::ParamsAre(m_teleport, {"handle:gameObject", "Vector4", "EulerAngles"});
    if (!teleportFits)
    {
        m_log("bodies: the games teleport is not what nightcoop uses (" + game::Describe(m_teleport) + "), no partner bodies");
        m_givenUp = true;
        return false;
    }
    if (!m_getDynamicEntitySystem || !m_createEntity || !m_specClass)
    {
        m_log("bodies: no DynamicEntitySystem, install codeware for mac to see partner bodies");
        m_givenUp = true;
        return false;
    }
    const bool entityApiFits = game::ParamsAre(m_getDynamicEntitySystem, {}) && game::ParamsAre(m_createEntity, {"handle:DynamicEntitySpec"}) &&
                               game::ReturnIs(m_createEntity, "entEntityID") && game::ParamsAre(m_getEntity, {"entEntityID"}) &&
                               game::ParamsAre(m_deleteEntity, {"entEntityID"}) && game::ParamsAre(m_isReady, {});
    if (!entityApiFits || !m_gameObjectClass)
    {
        m_log("bodies: codewares DynamicEntitySystem looks different than expected (" + game::Describe(m_createEntity) + ", " +
              game::Describe(m_getEntity) + "), no partner bodies");
        m_givenUp = true;
        return false;
    }
    if (!partner_body_builder::PrepareForBodies())
    {
        m_log("bodies: the body builder is not ready (its entity build hook or classes are missing), no partner bodies: "
              "an unbuilt body gets its look copied by its impostor and that crashes the game");
        m_givenUp = true;
        return false;
    }
    ResolveLocomotion();
    if (m_settings.PlayerBody && m_settings.Template.empty() && m_settings.Record.empty())
    {
        ResolvePlayerBodies();
    }
    m_log("bodies: partner bodies on, " + (!m_settings.Template.empty() ? "template " + m_settings.Template
                                           : !m_settings.Record.empty() ? "record " + m_settings.Record
                                           : m_playerBodies             ? std::string("a second PlayerPuppet from Character.Player_Puppet_Base")
                                                                        : std::string("the cutscene V template by the partners gender")));
    return true;
}

void RemoteBodies::ResolvePlayerBodies()
{
    m_getPlayer = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
    m_getTemplatePath = game::FindFunction("entEntity", "GetTemplatePath");
    m_queueEvent = game::FindFunction("entEntity", "QueueEvent");
    m_activateTppEventClass = RED4ext::CRTTISystem::Get()->GetClass("gameActivateTPPRepresentationEvent");
    const bool templatePathFits = game::ParamsAre(m_getTemplatePath, {}) && m_getTemplatePath->returnType &&
                                  m_getTemplatePath->returnType->type->GetSize() == sizeof(uint64_t) &&
                                  (game::ReturnIs(m_getTemplatePath, "raRef:CResource") ||
                                   game::ReturnIs(m_getTemplatePath, "redResourceReferenceScriptToken"));
    const bool callsFit = m_getPlayer && templatePathFits && game::ParamsAre(m_queueEvent, {"handle:redEvent"}) && m_activateTppEventClass;
    if (!callsFit)
    {
        m_log("bodies: GetTemplatePath / QueueEvent look different than expected (" + game::Describe(m_getTemplatePath) + ", " +
              game::Describe(m_queueEvent) + "), using the cutscene V body");
        return;
    }
    if (!RED4ext::CRTTISystem::Get()->GetClass("NightCoopPartnerBodies") && !m_settings.PlayerBodyWithoutScripts)
    {
        m_log("bodies: the script side of the PlayerPuppet body is not compiled in (needs redscript for mac and scripts/NightCoopMac), "
              "using the cutscene V body");
        return;
    }
    m_playerBodies = true;
}

std::string RemoteBodies::PlayerBodyTemplate(bool male)
{
    if (m_playerTemplateFamily < 0)
    {
        RED4ext::ScriptGameInstance gameInstance;
        RED4ext::Handle<RED4ext::IScriptable> player;
        RED4ext::StackArgs_t playerArgs;
        playerArgs.emplace_back(nullptr, &gameInstance);
        uint64_t ownTemplate = 0;
        RED4ext::StackArgs_t noArgs;
        if (!game::CallStatic(m_getPlayer, &player, playerArgs) || !player ||
            !game::CallMethod(player.instance, m_getTemplatePath, &ownTemplate, noArgs))
        {
            return {};
        }
        m_playerTemplateFamily = 0;
        for (int index = 0; index < 4; ++index)
        {
            if (RED4ext::ResourcePath(KnownPlayerTemplates[index]).hash == ownTemplate)
            {
                m_playerTemplateFamily = index / 2;
            }
        }
        m_log(std::string("bodies: our player comes from the ") + (m_playerTemplateFamily == 0 ? "ep1" : "base") +
              " player templates, PlayerPuppet bodies get the same family");
    }
    return KnownPlayerTemplates[m_playerTemplateFamily * 2 + (male ? 1 : 0)];
}

bool RemoteBodies::TagAsPartnerBody(RED4ext::IScriptable* spec)
{
    auto* property = m_specClass->GetProperty("tags");
    if (!property || !property->type || std::string(property->type->GetName().ToString()) != "array:CName")
    {
        LastPropertyProblem = "tags is missing or no array:CName";
        return false;
    }
    property->GetValuePtr<RED4ext::DynArray<RED4ext::CName>>(spec)->PushBack(RED4ext::CName(PartnerBodyTag));
    return true;
}

void RemoteBodies::ShowThirdPerson(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body)
{
    auto* activateEvent = static_cast<RED4ext::IScriptable*>(m_activateTppEventClass->CreateInstance(true));
    if (!activateEvent)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> eventHandle(activateEvent);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &eventHandle);
    const bool queued = game::CallMethod(body.instance, m_queueEvent, nullptr, args);
    m_log("bodies: peer " + std::to_string(peerNumber) + (queued ? " third person view on (ActivateTPPRepresentationEvent queued)"
                                                                 : " third person view could not be queued"));
}

bool RemoteBodies::FetchSystems()
{
    RED4ext::ScriptGameInstance gameInstance;
    if (!m_teleportationFacility)
    {
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &gameInstance);
        game::CallStatic(m_getTeleportationFacility, &m_teleportationFacility, args);
    }
    if (!m_dynamicEntitySystem)
    {
        RED4ext::StackArgs_t args;
        game::CallStatic(m_getDynamicEntitySystem, &m_dynamicEntitySystem, args);
    }
    if (!m_teleportationFacility || !m_dynamicEntitySystem)
    {
        return false;
    }
    bool ready = false;
    RED4ext::StackArgs_t args;
    return game::CallMethod(m_dynamicEntitySystem.instance, m_isReady, &ready, args) && ready;
}

bool RemoteBodies::Spawn(const RemotePeerView& peer, uint64_t nowMs)
{
    const uint32_t peerNumber = peer.PeerNumber;
    const net::PlayerState& state = peer.State;
    std::string record = m_settings.Record;
    std::string bodyTemplate = m_settings.Template;
    RED4ext::ResourcePath bodyTemplatePath;
    uint64_t playerBodyRecord = 0;
    bool isPlayerBody = false;
    if (record.empty() && bodyTemplate.empty())
    {
        bool male = peer.IsMale;
        if (m_settings.Gender == "male" || m_settings.Gender == "female")
        {
            male = m_settings.Gender == "male";
        }
        if (m_playerBodies)
        {
            const std::string playerTemplate = PlayerBodyTemplate(male);
            playerBodyRecord = playerTemplate.empty() ? 0 : body_records::PlayerBodyRecord(playerTemplate, male, m_log);
            isPlayerBody = playerBodyRecord != 0;
            record = isPlayerBody ? playerTemplate : std::string();
        }
        if (!isPlayerBody)
        {
            record = male ? MaleBodyRecord : FemaleBodyRecord;
        }
    }
    if (!bodyTemplate.empty())
    {
        bodyTemplatePath = RED4ext::ResourcePath(bodyTemplate.c_str(), bodyTemplate.size());
    }
    auto* spec = static_cast<RED4ext::IScriptable*>(m_specClass->CreateInstance(true));
    if (!spec)
    {
        return false;
    }
    RED4ext::Handle<RED4ext::IScriptable> specHandle(spec);

    const float halfYawRadians = state.Yaw * 3.14159265f / 360.0f;
    const RED4ext::Vector4 position {state.PositionX, state.PositionY, state.PositionZ, 1.0f};
    const QuaternionLayout orientation {0.0f, 0.0f, std::sin(halfYawRadians), std::cos(halfYawRadians)};
    bool written = WriteProperty(m_specClass, spec, "position", "Vector4", position) &&
                   WriteProperty(m_specClass, spec, "orientation", "Quaternion", orientation) &&
                   WriteProperty(m_specClass, spec, "alwaysSpawned", "Bool", true) &&
                   WriteProperty(m_specClass, spec, "spawnInView", "Bool", true) &&
                   WriteProperty(m_specClass, spec, "active", "Bool", true) &&
                   WriteProperty(m_specClass, spec, "persistState", "Bool", false) &&
                   WriteProperty(m_specClass, spec, "persistSpawn", "Bool", false);
    if (isPlayerBody)
    {
        written = written && WriteProperty(m_specClass, spec, "recordID", "TweakDBID", RED4ext::TweakDBID(playerBodyRecord));
    }
    else if (bodyTemplate.empty())
    {
        written = written && WriteProperty(m_specClass, spec, "recordID", "TweakDBID", RED4ext::TweakDBID(record.c_str(), record.size()));
    }
    else
    {
        written = written && WriteProperty(m_specClass, spec, "templatePath", "raRef:CResource", bodyTemplatePath);
    }
    written = written && TagAsPartnerBody(spec);
    if (!written)
    {
        m_log("bodies: DynamicEntitySpec has other fields than expected (" + LastPropertyProblem + "), no partner bodies");
        m_givenUp = true;
        return false;
    }

    RED4ext::ent::EntityID entityId;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &specHandle);
    if (!game::CallMethod(m_dynamicEntitySystem.instance, m_createEntity, &entityId, args) || entityId.hash == 0)
    {
        m_log("bodies: peer " + std::to_string(peerNumber) + " body could not be created, trying again in 10 s");
        m_retryAfterMs[peerNumber] = nowMs + RetryFailedSpawnAfterMs;
        return false;
    }
    partner_body_builder::ExpectBody(entityId.hash);
    if (peer.LookState)
    {
        partner_look::SetPartnerState(entityId.hash, peer.LookState, peer.IsMale);
    }
    auto& puppet = m_puppets[peerNumber];
    puppet = Puppet {};
    puppet.EntityHash = entityId.hash;
    puppet.SpawnRequestedMs = nowMs;
    puppet.IsPlayerBody = isPlayerBody;
    m_log("bodies: peer " + std::to_string(peerNumber) + " joined, spawning a body from " + (bodyTemplate.empty() ? record : bodyTemplate) +
          (peer.LookKnown || !m_settings.Gender.empty() || !m_settings.Record.empty() || !m_settings.Template.empty() ? "" : " (their look did not come in 5 s)"));
    return true;
}

void RemoteBodies::DropPolicy(Puppet& puppet)
{
    if (puppet.MovePolicies && puppet.MovePoliciesComponent)
    {
        RED4ext::StackArgs_t popArgs;
        popArgs.emplace_back(nullptr, &puppet.MovePolicies);
        game::CallMethod(puppet.MovePoliciesComponent.instance, m_popPolicies, nullptr, popArgs);
    }
    puppet.MovePolicies.Reset();
    puppet.MovePoliciesComponent.Reset();
}

bool RemoteBodies::PolicyIsStuck(uint32_t peerNumber, Puppet& puppet, float drift, uint64_t nowMs)
{
    uint32_t action = 0;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(puppet.MovePoliciesComponent.instance, m_getCurrentLocomotionAction, &action, noArgs);
    if (action != 0 || drift <= 1.0f)
    {
        puppet.PolicyIdleSinceMs = 0;
        return false;
    }
    if (puppet.PolicyIdleSinceMs == 0)
    {
        puppet.PolicyIdleSinceMs = nowMs;
        return false;
    }
    if (nowMs - puppet.PolicyIdleSinceMs < 3000)
    {
        return false;
    }
    DropPolicy(puppet);
    m_log("bodies: peer " + std::to_string(peerNumber) + " move policy does nothing here (" + std::to_string(drift).substr(0, 4) +
          " m off for 3 s), falling back to teleport");
    return true;
}

void RemoteBodies::ResolveStance()
{
    m_getSignalTable = game::FindFunction("ScriptedPuppet", "GetSignalTable");
    m_getOrCreateSignal = game::FindFunction("gameBoolSignalTable", "GetOrCreateSignal");
    m_setWithData = game::FindFunction("gameBoolSignalTable", "SetWithData");
    m_stateSignalClass = RED4ext::CRTTISystem::Get()->GetClass("NPCStateChangeSignal");
    auto propertyIs = [this](const char* name, const char* typeName) {
        auto* property = m_stateSignalClass ? m_stateSignalClass->GetProperty(name) : nullptr;
        return property && property->type && std::string(property->type->GetName().ToString()) == typeName;
    };
    m_stanceAvailable = m_scriptedPuppetClass && game::ParamsAre(m_getSignalTable, {}) &&
                        game::ReturnIs(m_getSignalTable, "handle:gameBoolSignalTable") && game::ParamsAre(m_getOrCreateSignal, {"CName"}) &&
                        game::ReturnIs(m_getOrCreateSignal, "Uint16") &&
                        game::ParamsAre(m_setWithData, {"Uint16", "handle:gameSignalUserData"}) && propertyIs("stanceState", "gamedataNPCStanceState") &&
                        propertyIs("stanceStateValid", "Bool") && propertyIs("upperBodyState", "gamedataNPCUpperBodyState") &&
                        propertyIs("upperBodyStateValid", "Bool");
    m_log(m_stanceAvailable ? "bodies: partner crouch / aim / reload go to the body as NPCStateChangeSignal like nightcoop"
                            : "bodies: NPCStateChangeSignal looks different than nightcoops, bodies keep standing");
}

void RemoteBodies::ApplyStance(uint32_t peerNumber, Puppet& puppet, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state,
                               uint64_t nowMs)
{
    if (!m_stanceAvailable)
    {
        return;
    }
    const uint32_t stance = (state.Flags & 0x1) ? NpcStanceCrouch : NpcStanceStand;
    uint32_t upperBody = NpcUpperBodyNormal;
    if (state.Flags & 0x80)
    {
        upperBody = NpcUpperBodyReload;
    }
    else if (state.Flags & (0x4 | 0x40))
    {
        upperBody = NpcUpperBodyAim;
    }
    const bool upperBodyDue = upperBody != puppet.AppliedUpperBody && (upperBody == NpcUpperBodyAim || nowMs - puppet.UpperBodyChangedMs >= 1500);
    if (stance == puppet.AppliedStance && !upperBodyDue)
    {
        return;
    }
    auto* bodyClass = body.instance->GetType();
    if (!bodyClass || !bodyClass->IsA(m_scriptedPuppetClass))
    {
        return;
    }

    RED4ext::Handle<RED4ext::IScriptable> signalTable;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(body.instance, m_getSignalTable, &signalTable, noArgs) || !signalTable)
    {
        return;
    }
    RED4ext::CName signalName("NPCStateChangeSignal");
    uint16_t signalId = 0;
    RED4ext::StackArgs_t signalArgs;
    signalArgs.emplace_back(nullptr, &signalName);
    if (!game::CallMethod(signalTable.instance, m_getOrCreateSignal, &signalId, signalArgs))
    {
        return;
    }
    auto* signalData = static_cast<RED4ext::IScriptable*>(m_stateSignalClass->CreateInstance(true));
    if (!signalData)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> signalDataHandle(signalData);
    const uint32_t appliedUpperBody = upperBodyDue ? upperBody : puppet.AppliedUpperBody;
    const bool written = WriteProperty(m_stateSignalClass, signalData, "stanceState", "gamedataNPCStanceState", stance) &&
                         WriteProperty(m_stateSignalClass, signalData, "stanceStateValid", "Bool", true) &&
                         WriteProperty(m_stateSignalClass, signalData, "upperBodyState", "gamedataNPCUpperBodyState",
                                       appliedUpperBody == 0xFFFFFFFF ? NpcUpperBodyNormal : appliedUpperBody) &&
                         WriteProperty(m_stateSignalClass, signalData, "upperBodyStateValid", "Bool", true);
    if (!written)
    {
        m_log("bodies: NPCStateChangeSignal has other fields than expected (" + LastPropertyProblem + "), bodies keep standing");
        m_stanceAvailable = false;
        return;
    }
    bool accepted = false;
    RED4ext::StackArgs_t setArgs;
    setArgs.emplace_back(nullptr, &signalId);
    setArgs.emplace_back(nullptr, &signalDataHandle);
    game::CallMethod(signalTable.instance, m_setWithData, &accepted, setArgs);

    if (stance != puppet.AppliedStance)
    {
        m_log("bodies: peer " + std::to_string(peerNumber) + (stance == NpcStanceCrouch ? " crouches" : " stands"));
    }
    puppet.AppliedStance = stance;
    if (upperBodyDue)
    {
        puppet.AppliedUpperBody = upperBody;
        puppet.UpperBodyChangedMs = nowMs;
    }
}

void RemoteBodies::Despawn(uint32_t peerNumber, Puppet& puppet, const char* why)
{
    DropPolicy(puppet);

    RED4ext::ent::EntityID entityId(puppet.EntityHash);
    bool deleted = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    game::CallMethod(m_dynamicEntitySystem.instance, m_deleteEntity, &deleted, args);
    partner_body_builder::ForgetBody(puppet.EntityHash);
    partner_look::ForgetPartnerState(puppet.EntityHash);
    m_log("bodies: peer " + std::to_string(peerNumber) + " " + why + (deleted ? "" : " (the game had it gone already)"));
}

void RemoteBodies::ResolveLocomotion()
{
    m_getWorldPosition = game::FindFunction("entEntity", "GetWorldPosition");
    m_getMovePoliciesComponent = game::FindFunction("ScriptedPuppet", "GetMovePolicesComponent");
    m_scriptedPuppetClass = RED4ext::CRTTISystem::Get()->GetClass("ScriptedPuppet");
    m_movePoliciesClass = RED4ext::CRTTISystem::Get()->GetClass("movePolicies");
    m_setDestinationPosition = game::FindFunction("movePolicies", "SetDestinationPosition");
    m_setStrafingPosition = game::FindFunction("movePolicies", "SetStrafingPosition");
    m_setIgnoreNavigation = game::FindFunction("movePolicies", "SetIgnoreNavigation");
    m_setUseStartStop = game::FindFunction("movePolicies", "SetUseStartStop");
    m_setCollisionAvoidancePolicy = game::FindFunction("movePolicies", "SetCollisionAvoidancePolicy");
    m_setMovementType = game::FindFunction("movePolicies", "SetMovementType");
    m_addPolicies = game::FindFunction("movePoliciesComponent", "AddPolicies");
    m_popPolicies = game::FindFunction("movePoliciesComponent", "PopPolicies");
    m_changeMovementType = game::FindFunction("movePoliciesComponent", "ChangeMovementType");
    m_getCurrentLocomotionAction = game::FindFunction("movePoliciesComponent", "GetCurrentLocomotionAction");
    m_getDistanceToDestination = game::FindFunction("movePoliciesComponent", "GetDistanceToDestination");
    m_isPathfindingFailed = game::FindFunction("movePoliciesComponent", "IsPathfindingFailed");
    m_isPausedByDynamicCollision = game::FindFunction("movePoliciesComponent", "IsPausedByDynamicCollision");

    m_locomotionAvailable =
        m_scriptedPuppetClass && m_movePoliciesClass && game::ParamsAre(m_getWorldPosition, {}) && game::ReturnIs(m_getWorldPosition, "Vector4") &&
        game::ParamsAre(m_getMovePoliciesComponent, {}) && game::ReturnIs(m_getMovePoliciesComponent, "handle:movePoliciesComponent") &&
        game::ParamsAre(m_setDestinationPosition, {"Vector4"}) && game::ParamsAre(m_setStrafingPosition, {"Vector4"}) &&
        game::ParamsAre(m_setIgnoreNavigation, {"Bool"}) && game::ParamsAre(m_setUseStartStop, {"Bool", "Bool"}) &&
        game::ParamsAre(m_setCollisionAvoidancePolicy, {"Bool", "Bool"}) && game::ParamsAre(m_setMovementType, {"moveMovementType"}) &&
        game::ParamsAre(m_addPolicies, {"handle:movePolicies"}) && game::ParamsAre(m_popPolicies, {"handle:movePolicies"}) &&
        game::ParamsAre(m_changeMovementType, {"moveMovementType"}) && game::ReturnIs(m_changeMovementType, "Bool") &&
        game::ParamsAre(m_getCurrentLocomotionAction, {}) && game::ParamsAre(m_getDistanceToDestination, {}) &&
        game::ParamsAre(m_isPathfindingFailed, {}) && game::ParamsAre(m_isPausedByDynamicCollision, {});
    m_log(m_locomotionAvailable ? "bodies: npc bodies walk with the games move policies like nightcoop"
                                : "bodies: the move policies look different than nightcoops, bodies only get teleported");
    ResolveStance();
}

uint32_t RemoteBodies::PickMovementType(Puppet& puppet, const net::PlayerState& state, uint64_t nowMs)
{
    puppet.SmoothedSpeed = puppet.SmoothedSpeed * 0.8f + state.Speed * 0.2f;
    const float speed = puppet.SmoothedSpeed;
    const uint32_t current = puppet.MovementType;
    uint32_t wanted = current;
    if (speed >= 5.8f)
    {
        wanted = 2;
    }
    else if (current == 2)
    {
        wanted = speed >= 4.8f ? 2 : 1;
    }
    else if (current == 0)
    {
        wanted = speed < 2.8f ? 0 : 1;
    }
    else if (current == 1)
    {
        wanted = speed >= 2.0f ? 1 : 0;
    }
    if (wanted == current || nowMs - puppet.MovementTypeChangedMs < 500)
    {
        return current;
    }
    puppet.MovementType = wanted;
    puppet.MovementTypeChangedMs = nowMs;
    return wanted;
}

bool RemoteBodies::EnsurePolicy(uint32_t peerNumber, Puppet& puppet, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state)
{
    if (puppet.MovePolicies)
    {
        return true;
    }
    if (puppet.PolicyTried || !m_locomotionAvailable)
    {
        return false;
    }
    puppet.PolicyTried = true;
    auto* bodyClass = body.instance->GetType();
    if (!bodyClass || !bodyClass->IsA(m_scriptedPuppetClass))
    {
        m_log("bodies: peer " + std::to_string(peerNumber) + " body is no ScriptedPuppet, move policy unavailable, falling back to teleport");
        return false;
    }
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(body.instance, m_getMovePoliciesComponent, &puppet.MovePoliciesComponent, noArgs) || !puppet.MovePoliciesComponent)
    {
        m_log("bodies: peer " + std::to_string(peerNumber) + " body has no movePoliciesComponent, falling back to teleport");
        return false;
    }
    auto* policies = static_cast<RED4ext::IScriptable*>(m_movePoliciesClass->CreateInstance(true));
    if (!policies)
    {
        return false;
    }
    RED4ext::Handle<RED4ext::IScriptable> policiesHandle(policies);

    bool ignoreNavigation = true;
    bool useStart = true;
    bool useStop = true;
    bool collisionAvoidance = false;
    bool destinationReservation = false;
    uint32_t movementType = puppet.MovementType;
    RED4ext::Vector4 destination {state.PositionX, state.PositionY, state.PositionZ, 1.0f};

    RED4ext::StackArgs_t ignoreArgs;
    ignoreArgs.emplace_back(nullptr, &ignoreNavigation);
    RED4ext::StackArgs_t startStopArgs;
    startStopArgs.emplace_back(nullptr, &useStart);
    startStopArgs.emplace_back(nullptr, &useStop);
    RED4ext::StackArgs_t avoidanceArgs;
    avoidanceArgs.emplace_back(nullptr, &collisionAvoidance);
    avoidanceArgs.emplace_back(nullptr, &destinationReservation);
    RED4ext::StackArgs_t typeArgs;
    typeArgs.emplace_back(nullptr, &movementType);
    RED4ext::StackArgs_t destinationArgs;
    destinationArgs.emplace_back(nullptr, &destination);
    const bool configured = game::CallMethod(policies, m_setIgnoreNavigation, nullptr, ignoreArgs) &&
                            game::CallMethod(policies, m_setUseStartStop, nullptr, startStopArgs) &&
                            game::CallMethod(policies, m_setCollisionAvoidancePolicy, nullptr, avoidanceArgs) &&
                            game::CallMethod(policies, m_setMovementType, nullptr, typeArgs) &&
                            game::CallMethod(policies, m_setDestinationPosition, nullptr, destinationArgs);
    RED4ext::StackArgs_t addArgs;
    addArgs.emplace_back(nullptr, &policiesHandle);
    if (!configured || !game::CallMethod(puppet.MovePoliciesComponent.instance, m_addPolicies, nullptr, addArgs))
    {
        m_log("bodies: peer " + std::to_string(peerNumber) + " move policy could not be set up, falling back to teleport");
        puppet.MovePoliciesComponent.Reset();
        return false;
    }
    puppet.MovePolicies = policiesHandle;
    puppet.AppliedMovementType = movementType;
    m_log("bodies: peer " + std::to_string(peerNumber) + " move policy added");
    return true;
}

void RemoteBodies::ReportPolicy(uint32_t peerNumber, Puppet& puppet, float drift, uint64_t nowMs)
{
    if (nowMs < puppet.NextReportMs || !puppet.MovePoliciesComponent)
    {
        return;
    }
    puppet.NextReportMs = nowMs + 5000;
    static const char* LocomotionActionNames[] = {"Undefined", "Exploration", "Idle", "IdleTurn", "Reposition", "Start", "Move", "Stop"};
    uint32_t action = 0;
    float toDestination = 0;
    bool pathFailed = false;
    bool paused = false;
    RED4ext::StackArgs_t noArgs;
    void* component = puppet.MovePoliciesComponent.instance;
    game::CallMethod(component, m_getCurrentLocomotionAction, &action, noArgs);
    game::CallMethod(component, m_getDistanceToDestination, &toDestination, noArgs);
    game::CallMethod(component, m_isPathfindingFailed, &pathFailed, noArgs);
    game::CallMethod(component, m_isPausedByDynamicCollision, &paused, noArgs);
    char line[240];
    std::snprintf(line, sizeof(line),
                  "bodies: peer %u policy: drift %.2f m, action %s, toDest %.2f, pathFailed %d, paused %d, speed %.2f, type %u, teleports %u, shots played %u",
                  peerNumber, drift, action < 8 ? LocomotionActionNames[action] : "?", toDestination, pathFailed, paused, puppet.SmoothedSpeed,
                  puppet.AppliedMovementType, puppet.Teleports, puppet.Weapons.ShotsPlayed);
    m_log(line);
}

void RemoteBodies::Advance(uint32_t peerNumber, Puppet& puppet, const net::PlayerState& state, uint64_t nowMs)
{
    RED4ext::ent::EntityID entityId(puppet.EntityHash);
    RED4ext::Handle<RED4ext::IScriptable> body;
    RED4ext::StackArgs_t getArgs;
    getArgs.emplace_back(nullptr, &entityId);
    if (!game::CallMethod(m_dynamicEntitySystem.instance, m_getEntity, &body, getArgs) || !body)
    {
        return;
    }
    if (!puppet.InWorld)
    {
        auto* bodyClass = body.instance->GetType();
        if (!bodyClass || !bodyClass->IsA(m_gameObjectClass))
        {
            m_log("bodies: peer " + std::to_string(peerNumber) + " body is not a gameObject, cant move it");
            return;
        }
        puppet.InWorld = true;
        puppet.InWorldMs = nowMs;
        std::string classChain;
        for (auto* classType = bodyClass; classType; classType = classType->parent)
        {
            classChain += (classChain.empty() ? "" : " < ") + std::string(classType->name.ToString());
        }
        m_log("bodies: peer " + std::to_string(peerNumber) + " body is in the world (" + classChain + ")");
        if (puppet.IsPlayerBody)
        {
            uint64_t bodyTemplate = 0;
            RED4ext::StackArgs_t templateArgs;
            game::CallMethod(body.instance, m_getTemplatePath, &bodyTemplate, templateArgs);
            std::string templateName = "an unknown template";
            for (const char* path : KnownPlayerTemplates)
            {
                if (RED4ext::ResourcePath(path).hash == bodyTemplate)
                {
                    templateName = path;
                }
            }
            m_log("bodies: peer " + std::to_string(peerNumber) + " PlayerPuppet body comes from " + templateName);
            ShowThirdPerson(peerNumber, body);
        }
    }

    if (!puppet.IsPlayerBody)
    {
        ApplyStance(peerNumber, puppet, body, state, nowMs);
    }
    const uint32_t movementType = PickMovementType(puppet, state, nowMs);
    RED4ext::Vector4 bodyPosition {};
    RED4ext::StackArgs_t noArgs;
    if (!m_locomotionAvailable || !game::CallMethod(body.instance, m_getWorldPosition, &bodyPosition, noArgs))
    {
        MoveTo(puppet, body, state);
        return;
    }
    if (std::fabs(bodyPosition.X + 16384.0f) < 1.0f && std::fabs(bodyPosition.Y + 16384.0f) < 1.0f && std::fabs(bodyPosition.Z + 16384.0f) < 1.0f)
    {
        if (!puppet.OutOfWorldLogged)
        {
            puppet.OutOfWorldLogged = true;
            m_log("bodies: peer " + std::to_string(peerNumber) + " is out of the world");
        }
        return;
    }
    puppet.OutOfWorldLogged = false;

    const float driftX = state.PositionX - bodyPosition.X;
    const float driftY = state.PositionY - bodyPosition.Y;
    const float driftZ = state.PositionZ - bodyPosition.Z;
    const float drift = std::sqrt(driftX * driftX + driftY * driftY + driftZ * driftZ);
    if (puppet.IsPlayerBody || drift > SnapBeyondMeters || !EnsurePolicy(peerNumber, puppet, body, state) ||
        PolicyIsStuck(peerNumber, puppet, drift, nowMs))
    {
        MoveTo(puppet, body, state);
        ReportPolicy(peerNumber, puppet, drift, nowMs);
        return;
    }

    RED4ext::Vector4 destination {state.PositionX, state.PositionY, state.PositionZ, 1.0f};
    RED4ext::StackArgs_t destinationArgs;
    destinationArgs.emplace_back(nullptr, &destination);
    game::CallMethod(puppet.MovePolicies.instance, m_setDestinationPosition, nullptr, destinationArgs);

    const float yawRadians = (state.Yaw + state.AimYawOffset) * 0.017453292f;
    const float pitchRadians = state.CameraPitch * 0.017453292f;
    RED4ext::Vector4 lookPoint {state.PositionX - std::sin(yawRadians) * std::cos(pitchRadians) * 12.0f,
                                state.PositionY + std::cos(yawRadians) * std::cos(pitchRadians) * 12.0f,
                                state.PositionZ + std::sin(pitchRadians) * 12.0f + 1.5f, 1.0f};
    RED4ext::StackArgs_t strafingArgs;
    strafingArgs.emplace_back(nullptr, &lookPoint);
    game::CallMethod(puppet.MovePolicies.instance, m_setStrafingPosition, nullptr, strafingArgs);

    if (movementType != puppet.AppliedMovementType)
    {
        uint32_t wantedType = movementType;
        bool accepted = false;
        RED4ext::StackArgs_t typeArgs;
        typeArgs.emplace_back(nullptr, &wantedType);
        game::CallMethod(puppet.MovePoliciesComponent.instance, m_changeMovementType, &accepted, typeArgs);
        puppet.AppliedMovementType = movementType;
        static const char* MovementTypeNames[] = {"walk", "run", "sprint"};
        m_log("bodies: peer " + std::to_string(peerNumber) + " moves at " + MovementTypeNames[movementType] + " now (" +
              (accepted ? "ok" : "refused") + ")");
    }
    ReportPolicy(peerNumber, puppet, drift, nowMs);
}

void RemoteBodies::MoveTo(Puppet& puppet, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state)
{
    if (puppet.Placed)
    {
        const float deltaX = state.PositionX - puppet.PlacedX;
        const float deltaY = state.PositionY - puppet.PlacedY;
        const float deltaZ = state.PositionZ - puppet.PlacedZ;
        if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ < MoveAtLeastSquaredMeters &&
            YawDifference(state.Yaw, puppet.PlacedYaw) < TurnAtLeastDegrees)
        {
            return;
        }
    }

    RED4ext::Vector4 position {state.PositionX, state.PositionY, state.PositionZ, 1.0f};
    EulerAnglesLayout orientation {0.0f, 0.0f, state.Yaw};
    RED4ext::StackArgs_t teleportArgs;
    teleportArgs.emplace_back(nullptr, &body);
    teleportArgs.emplace_back(nullptr, &position);
    teleportArgs.emplace_back(nullptr, &orientation);
    if (!game::CallMethod(m_teleportationFacility.instance, m_teleport, nullptr, teleportArgs))
    {
        return;
    }
    puppet.Placed = true;
    puppet.PlacedX = state.PositionX;
    puppet.PlacedY = state.PositionY;
    puppet.PlacedZ = state.PositionZ;
    puppet.PlacedYaw = state.Yaw;
    ++puppet.Teleports;
}

void RemoteBodies::Tick(uint64_t nowMs, const std::vector<RemotePeerView>& peers, const net::PlayerState* localPlayer, bool bodiesAllowed)
{
    for (const auto& line : partner_body_builder::TakeLogLines())
    {
        m_log(line);
    }
    if (!m_settings.Enabled || m_givenUp)
    {
        return;
    }
    if (!localPlayer)
    {
        if (!m_puppets.empty())
        {
            ForgetAll("forgotten, the world is unloading");
        }
        return;
    }
    if (!bodiesAllowed)
    {
        if (!m_puppets.empty() && m_dynamicEntitySystem)
        {
            for (auto& [peerNumber, puppet] : m_puppets)
            {
                Despawn(peerNumber, puppet, "body removed, no partner bodies outside a loaded save");
            }
        }
        m_puppets.clear();
        return;
    }
    if (peers.empty() && m_puppets.empty())
    {
        return;
    }
    if (!m_resolved && !ResolveApi())
    {
        return;
    }
    if (!FetchSystems())
    {
        return;
    }

    std::map<uint32_t, const net::PlayerState*> present;
    std::map<uint32_t, const RemotePeerView*> views;
    for (const auto& peer : peers)
    {
        present[peer.PeerNumber] = &peer.State;
        views[peer.PeerNumber] = &peer;
        m_firstSeenMs.try_emplace(peer.PeerNumber, nowMs);
    }
    std::erase_if(m_firstSeenMs, [&present](const auto& entry) { return !present.count(entry.first); });

    for (auto puppetEntry = m_puppets.begin(); puppetEntry != m_puppets.end();)
    {
        const uint32_t peerNumber = puppetEntry->first;
        auto& puppet = puppetEntry->second;
        const auto peer = present.find(peerNumber);
        if (peer == present.end())
        {
            Despawn(peerNumber, puppet, "left, despawning");
            puppetEntry = m_puppets.erase(puppetEntry);
            continue;
        }
        const float distance = DistanceBetween(*peer->second, *localPlayer);
        if (distance > RemoveBeyondMeters)
        {
            Despawn(peerNumber, puppet, ("is " + std::to_string(static_cast<int>(distance)) + " m away, their body is removed until they come closer").c_str());
            puppetEntry = m_puppets.erase(puppetEntry);
            continue;
        }
        if (!puppet.InWorld && nowMs - puppet.SpawnRequestedMs > GiveUpSpawningAfterMs)
        {
            Despawn(peerNumber, puppet, "body never came into the world, it will be created again");
            puppetEntry = m_puppets.erase(puppetEntry);
            continue;
        }
        Advance(peerNumber, puppet, *peer->second, nowMs);
        DressIfNeeded(peerNumber, puppet, *views[peerNumber], nowMs);
        ArmIfNeeded(peerNumber, puppet, *peer->second, nowMs);
        ++puppetEntry;
    }

    for (const auto& peer : peers)
    {
        const uint32_t peerNumber = peer.PeerNumber;
        if (m_puppets.count(peerNumber) || DistanceBetween(peer.State, *localPlayer) > SpawnWithinMeters)
        {
            continue;
        }
        const bool needsLook = m_settings.Record.empty() && m_settings.Template.empty() && m_settings.Gender.empty();
        if (needsLook && !peer.LookKnown && nowMs - m_firstSeenMs[peerNumber] < WaitForLookMs)
        {
            continue;
        }
        const auto retry = m_retryAfterMs.find(peerNumber);
        if (retry != m_retryAfterMs.end() && nowMs < retry->second)
        {
            continue;
        }
        if (!Spawn(peer, nowMs) && m_givenUp)
        {
            return;
        }
    }
}

bool RemoteBodies::GetBody(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body)
{
    const auto puppet = m_puppets.find(peerNumber);
    if (puppet == m_puppets.end() || !puppet->second.InWorld || !m_dynamicEntitySystem)
    {
        return false;
    }
    RED4ext::ent::EntityID entityId(puppet->second.EntityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    return game::CallMethod(m_dynamicEntitySystem.instance, m_getEntity, &body, args) && body;
}

void RemoteBodies::DressIfNeeded(uint32_t peerNumber, Puppet& puppet, const RemotePeerView& peer, uint64_t nowMs)
{
    if (!puppet.InWorld || !peer.Clothes)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> body;
    if (!GetBody(peerNumber, body))
    {
        return;
    }
    if (puppet.DressedFingerprint != peer.ClothesFingerprint)
    {
        if (puppet.IsPlayerBody && !wardrobe::HeadIsThirdPerson(body) && nowMs - puppet.InWorldMs < 8000)
        {
            if (!puppet.HeadWaitLogged)
            {
                puppet.HeadWaitLogged = true;
                m_log("bodies: peer " + std::to_string(peerNumber) + " clothes wait for the third person head (8 s at most)");
            }
            return;
        }
        if (puppet.HeadWaitLogged && puppet.DressedFingerprint == 0)
        {
            m_log("bodies: peer " + std::to_string(peerNumber) + " head wait over (" + wardrobe::DescribeHead(body) + ")");
        }
        const uint32_t placed = wardrobe::Dress(body, *peer.Clothes, puppet.Worn, m_log);
        puppet.DressedFingerprint = peer.ClothesFingerprint;
        puppet.ThirdPersonPasses = 0;
        puppet.NextThirdPersonPassMs = nowMs + 500;
        m_log("bodies: peer " + std::to_string(peerNumber) + " dressed: " + std::to_string(placed) + " piece(s) put on, " +
              std::to_string(puppet.Worn.RecordBySlot.size()) + " worn" + (puppet.Worn.Arms ? " plus arms" : "") +
              (puppet.Worn.Head ? " plus a third person head" : "") + " (" + wardrobe::DescribeHead(body) + ")");
    }
    if (puppet.ThirdPersonPasses < 3 && nowMs >= puppet.NextThirdPersonPassMs)
    {
        ++puppet.ThirdPersonPasses;
        puppet.NextThirdPersonPassMs = nowMs + 1000;
        const uint32_t turned = wardrobe::TurnToThirdPerson(body);
        m_log("bodies: peer " + std::to_string(peerNumber) + " third person pass " + std::to_string(puppet.ThirdPersonPasses) + ": " +
              std::to_string(turned) + " first person appearance(s) turned, " + wardrobe::DescribeHead(body));
    }
}

void RemoteBodies::ArmIfNeeded(uint32_t peerNumber, Puppet& puppet, const net::PlayerState& state, uint64_t nowMs)
{
    if (!puppet.InWorld || nowMs - puppet.InWorldMs < 1000)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> body;
    if (GetBody(peerNumber, body))
    {
        weapons::Hold(peerNumber, body, state, puppet.Weapons, nowMs, m_log);
    }
}

void RemoteBodies::UseLook(uint32_t peerNumber, const RED4ext::Handle<RED4ext::IScriptable>& state, bool isMale)
{
    const auto puppet = m_puppets.find(peerNumber);
    if (puppet != m_puppets.end() && state)
    {
        partner_look::SetPartnerState(puppet->second.EntityHash, state, isMale);
    }
}

void RemoteBodies::RemoveAll()
{
    if (!m_dynamicEntitySystem)
    {
        m_puppets.clear();
        return;
    }
    for (auto& [peerNumber, puppet] : m_puppets)
    {
        Despawn(peerNumber, puppet, "body removed, the session ended");
    }
    m_puppets.clear();
}

void RemoteBodies::ForgetAll(const char* why, bool gameIsClosing)
{
    for (const auto& [peerNumber, puppet] : m_puppets)
    {
        m_log("bodies: peer " + std::to_string(peerNumber) + " " + why);
    }
    if (gameIsClosing)
    {
        new std::map<uint32_t, Puppet>(std::move(m_puppets));
        new RED4ext::Handle<RED4ext::IScriptable>(std::move(m_teleportationFacility));
        new RED4ext::Handle<RED4ext::IScriptable>(std::move(m_dynamicEntitySystem));
    }
    m_puppets.clear();
    m_retryAfterMs.clear();
    m_firstSeenMs.clear();
    partner_body_builder::ForgetAllBodies();
    partner_look::ForgetAllPartnerStates();
    m_playerTemplateFamily = -1;
    m_teleportationFacility.Reset();
    m_dynamicEntitySystem.Reset();
}
}
