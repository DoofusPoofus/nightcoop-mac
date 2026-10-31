#include "plugin/PlayerSampler.hpp"

#include "plugin/Weapons.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <cmath>

namespace nightcoop_mac
{
namespace
{
constexpr float SprintAboveMetersPerSecond = 6.5f;
constexpr uint16_t CrouchFlag = 1;
constexpr uint16_t SprintFlag = 2;
constexpr uint8_t PsmLocomotionDefault = 0;
constexpr uint8_t PsmLocomotionCrouch = 1;
constexpr uint8_t PsmLocomotionSprint = 2;
}

bool PlayerSampler::ResolveFunctions(void* playerInstance)
{
    auto* player = static_cast<RED4ext::IScriptable*>(playerInstance);
    RED4ext::CClass* playerClass = player->GetType();
    if (!playerClass)
    {
        return false;
    }
    m_getWorldPosition = playerClass->GetFunction("GetWorldPosition");
    m_getWorldYaw = playerClass->GetFunction("GetWorldYaw");
    if (!m_getWorldPosition || !m_getWorldYaw)
    {
        m_log(std::string("player sampler: no ") + (!m_getWorldPosition ? "GetWorldPosition" : "GetWorldYaw") +
              " on the player class, not sending our position");
        m_givenUp = true;
        return false;
    }
    m_resolved = true;
    m_log("player sampler: found GetWorldPosition and GetWorldYaw");
    return true;
}

bool PlayerSampler::Sample(net::PlayerState& state, uint64_t nowMs)
{
    if (m_givenUp)
    {
        return false;
    }
    if (!m_getPlayer)
    {
        m_getPlayer = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
        if (!m_getPlayer)
        {
            m_log("player sampler: no GetPlayer;GameInstance, not sending our position");
            m_givenUp = true;
            return false;
        }
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t getPlayerArgs;
    getPlayerArgs.emplace_back(nullptr, &gameInstance);
    if (!RED4ext::ExecuteFunction(static_cast<void*>(nullptr), m_getPlayer, &player, getPlayerArgs) || !player)
    {
        m_hasLastSample = false;
        return false;
    }
    if (!m_resolved && !ResolveFunctions(player.instance))
    {
        return false;
    }

    RED4ext::Vector4 position {};
    float yaw = 0;
    if (!RED4ext::ExecuteFunction(player.instance, m_getWorldPosition, &position) ||
        !RED4ext::ExecuteFunction(player.instance, m_getWorldYaw, &yaw))
    {
        return false;
    }

    float speed = 0;
    if (m_hasLastSample && nowMs > m_lastSampleMs)
    {
        const float deltaX = position.X - m_lastX;
        const float deltaY = position.Y - m_lastY;
        const float deltaZ = position.Z - m_lastZ;
        const float seconds = static_cast<float>(nowMs - m_lastSampleMs) / 1000.0f;
        speed = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) / seconds;
    }
    m_lastX = position.X;
    m_lastY = position.Y;
    m_lastZ = position.Z;
    m_lastSampleMs = nowMs;
    m_hasLastSample = true;

    state.PositionX = position.X;
    state.PositionY = position.Y;
    state.PositionZ = position.Z;
    state.Yaw = yaw;
    state.Speed = speed;

    if (!m_inCrouchChecked)
    {
        m_inCrouchChecked = true;
        auto* playerClass = player.instance->GetType();
        m_inCrouch = playerClass ? playerClass->GetProperty("inCrouch") : nullptr;
        if (m_inCrouch && (!m_inCrouch->type || std::string(m_inCrouch->type->GetName().ToString()) != "Bool"))
        {
            m_inCrouch = nullptr;
        }
        m_log(m_inCrouch ? "player sampler: crouch comes from PlayerPuppet.inCrouch" : "player sampler: no inCrouch, partners will always see us standing");
    }
    const bool crouching = m_inCrouch && m_inCrouch->GetValue<bool>(player.instance);
    const bool sprinting = !crouching && speed > SprintAboveMetersPerSecond;
    state.Flags = static_cast<uint16_t>((crouching ? CrouchFlag : 0) | (sprinting ? SprintFlag : 0));
    state.LocomotionState = crouching ? PsmLocomotionCrouch : (sprinting ? PsmLocomotionSprint : PsmLocomotionDefault);
    weapons::OwnWeapons ownWeapons;
    if (weapons::ReadOwnWeapons(player, ownWeapons, m_log))
    {
        weapons::PutInState(ownWeapons, state);
    }
    return true;
}
}
