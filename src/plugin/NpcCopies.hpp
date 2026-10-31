/*
 npc copies, the watching half of nightcoops npccopies (NpcSpawn 20 / NpcSnapshot 21 / NpcDespawn 22)
 a partners npc shows up here as a stand in: codeware DynamicEntitySystem spawns it from the owners character record and
 appearance (tag NightCoopNpcCopy), and once it is in the world it is taken under direct control like nightcoop does: its
 AIHumanComponent, senseComponent, ReactionManagerComponent, StimBroadcasterComponent, AITargetTrackerComponent,
 moveComponent and moveMotionPlannerComponent get toggled off and its ragdoll disabled, so only the owners snapshots move it.
 snapshots are shown 100 ms late, lerped between the two around that moment by when they came, and placed by the games
 teleport (at most 30 times a second, only when it moved 2 cm or turned half a degree). a despawn removes the copy, a death
 (reason 3) kills it first and leaves the body for 10 s. a copy nobody heard of for 10 s goes away
 the owners NpcAnim (23) batches play on the copys anim controller thru the same setters as the partner body mirror. a copy
 waits for a full graph state first: it asks with NpcAnimWant (35) once a second and drops the plain batches till one came
 not done yet: matching our own npc of the same person instead of spawning (static ids, community keys), states, health,
 attitude, weapons, look at, handoffs and claims
*/
#pragma once

#include "net/GameMessages.hpp"
#include "net/NpcMessages.hpp"
#include "plugin/AnimPlayer.hpp"

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nightcoop_mac
{
namespace net
{
class RelaySession;
}

class NpcCopies
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    explicit NpcCopies(LogFunction log)
        : m_log(std::move(log))
    {
    }

    void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs);
    void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session);
    uint32_t CopyCount() const { return static_cast<uint32_t>(m_copies.size()); }
    uint64_t CopyEntity(uint32_t netId, uint32_t& ownerPeer) const;
    uint32_t NetIdOfCopy(uint64_t entityHash) const;
    void SetHeld(uint32_t netId, bool held);

private:
    struct Sample
    {
        uint64_t ArrivedMs = 0;
        float Position[3] = {0, 0, 0};
        float Yaw = 0;
    };

    struct Copy
    {
        uint32_t OwnerPeer = 0;
        net::NpcSpawn Spawn;
        uint64_t EntityHash = 0;
        bool InWorld = false;
        uint64_t SpawnRequestedMs = 0;
        uint64_t LastHeardMs = 0;
        uint64_t NextPlaceMs = 0;
        uint64_t RemoveAtMs = 0;
        uint32_t Placements = 0;
        uint32_t ComponentsOff = 0;
        float PlacedPosition[3] = {0, 0, 0};
        float PlacedYaw = 0;
        bool Placed = false;
        std::deque<Sample> Samples;
        std::vector<net::AnimBatch> PendingAnim;
        bool GotFullAnimState = false;
        uint64_t LastAnimWantMs = 0;
        bool Held = false;
    };

    bool Resolve();
    RED4ext::Handle<RED4ext::IScriptable> EntityOf(uint64_t entityHash);
    void Spawn(uint32_t netId, Copy& copy, uint64_t nowMs);
    void TakeControl(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc);
    void Place(Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs);
    void PlayAnim(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs, net::RelaySession& session);
    void Report(uint64_t nowMs);
    void Kill(RED4ext::Handle<RED4ext::IScriptable>& npc);
    void Remove(uint32_t netId, Copy& copy, const char* why);

    LogFunction m_log;
    bool m_resolved = false;
    bool m_usable = false;
    RED4ext::CBaseFunction* m_getDynamicEntitySystem = nullptr;
    RED4ext::CBaseFunction* m_createEntity = nullptr;
    RED4ext::CBaseFunction* m_getEntity = nullptr;
    RED4ext::CBaseFunction* m_deleteEntity = nullptr;
    RED4ext::CBaseFunction* m_getTeleportationFacility = nullptr;
    RED4ext::CBaseFunction* m_teleport = nullptr;
    RED4ext::CBaseFunction* m_getComponents = nullptr;
    RED4ext::CBaseFunction* m_toggle = nullptr;
    RED4ext::CBaseFunction* m_setDisableRagdoll = nullptr;
    RED4ext::CBaseFunction* m_kill = nullptr;
    RED4ext::CClass* m_specClass = nullptr;
    RED4ext::CClass* m_npcClass = nullptr;
    std::vector<RED4ext::CClass*> m_offClasses;
    RED4ext::Handle<RED4ext::IScriptable> m_dynamicEntitySystem;
    RED4ext::Handle<RED4ext::IScriptable> m_teleportationFacility;
    std::map<uint32_t, Copy> m_copies;
    AnimPlayer m_animPlayer;
    uint64_t m_nextReportMs = 0;
    uint32_t m_animBatches = 0;
    uint32_t m_animFullStates = 0;
    uint32_t m_animWaited = 0;
    uint32_t m_animPlayed = 0;
    uint32_t m_animFailed = 0;
    uint32_t m_animOther = 0;
    uint32_t m_animWants = 0;
};
}
