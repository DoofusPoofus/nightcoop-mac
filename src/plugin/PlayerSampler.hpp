/*
 reads the local player for PlayerState on the main thread thru the games rtti
 GetPlayer -> GetWorldPosition / GetWorldYaw, speed and velocity come from how far it moved since the last sample (both go
 to 0 over 200 m/s, a teleport, like nightcoops bridge)
 health is the Health stat pool in percent / 100 (evry 250 ms), time dilation is the players GetTimeDilationValue, the
 camera pitch (degrees) and the aim yaw offset (camera yaw - body yaw, degrees) come from the active cameras forward
 GetPlayer gets called as the static function it is with no context. the sdk helper that looks up
 cpPlayerSystem wraps the raw system pointer in a new Handle and on mac that ends up destroying the system
 crouch comes from PlayerPuppet.inCrouch and sprint from speed (over 6.5 m/s), they go out in Flags and the
 locomotion state byte the same way nightcoops player bridge fills them from the state machine. weapons (records, shots,
 weapon states, aim / shoot / reload) come from the weapons module. like nightcoop the state machine board adds flag 0x100 when
 Vitals is Dead, 0x10 when Vehicle is 1 or more and 0x200 when TimeDilation is the sandevistan
 if a function cant be found it says so once and stays off
*/
#pragma once

#include "net/SessionMessages.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace RED4ext
{
struct CBaseFunction;
struct CProperty;
}

namespace nightcoop_mac
{
class PlayerSampler
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    explicit PlayerSampler(LogFunction log)
        : m_log(std::move(log))
    {
    }

    bool Sample(net::PlayerState& state, uint64_t nowMs);

private:
    bool ResolveFunctions(void* playerInstance);
    void ResolveExtras();
    void SampleExtras(void* playerInstance, net::PlayerState& state, uint64_t nowMs);

    LogFunction m_log;
    bool m_resolved = false;
    bool m_givenUp = false;
    RED4ext::CBaseFunction* m_getPlayer = nullptr;
    RED4ext::CBaseFunction* m_getWorldPosition = nullptr;
    RED4ext::CBaseFunction* m_getWorldYaw = nullptr;
    RED4ext::CProperty* m_inCrouch = nullptr;
    bool m_inCrouchChecked = false;
    bool m_extrasResolved = false;
    RED4ext::CBaseFunction* m_getEntityId = nullptr;
    RED4ext::CBaseFunction* m_getStatPoolsSystem = nullptr;
    RED4ext::CBaseFunction* m_getStatPoolValue = nullptr;
    RED4ext::CBaseFunction* m_getTimeDilation = nullptr;
    RED4ext::CBaseFunction* m_getCameraSystem = nullptr;
    RED4ext::CBaseFunction* m_getCameraForward = nullptr;
    uint32_t m_healthPool = 0;
    bool m_healthKnown = false;
    float m_health = 1.0f;
    uint64_t m_nextHealthMs = 0;
    bool m_cameraLogged = false;
    bool m_hasLastSample = false;
    float m_lastX = 0;
    float m_lastY = 0;
    float m_lastZ = 0;
    uint64_t m_lastSampleMs = 0;
};
}
