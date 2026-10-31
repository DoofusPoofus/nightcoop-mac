/*
 npc share the owning half of nightcoops npc sharing (NpcSpawn 20 / NpcSnapshot 21 / NpcDespawn 22)
 like nightcoop npcs are only shared with partners in our world: the host shares with guests whose presence says the world
 handoff put them in its world a guest in the hosts world keeps no npcs of its own (share_npcs in session.txt shares with
 evryone anyway testing only)
 evry 750 ms the games entity list is scanned for living NPCPuppets that are not ours (codeware managed bodies cars and
 copies are skipped so are our own npcs that play a partners npc as its puppet) within 150 m of such a partner the nearest first 64 at most. a shared npc gets a net id (our peer
 << 24 | counter) and goes out as NpcSpawn (record appearance entity id community match keys place yaw) evry 625 ms and NpcSnapshot 10 times a
 second (place yaw speed health %) and NpcDespawn when it is past 180 m from them (reason 1) gone (2) or dead (3)
 each shared npcs anim controller and event queue go under the anim tap which streams its graph writes as NpcAnim (23)
 snapshots also carry its 8 ai net states its attitude to our player and to evry partner body its hand weapons (checked
 evry 500 ms) time dilation and the incapacitated flag (npc states does the game calls). a shared npc in combat that is
 hostile to a partner body within 35 m gets a threat on that body evry 2 s (npc combat) so it fights partners too
 not done yet: match keys workspot flags handoffs claims and orphans
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
struct NpcSnapshot;
}

class NpcShare
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    struct Partner
    {
        uint32_t PeerNumber = 0;
        float Position[3] = {0, 0, 0};
        RED4ext::Handle<RED4ext::IScriptable> Body;
    };

    explicit NpcShare(LogFunction log);

    void Tick(uint64_t nowMs, bool inSave, const std::vector<Partner>& partners, net::RelaySession& session);
    void SetPuppetCheck(std::function<bool(uint64_t)> isPartnerPuppet) { m_isPartnerPuppet = std::move(isPartnerPuppet); }
    uint64_t EntityOfNetId(uint32_t netId) const;
    uint32_t NetIdOfEntity(uint64_t entityHash) const;
    bool RecentlyDied(uint32_t netId, uint64_t nowMs) const;

private:
    struct Source
    {
        uint32_t NetId = 0;
        uint64_t Record = 0;
        uint64_t Appearance = 0;
        uint64_t SpawnerKey = 0;
        uint64_t CommunityEntryKey = 0;
        uint64_t NextSpawnMs = 0;
        uint64_t NextSnapshotMs = 0;
        float LastPosition[3] = {0, 0, 0};
        uint64_t LastSampleMs = 0;
        bool Seen = false;
        RED4ext::Handle<RED4ext::IScriptable> Controller;
        RED4ext::Handle<RED4ext::IScriptable> Body;
        uint64_t RightHandRecord = 0;
        uint64_t LeftHandRecord = 0;
        uint64_t NextHandsMs = 0;
        std::map<uint32_t, uint64_t> NextThreatMsByPeer;
    };

    bool Resolve();
    void Scan(uint64_t nowMs, const std::vector<Partner>& partners, net::RelaySession& session);
    void Send(uint64_t nowMs, uint64_t entityHash, Source& source, RED4ext::Handle<RED4ext::IScriptable>& npc, const std::vector<Partner>& partners,
              net::RelaySession& session);
    void FillStates(uint64_t nowMs, Source& source, RED4ext::Handle<RED4ext::IScriptable>& npc, const std::vector<Partner>& partners,
                    net::NpcSnapshot& snapshot);
    void StopSharing(uint64_t entityHash, Source& source, uint8_t reason, net::RelaySession& session, uint32_t killer = 0xFFFFFFFF);
    uint32_t KillerOf(RED4ext::Handle<RED4ext::IScriptable>& npc, const std::vector<Partner>& partners, uint32_t ourPeer);
    void WatchGraph(Source& source, RED4ext::Handle<RED4ext::IScriptable>& npc);
    void LetGoOfAll();

    LogFunction m_log;
    std::function<bool(uint64_t)> m_isPartnerPuppet;
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
    RED4ext::CBaseFunction* m_getAnimationController = nullptr;
    RED4ext::CBaseFunction* m_getPlayer = nullptr;
    RED4ext::CBaseFunction* m_getKiller = nullptr;
    RED4ext::Handle<RED4ext::IScriptable> m_player;
    std::map<uint32_t, uint64_t> m_diedAtMs;
    RED4ext::Handle<RED4ext::IScriptable> m_playerAgent;
    std::vector<RED4ext::Handle<RED4ext::IScriptable>> m_partnerAgents;
    RED4ext::CClass* m_npcClass = nullptr;
    std::map<uint64_t, Source> m_sources;
    uint32_t m_netIdCounter = 0;
    uint64_t m_nextScanMs = 0;
    uint64_t m_nextReportMs = 0;
    uint32_t m_sharesStarted = 0;
    uint32_t m_graphsWatched = 0;
    uint32_t m_lastScanEntities = 0;
    uint32_t m_lastScanNpcs = 0;
    uint32_t m_lastScanManaged = 0;
    uint32_t m_lastScanCandidates = 0;
    uint32_t m_lastScanDead = 0;
    float m_lastScanNearest = 1e9f;
};
}
