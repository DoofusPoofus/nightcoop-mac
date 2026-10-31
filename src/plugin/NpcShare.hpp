/*
 npc share, the owning half of nightcoops npc sharing (NpcSpawn 20 / NpcSnapshot 21 / NpcDespawn 22)
 like nightcoop npcs are only shared with partners in our world: the host shares with guests whose presence says the world
 handoff put them in its world, a guest in the hosts world keeps no npcs of its own (share_npcs in session.txt shares with
 evryone anyway, testing only)
 evry 750 ms the games entity list is scanned for living NPCPuppets that are not ours (codeware managed bodies, cars and
 copies are skipped) within 150 m of such a partner, the nearest first, 64 at most. a shared npc gets a net id (our peer
 << 24 | counter) and goes out as NpcSpawn (record, appearance, entity id, place, yaw) evry 625 ms and NpcSnapshot 10 times a
 second (place, yaw, speed, health %), and NpcDespawn when it is past 180 m from them (reason 1), gone (2) or dead (3)
 not done yet: match keys, ai net states, attitudes, weapons, the npc anim stream (NpcAnim 23), handoffs, claims and orphans
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
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

class NpcShare
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    struct Partner
    {
        uint32_t PeerNumber = 0;
        float Position[3] = {0, 0, 0};
    };

    explicit NpcShare(LogFunction log);

    void Tick(uint64_t nowMs, bool inSave, const std::vector<Partner>& partners, net::RelaySession& session);

private:
    struct Source
    {
        uint32_t NetId = 0;
        uint64_t Record = 0;
        uint64_t Appearance = 0;
        uint64_t NextSpawnMs = 0;
        uint64_t NextSnapshotMs = 0;
        float LastPosition[3] = {0, 0, 0};
        uint64_t LastSampleMs = 0;
        bool Seen = false;
    };

    bool Resolve();
    void Scan(uint64_t nowMs, const std::vector<Partner>& partners, net::RelaySession& session);
    void Send(uint64_t nowMs, uint64_t entityHash, Source& source, RED4ext::Handle<RED4ext::IScriptable>& npc, net::RelaySession& session);
    void StopSharing(uint64_t entityHash, Source& source, uint8_t reason, net::RelaySession& session);

    LogFunction m_log;
    bool m_resolved = false;
    bool m_usable = false;
    RED4ext::CBaseFunction* m_getEntityList = nullptr;
    RED4ext::CBaseFunction* m_getEntityId = nullptr;
    RED4ext::CBaseFunction* m_getWorldPosition = nullptr;
    RED4ext::CBaseFunction* m_getWorldYaw = nullptr;
    RED4ext::CBaseFunction* m_getRecordId = nullptr;
    RED4ext::CBaseFunction* m_getAppearance = nullptr;
    RED4ext::CBaseFunction* m_isDead = nullptr;
    RED4ext::CBaseFunction* m_getDynamicEntitySystem = nullptr;
    RED4ext::CBaseFunction* m_isManaged = nullptr;
    RED4ext::CBaseFunction* m_getStatPoolsSystem = nullptr;
    RED4ext::CBaseFunction* m_getStatPoolValue = nullptr;
    RED4ext::CClass* m_npcClass = nullptr;
    uint32_t m_healthPool = 0;
    std::map<uint64_t, Source> m_sources;
    uint32_t m_netIdCounter = 0;
    uint64_t m_nextScanMs = 0;
    uint64_t m_nextReportMs = 0;
    uint32_t m_sharesStarted = 0;
    uint32_t m_lastScanEntities = 0;
    uint32_t m_lastScanNpcs = 0;
    uint32_t m_lastScanManaged = 0;
    float m_lastScanNearest = 1e9f;
};
}
