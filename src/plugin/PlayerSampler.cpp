#include "plugin/PlayerSampler.hpp"

#include "plugin/StateMachineBoard.hpp"
#include "plugin/Weapons.hpp"

#include "plugin/GameCalls.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/StatsObjectID.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nightcoop_mac
{
namespace
{
constexpr float SprintAboveMetersPerSecond = 6.5f;
constexpr float TeleportAboveMetersPerSecond = 200.0f;
constexpr float DegreesPerRadian = 57.29578f;
constexpr uint16_t CrouchFlag = 1;
constexpr uint16_t SprintFlag = 2;
constexpr uint16_t InVehicleFlag = 0x10;
constexpr uint16_t DownFlag = 0x100;
constexpr uint16_t SandevistanFlag = 0x200;
constexpr int32_t VitalsDead = 1;
constexpr int32_t TimeDilationSandevistan = 1;
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

void PlayerSampler::ResolveExtras()
{
    m_extrasResolved = true;
    m_getEntityId = game::FindFunction("entEntity", "GetEntityID");
    m_getStatPoolsSystem = game::FindFunction("ScriptGameInstance", "GetStatPoolsSystem");
    m_getStatPoolValue = game::FindFunction("gameStatPoolsSystem", "GetStatPoolValue");
    m_getTimeDilation = game::FindFunction("gameTimeDilatable", "GetTimeDilationValue");
    m_getCameraSystem = game::FindFunction("ScriptGameInstance", "GetCameraSystem");
    m_getCameraForward = game::FindFunction("gameCameraSystem", "GetActiveCameraForward");
    if (auto* pools = RED4ext::CRTTISystem::Get()->GetEnum("gamedataStatPoolType"))
    {
        const RED4ext::CName health("Health");
        for (uint32_t index = 0; index < pools->hashList.Size() && index < pools->valueList.Size(); ++index)
        {
            if (pools->hashList[index] == health)
            {
                m_healthPool = static_cast<uint32_t>(pools->valueList[index]);
                m_healthKnown = true;
            }
        }
    }
    m_healthKnown = m_healthKnown && game::ParamsAre(m_getEntityId, {}) && game::ParamsAre(m_getStatPoolsSystem, {"ScriptGameInstance"}) &&
                    game::ParamsAre(m_getStatPoolValue, {"gameStatsObjectID", "gamedataStatPoolType", "Bool"});
    if (!game::ParamsAre(m_getTimeDilation, {}))
    {
        m_getTimeDilation = nullptr;
    }
    if (!game::ParamsAre(m_getCameraSystem, {"ScriptGameInstance"}) || !game::ParamsAre(m_getCameraForward, {}))
    {
        m_getCameraForward = nullptr;
    }
    m_log(std::string("player sampler: health ") + (m_healthKnown ? "from the Health stat pool" : "not readable") + ", time dilation " +
          (m_getTimeDilation ? "read" : "not readable") + ", camera pitch and aim offset " + (m_getCameraForward ? "from the active camera" : "not readable"));
}

void PlayerSampler::SampleExtras(void* playerInstance, net::PlayerState& state, uint64_t nowMs)
{
    if (!m_extrasResolved)
    {
        ResolveExtras();
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::StackArgs_t noArgs;
    if (m_healthKnown && nowMs >= m_nextHealthMs)
    {
        m_nextHealthMs = nowMs + 250;
        uint64_t entityId = 0;
        RED4ext::Handle<RED4ext::IScriptable> pools;
        RED4ext::StackArgs_t poolsArgs;
        poolsArgs.emplace_back(nullptr, &gameInstance);
        if (game::CallMethod(playerInstance, m_getEntityId, &entityId, noArgs) && game::CallStatic(m_getStatPoolsSystem, &pools, poolsArgs) && pools)
        {
            RED4ext::game::StatsObjectID statsObject {};
            statsObject.entityHash = entityId;
            statsObject.idType = RED4ext::game::StatIDType::EntityID;
            uint32_t poolType = m_healthPool;
            bool inPercent = true;
            float percent = -1;
            RED4ext::StackArgs_t valueArgs;
            valueArgs.emplace_back(nullptr, &statsObject);
            valueArgs.emplace_back(nullptr, &poolType);
            valueArgs.emplace_back(nullptr, &inPercent);
            if (game::CallMethod(pools.instance, m_getStatPoolValue, &percent, valueArgs) && std::isfinite(percent) && percent >= 0)
            {
                m_health = std::min(percent / 100.0f, 1.0f);
            }
        }
    }
    state.Health = m_health;
    float dilation = 1.0f;
    if (m_getTimeDilation && game::CallMethod(playerInstance, m_getTimeDilation, &dilation, noArgs) && std::isfinite(dilation) && dilation > 0)
    {
        state.TimeDilation = dilation;
    }
    if (m_getCameraForward)
    {
        RED4ext::Handle<RED4ext::IScriptable> cameras;
        RED4ext::StackArgs_t camerasArgs;
        camerasArgs.emplace_back(nullptr, &gameInstance);
        RED4ext::Vector4 forward {};
        if (game::CallStatic(m_getCameraSystem, &cameras, camerasArgs) && cameras &&
            game::CallMethod(cameras.instance, m_getCameraForward, &forward, noArgs))
        {
            if (!m_cameraLogged)
            {
                m_cameraLogged = true;
                char line[160];
                std::snprintf(line, sizeof(line), "player sampler: first camera forward %.3f %.3f %.3f, body yaw %.1f", forward.X, forward.Y, forward.Z, state.Yaw);
                m_log(line);
            }
            const float flat = std::sqrt(forward.X * forward.X + forward.Y * forward.Y);
            if (flat > 0.0001f || std::fabs(forward.Z) > 0.0001f)
            {
                state.CameraPitch = std::atan2(forward.Z, flat) * DegreesPerRadian;
            }
            if (flat > 0.0001f)
            {
                float offset = std::atan2(-forward.X, forward.Y) * DegreesPerRadian - state.Yaw;
                while (offset > 180.0f)
                {
                    offset -= 360.0f;
                }
                while (offset < -180.0f)
                {
                    offset += 360.0f;
                }
                state.AimYawOffset = offset;
            }
        }
    }
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
        const float seconds = static_cast<float>(nowMs - m_lastSampleMs) / 1000.0f;
        const float velocityX = (position.X - m_lastX) / seconds;
        const float velocityY = (position.Y - m_lastY) / seconds;
        const float velocityZ = (position.Z - m_lastZ) / seconds;
        speed = std::sqrt(velocityX * velocityX + velocityY * velocityY + velocityZ * velocityZ);
        if (speed > TeleportAboveMetersPerSecond)
        {
            speed = 0;
        }
        else
        {
            state.VelocityX = velocityX;
            state.VelocityY = velocityY;
            state.VelocityZ = velocityZ;
        }
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
    SampleExtras(player.instance, state, nowMs);
    int32_t vitals = 0;
    int32_t vehicle = 0;
    int32_t timeDilation = 0;
    if (state_machine_board::ReadInt(player, "Vitals", vitals) && vitals == VitalsDead)
    {
        state.Flags |= DownFlag;
    }
    if (state_machine_board::ReadInt(player, "Vehicle", vehicle) && vehicle >= 1)
    {
        state.Flags |= InVehicleFlag;
    }
    if (state_machine_board::ReadInt(player, "TimeDilation", timeDilation) && timeDilation == TimeDilationSandevistan)
    {
        state.Flags |= SandevistanFlag;
    }
    weapons::OwnWeapons ownWeapons;
    if (weapons::ReadOwnWeapons(player, ownWeapons, m_log))
    {
        weapons::PutInState(ownWeapons, state);
    }
    return true;
}
}
