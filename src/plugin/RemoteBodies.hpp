/*
 remote bodies, the mac take on nightcoops puppets module
 one puppet per partner that sends PlayerStates. same rules as nightcoop:
   spawn a body once the partner is within 150 m of us, take it away when they get over 200 m
   move it by the games teleport (gameTeleportationFacility.Teleport) when they moved over 2 cm or turned over 0.5 deg
   despawn when they leave (no state for 5 s), forget it when our world goes away (it goes with the world)
 nightcoop spawns thru native calls on gameIDynamicEntityIDSystem / gameIEntityStubSystem / gameIPopulationSystem.
 those slots are not verified on mac so the body comes from codewares DynamicEntitySystem instead
 (CreateEntity / GetEntity / DeleteEntity), which needs codeware for mac installed. the body is an npc record
 or a template like nightcoops npc body mode, not the full player body with the anim mirror (not ported yet)
*/
#pragma once

#include "net/SessionMessages.hpp"

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nightcoop_mac
{
struct BodySettings
{
    bool Enabled = true;
    std::string Record = "Character.Panam";
    std::string Template;
};

struct RemotePeerView
{
    uint32_t PeerNumber = 0;
    net::PlayerState State;
};

class RemoteBodies
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    RemoteBodies(LogFunction log, BodySettings settings);

    void Tick(uint64_t nowMs, const std::vector<RemotePeerView>& peers, const net::PlayerState* localPlayer);
    void RemoveAll();
    void ForgetAll(const char* why);

private:
    struct Puppet
    {
        uint64_t EntityHash = 0;
        uint64_t SpawnRequestedMs = 0;
        bool InWorld = false;
        bool Placed = false;
        float PlacedX = 0;
        float PlacedY = 0;
        float PlacedZ = 0;
        float PlacedYaw = 0;
        uint32_t Teleports = 0;
    };

    bool ResolveApi();
    bool FetchSystems();
    bool Spawn(uint32_t peerNumber, const net::PlayerState& state, uint64_t nowMs);
    void Despawn(uint32_t peerNumber, Puppet& puppet, const char* why);
    void MoveTo(uint32_t peerNumber, Puppet& puppet, const net::PlayerState& state);

    LogFunction m_log;
    BodySettings m_settings;
    bool m_resolved = false;
    bool m_givenUp = false;

    RED4ext::CBaseFunction* m_getTeleportationFacility = nullptr;
    RED4ext::CBaseFunction* m_teleport = nullptr;
    RED4ext::CBaseFunction* m_getDynamicEntitySystem = nullptr;
    RED4ext::CBaseFunction* m_isReady = nullptr;
    RED4ext::CBaseFunction* m_createEntity = nullptr;
    RED4ext::CBaseFunction* m_getEntity = nullptr;
    RED4ext::CBaseFunction* m_deleteEntity = nullptr;
    RED4ext::CClass* m_specClass = nullptr;
    RED4ext::CClass* m_gameObjectClass = nullptr;

    RED4ext::Handle<RED4ext::IScriptable> m_teleportationFacility;
    RED4ext::Handle<RED4ext::IScriptable> m_dynamicEntitySystem;

    std::map<uint32_t, Puppet> m_puppets;
    std::map<uint32_t, uint64_t> m_retryAfterMs;
};
}
