#include "plugin/PlayerSampler.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <cmath>

namespace nightcoop_mac
{
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
    return true;
}
}
