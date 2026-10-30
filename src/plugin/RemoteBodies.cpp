#include "plugin/RemoteBodies.hpp"

#include "plugin/GameCalls.hpp"

#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <cmath>
#include <cstring>

namespace nightcoop_mac
{
namespace
{
constexpr float SpawnWithinMeters = 150.0f;
constexpr float RemoveBeyondMeters = 200.0f;
constexpr float MoveAtLeastSquaredMeters = 0.0004f;
constexpr float TurnAtLeastDegrees = 0.5f;
constexpr uint64_t GiveUpSpawningAfterMs = 30000;
constexpr uint64_t RetryFailedSpawnAfterMs = 10000;

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
    std::memcpy(reinterpret_cast<uint8_t*>(instance) + property->valueOffset, &value, sizeof(Value));
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
    m_log("bodies: partner bodies on, " + (m_settings.Template.empty() ? "record " + m_settings.Record : "template " + m_settings.Template) +
          ", moved by the games teleport like nightcoop");
    return true;
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

bool RemoteBodies::Spawn(uint32_t peerNumber, const net::PlayerState& state, uint64_t nowMs)
{
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
    if (m_settings.Template.empty())
    {
        written = written && WriteProperty(m_specClass, spec, "recordID", "TweakDBID",
                                           RED4ext::TweakDBID(m_settings.Record.c_str(), m_settings.Record.size()));
    }
    else
    {
        written = written && WriteProperty(m_specClass, spec, "templatePath", "ResRef",
                                           RED4ext::ResourcePath(m_settings.Template.c_str(), m_settings.Template.size()));
    }
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
    auto& puppet = m_puppets[peerNumber];
    puppet = Puppet {};
    puppet.EntityHash = entityId.hash;
    puppet.SpawnRequestedMs = nowMs;
    m_log("bodies: peer " + std::to_string(peerNumber) + " joined, spawning a body");
    return true;
}

void RemoteBodies::Despawn(uint32_t peerNumber, Puppet& puppet, const char* why)
{
    RED4ext::ent::EntityID entityId(puppet.EntityHash);
    bool deleted = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    game::CallMethod(m_dynamicEntitySystem.instance, m_deleteEntity, &deleted, args);
    m_log("bodies: peer " + std::to_string(peerNumber) + " " + why + (deleted ? "" : " (the game had it gone already)"));
}

void RemoteBodies::MoveTo(uint32_t peerNumber, Puppet& puppet, const net::PlayerState& state)
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
            m_log("bodies: peer " + std::to_string(peerNumber) + " body is not a gameObject, cant teleport it");
            return;
        }
        puppet.InWorld = true;
        m_log("bodies: peer " + std::to_string(peerNumber) + " body is in the world");
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

void RemoteBodies::Tick(uint64_t nowMs, const std::vector<RemotePeerView>& peers, const net::PlayerState* localPlayer)
{
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
    for (const auto& peer : peers)
    {
        present[peer.PeerNumber] = &peer.State;
    }

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
        MoveTo(peerNumber, puppet, *peer->second);
        ++puppetEntry;
    }

    for (const auto& [peerNumber, state] : present)
    {
        if (m_puppets.count(peerNumber) || DistanceBetween(*state, *localPlayer) > SpawnWithinMeters)
        {
            continue;
        }
        const auto retry = m_retryAfterMs.find(peerNumber);
        if (retry != m_retryAfterMs.end() && nowMs < retry->second)
        {
            continue;
        }
        if (!Spawn(peerNumber, *state, nowMs) && m_givenUp)
        {
            return;
        }
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

void RemoteBodies::ForgetAll(const char* why)
{
    for (const auto& [peerNumber, puppet] : m_puppets)
    {
        m_log("bodies: peer " + std::to_string(peerNumber) + " " + why);
    }
    m_puppets.clear();
    m_retryAfterMs.clear();
    m_teleportationFacility.Reset();
    m_dynamicEntitySystem.Reset();
}
}
