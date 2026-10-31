/*
 npc copies the watching half of nightcoops npccopies (NpcSpawn 20 / NpcSnapshot 21 / NpcDespawn 22)
 a partners npc shows up here as a stand in: codeware DynamicEntitySystem spawns it from the owners character record and
 appearance (tag NightCoopNpcCopy) and once it is in the world it is taken under direct control like nightcoop does: its
 AIHumanComponent senseComponent ReactionManagerComponent StimBroadcasterComponent AITargetTrackerComponent
 moveComponent and moveMotionPlannerComponent get toggled off and its ragdoll disabled so only the owners snapshots move it.
 snapshots are shown 100 ms late lerped between the two around that moment by when they came and placed by the games
 teleport (at most 30 times a second only when it moved 2 cm or turned half a degree). a despawn removes the copy a death
 (reason 3) kills it first and leaves the body for 10 s. a copy nobody heard of for 10 s goes away
 the owners NpcAnim (23) batches play on the copys anim controller thru the same setters as the partner body mirror. a copy
 waits for a full graph state first: it asks with NpcAnimWant (35) once a second and drops the plain batches till one came
 from evry snapshot the copy gets the owners health % (kept at 1 % or more while it lives death comes with the despawn)
 the 8 ai net states that changed (dead kept off a living copy cover shown as crouch like nightcoop) the attitude the owners
 npc has to our partner body as its attitude to our player and the owners hand weapons (given and put in the slot checked
 evry 2 s 5 tries)
 copies are marked in the NightCoopCombat script service (immortal our hits on them go to the owner as claims) and a copy
 that dies at its owner gets its kill reward turned off unless the despawn says we killed it (then we are the instigator)
 before a stand in gets spawned our own world is checked for the same person like nightcoop does: the same static entity id
 (above 0xFFFFFF) with the same record else the only npc of that record with the same community match keys within 50 m else
 the only one of that record within 2 m of the owners spot. that npc of ours then becomes the owners puppet (same controls as a
 stand in our npc share leaves it alone) and is given back to its own ai when the owner lets it go
 not done yet: look at time dilation handoffs and claims
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

    void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, uint32_t ourPeer);
    void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session);
    uint32_t CopyCount() const { return static_cast<uint32_t>(m_copies.size()); }
    bool FindByEntity(uint64_t entityHash, uint32_t& netId, uint32_t& ownerPeer, float& ownerHealth) const;
    uint64_t EntityOfNetId(uint32_t netId) const;
    bool IsPuppet(uint64_t entityHash) const;
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
        bool HasSnapshot = false;
        net::NpcSnapshot Latest;
        float AppliedHealth = -1.0f;
        uint8_t AppliedStates[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        int AppliedAttitude = -1;
        uint64_t HandRecords[2] = {0, 0};
        uint32_t HandTries[2] = {0, 0};
        uint64_t NextHandCheckMs = 0;
        uint64_t NextStatesMs = 0;
        bool Marked = false;
        bool OwnPuppet = false;
        bool MatchTried = false;
        bool Held = false;
    };

    bool Resolve();
    RED4ext::Handle<RED4ext::IScriptable> EntityOf(uint64_t entityHash);
    void Spawn(uint32_t netId, Copy& copy, uint64_t nowMs);
    bool TryMatch(uint32_t netId, Copy& copy);
    void GiveBack(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc);
    void TakeControl(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc);
    void Place(Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs);
    void PlayAnim(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs, net::RelaySession& session);
    void ApplyStates(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs, uint32_t ourPeer);
    void ApplyHands(uint32_t netId, Copy& copy, RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t nowMs);
    void Report(uint64_t nowMs);
    void Kill(RED4ext::Handle<RED4ext::IScriptable>& npc, bool weKilledIt);
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
    RED4ext::CBaseFunction* m_getPlayer = nullptr;
    RED4ext::CBaseFunction* m_findEntity = nullptr;
    RED4ext::CBaseFunction* m_getEntityList = nullptr;
    RED4ext::CBaseFunction* m_getWorldPosition = nullptr;
    RED4ext::CBaseFunction* m_isDead = nullptr;
    RED4ext::Handle<RED4ext::IScriptable> m_playerAgent;
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
    uint32_t m_healthWrites = 0;
    uint32_t m_stateWrites = 0;
    uint32_t m_attitudeWrites = 0;
    uint32_t m_handWrites = 0;
    bool m_attitudeTroubleLogged = false;
};
}
