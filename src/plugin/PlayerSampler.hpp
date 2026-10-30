/*
 reads the local player for PlayerState on the main thread thru the games rtti
 GetPlayer -> GetWorldPosition / GetWorldYaw, speed comes from how far it moved since the last sample
 GetPlayer gets called as the static function it is with no context. the sdk helper that looks up
 cpPlayerSystem wraps the raw system pointer in a new Handle and on mac that ends up destroying the system
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

    LogFunction m_log;
    bool m_resolved = false;
    bool m_givenUp = false;
    RED4ext::CBaseFunction* m_getPlayer = nullptr;
    RED4ext::CBaseFunction* m_getWorldPosition = nullptr;
    RED4ext::CBaseFunction* m_getWorldYaw = nullptr;
    bool m_hasLastSample = false;
    float m_lastX = 0;
    float m_lastY = 0;
    float m_lastZ = 0;
    uint64_t m_lastSampleMs = 0;
};
}
