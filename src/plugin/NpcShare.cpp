#include "plugin/NpcShare.hpp"

#include "net/NpcMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/AnimTap.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/NpcCombat.hpp"
#include "plugin/NpcStates.hpp"

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace nightcoop_mac
{
namespace
{
constexpr uint64_t ScanEveryMs = 750;
constexpr uint64_t SpawnEveryMs = 625;
constexpr uint64_t SnapshotEveryMs = 100;
constexpr uint64_t HandsEveryMs = 500;
constexpr size_t MostPartnerAttitudes = 7;
constexpr uint8_t FlagIncapacitated = 0x80;
constexpr uint8_t HighLevelCombat = 2;
constexpr uint8_t AttitudeHostile = 2;
constexpr float ThreatWithinMeters = 35.0f;
constexpr uint64_t ThreatEveryMs = 2000;
constexpr uint64_t OfferCheckEveryMs = 1000;
constexpr uint64_t OwnedBeforeOfferMs = 5000;
constexpr uint64_t OfferWaitsMs = 5000;
constexpr uint64_t OfferAgainMs = 10000;
constexpr uint64_t OfferAfterRefusalMs = 30000;
constexpr float GiveAwayPastMeters = 70.0f;
constexpr float TakeWithinMeters = 100.0f;
constexpr float HandoffMarginMeters = 15.0f;
constexpr uint64_t ReportEveryMs = 10000;
constexpr float ShareWithinMeters = 150.0f;
constexpr float StopPastMeters = 180.0f;
constexpr size_t MostShared = 64;
constexpr uint8_t ReasonStopped = 1;
constexpr uint8_t ReasonGone = 2;
constexpr uint8_t ReasonDied = 3;

float NearestPartner(const float* position, const std::vector<NpcShare::Partner>& partners)
{
    float nearest = 1e9f;
    for (const auto& partner : partners)
    {
        const float deltaX = position[0] - partner.Position[0];
        const float deltaY = position[1] - partner.Position[1];
        const float deltaZ = position[2] - partner.Position[2];
        nearest = std::min(nearest, std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ));
    }
    return nearest;
}
}

NpcShare::NpcShare(LogFunction log)
    : m_log(std::move(log))
{
    m_netIdCounter = std::random_device {}() & 0xFFFFFF;
}

bool NpcShare::Resolve()
{
    if (m_resolved)
    {
        return m_usable;
    }
    m_resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    m_getEntityList = game::FindFunction("ScriptGameInstance", "GetEntityList");
    m_getEntityId = game::FindFunction("entEntity", "GetEntityID");
    m_getWorldPosition = game::FindFunction("entEntity", "GetWorldPosition");
    m_getWorldYaw = game::FindFunction("gameObject", "GetWorldYaw");
    m_getRecordId = game::FindFunction("gamePuppetBase", "GetRecordID");
    m_getAppearance = game::FindFunction("entEntity", "GetCurrentAppearanceName");
    m_isDead = game::FindFunction("NPCPuppet", "IsDead");
    m_getDynamicEntitySystem = game::FindFunction("ScriptGameInstance", "GetDynamicEntitySystem");
    m_isManaged = game::FindFunction("DynamicEntitySystem", "IsManaged");
    m_getAnimationController = game::FindFunction("ScriptedPuppet", "GetAnimationControllerComponent");
    m_getPlayer = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
    m_getKiller = game::FindFunction("ScriptedPuppet", "GetKiller");
    if (!game::ParamsAre(m_getKiller, {}))
    {
        m_getKiller = nullptr;
    }
    m_npcClass = rtti->GetClass("NPCPuppet");
    m_usable = game::ParamsAre(m_getEntityList, {"ScriptGameInstance"}) && game::ParamsAre(m_getEntityId, {}) &&
               game::ParamsAre(m_getWorldPosition, {}) && game::ParamsAre(m_getWorldYaw, {}) && game::ParamsAre(m_getRecordId, {}) &&
               game::ParamsAre(m_getAppearance, {}) && game::ParamsAre(m_isDead, {}) && game::ParamsAre(m_getDynamicEntitySystem, {}) &&
               game::ParamsAre(m_isManaged, {"entEntityID"}) && npc_states::Ready() && m_npcClass;
    m_log(m_usable ? "npcs: our npcs near partners in our world go to them (npc state calls that look different: " + npc_states::MissingCalls() + ")" : "npcs: the entity list or npc calls look different than expected, our npcs stay ours");
    return m_usable;
}

void NpcShare::StopSharing(uint64_t entityHash, Source& source, uint8_t reason, net::RelaySession& session, uint32_t killer)
{
    anim_tap::StopWatchingNpc(source.NetId);
    session.SendGameMessage(net::WriteNpcDespawn({source.NetId, reason, reason == ReasonDied ? killer : 0xFFFFFFFF}));
}

uint32_t NpcShare::KillerOf(RED4ext::Handle<RED4ext::IScriptable>& npc, const std::vector<Partner>& partners, uint32_t ourPeer)
{
    if (!m_getKiller)
    {
        return 0xFFFFFFFF;
    }
    RED4ext::WeakHandle<RED4ext::IScriptable> killer;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(npc.instance, m_getKiller, &killer, noArgs);
    auto killerEntity = killer.Lock();
    if (!killerEntity)
    {
        return 0xFFFFFFFF;
    }
    if (m_player && killerEntity.instance == m_player.instance)
    {
        return ourPeer;
    }
    for (const auto& partner : partners)
    {
        if (partner.Body && partner.Body.instance == killerEntity.instance)
        {
            return partner.PeerNumber;
        }
    }
    return 0xFFFFFFFF;
}

uint64_t NpcShare::EntityOfNetId(uint32_t netId) const
{
    for (const auto& [entityHash, source] : m_sources)
    {
        if (source.NetId == netId)
        {
            return entityHash;
        }
    }
    return 0;
}

void NpcShare::OfferHandoffs(uint64_t nowMs, const std::vector<Partner>& partners, net::RelaySession& session)
{
    if (!m_player)
    {
        return;
    }
    RED4ext::Vector4 ourPlace {};
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(m_player.instance, m_getWorldPosition, &ourPlace, noArgs);
    for (auto& [entityHash, source] : m_sources)
    {
        if (source.OfferedTo != 0xFFFFFFFF && nowMs - source.OfferedAtMs >= OfferWaitsMs)
        {
            source.OfferedTo = 0xFFFFFFFF;
        }
        if (source.OfferedTo != 0xFFFFFFFF || nowMs < source.NextOfferMs || nowMs - source.SharedSinceMs < OwnedBeforeOfferMs || !source.LastSampleMs)
        {
            continue;
        }
        const float ourX = source.LastPosition[0] - ourPlace.X;
        const float ourY = source.LastPosition[1] - ourPlace.Y;
        const float ourZ = source.LastPosition[2] - ourPlace.Z;
        const float ourDistance = std::sqrt(ourX * ourX + ourY * ourY + ourZ * ourZ);
        if (ourDistance <= GiveAwayPastMeters)
        {
            continue;
        }
        const Partner* taker = nullptr;
        float takerDistance = TakeWithinMeters;
        for (const auto& partner : partners)
        {
            const float deltaX = source.LastPosition[0] - partner.Position[0];
            const float deltaY = source.LastPosition[1] - partner.Position[1];
            const float deltaZ = source.LastPosition[2] - partner.Position[2];
            const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            if (distance <= takerDistance && distance + HandoffMarginMeters <= ourDistance)
            {
                taker = &partner;
                takerDistance = distance;
            }
        }
        if (!taker)
        {
            continue;
        }
        net::NpcHandoff handoff;
        handoff.Spawn.NetId = source.NetId;
        handoff.Spawn.Record = source.Record;
        handoff.Spawn.Appearance = source.Appearance;
        handoff.Spawn.OwnerEntity = entityHash;
        handoff.Spawn.MatchKeyA = source.SpawnerKey;
        handoff.Spawn.MatchKeyB = source.CommunityEntryKey;
        std::memcpy(handoff.Spawn.Position, source.LastPosition, sizeof(handoff.Spawn.Position));
        handoff.Spawn.Yaw = source.LastYaw;
        handoff.Snapshot.NetId = source.NetId;
        std::memcpy(handoff.Snapshot.Position, source.LastPosition, sizeof(handoff.Snapshot.Position));
        handoff.Snapshot.Yaw = source.LastYaw;
        handoff.Snapshot.Health = std::clamp(npc_states::HealthPercent(entityHash), 0.0f, 100.0f);
        handoff.Snapshot.OwnerClockMs = session.SessionClockMs();
        handoff.Snapshot.RightHandItem = source.RightHandRecord;
        handoff.Snapshot.LeftHandItem = source.LeftHandRecord;
        session.SendGameMessage(net::WriteNpcHandoff(handoff), static_cast<int32_t>(taker->PeerNumber));
        source.OfferedTo = taker->PeerNumber;
        source.OfferedAtMs = nowMs;
        source.NextOfferMs = nowMs + OfferAgainMs;
        ++m_handoffsOffered;
        char line[200];
        std::snprintf(line, sizeof(line), "npcs: offering net id %#010x to peer %u: it is %.0f m from us and %.0f m from them", source.NetId,
                      taker->PeerNumber, ourDistance, takerDistance);
        m_log(line);
    }
}

void NpcShare::OnHandoffAck(uint32_t fromPeer, const net::NpcHandoffAck& ack, uint64_t nowMs, const HandedAway& handedAway)
{
    for (auto source = m_sources.begin(); source != m_sources.end(); ++source)
    {
        if (source->second.NetId != ack.NetId || source->second.OfferedTo != fromPeer)
        {
            continue;
        }
        char line[200];
        if (!ack.Accepted)
        {
            source->second.OfferedTo = 0xFFFFFFFF;
            source->second.NextOfferMs = nowMs + OfferAfterRefusalMs;
            std::snprintf(line, sizeof(line), "npcs: peer %u refused net id %#010x (reason %u)", fromPeer, ack.NetId, ack.Reason);
            m_log(line);
            return;
        }
        net::NpcSpawn spawn;
        spawn.NetId = source->second.NetId;
        spawn.Record = source->second.Record;
        spawn.Appearance = source->second.Appearance;
        spawn.OwnerEntity = source->first;
        spawn.MatchKeyA = source->second.SpawnerKey;
        spawn.MatchKeyB = source->second.CommunityEntryKey;
        std::memcpy(spawn.Position, source->second.LastPosition, sizeof(spawn.Position));
        spawn.Yaw = source->second.LastYaw;
        anim_tap::StopWatchingNpc(source->second.NetId);
        const uint64_t entityHash = source->first;
        m_sources.erase(source);
        ++m_handoffsGiven;
        std::snprintf(line, sizeof(line), "npcs: peer %u took net id %#010x (entity %#llx on its side), our npc is now its puppet", fromPeer, ack.NetId,
                      static_cast<unsigned long long>(ack.Entity));
        m_log(line);
        if (handedAway)
        {
            handedAway(spawn.NetId, fromPeer, entityHash, spawn);
        }
        return;
    }
}

bool NpcShare::TakeHanded(uint32_t netId, uint64_t entityHash, const net::NpcSpawn& spawn, uint64_t nowMs)
{
    if (!Resolve() || !entityHash || m_sources.count(entityHash))
    {
        return false;
    }
    Source taken;
    taken.NetId = netId;
    taken.Record = spawn.Record & 0xFFFFFFFFFFULL;
    taken.Appearance = spawn.Appearance;
    taken.SpawnerKey = spawn.MatchKeyA;
    taken.CommunityEntryKey = spawn.MatchKeyB;
    taken.SharedSinceMs = nowMs;
    taken.Handed = true;
    std::memcpy(taken.LastPosition, spawn.Position, sizeof(taken.LastPosition));
    taken.LastYaw = spawn.Yaw;
    auto& source = m_sources.emplace(entityHash, taken).first->second;
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> npc;
    static auto* findEntity = game::FindFunction("ScriptGameInstance", "FindEntityByID");
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::StackArgs_t findArgs;
    findArgs.emplace_back(nullptr, &gameInstance);
    findArgs.emplace_back(nullptr, &entityId);
    if (findEntity && game::CallStatic(findEntity, &npc, findArgs) && npc)
    {
        WatchGraph(source, npc);
    }
    ++m_handoffsTaken;
    return true;
}

uint32_t NpcShare::NetIdOfEntity(uint64_t entityHash) const
{
    const auto source = m_sources.find(entityHash);
    return source != m_sources.end() ? source->second.NetId : 0;
}

bool NpcShare::RecentlyDied(uint32_t netId, uint64_t nowMs) const
{
    const auto died = m_diedAtMs.find(netId);
    return died != m_diedAtMs.end() && nowMs - died->second < 30000;
}

void NpcShare::WatchGraph(Source& source, RED4ext::Handle<RED4ext::IScriptable>& npc)
{
    RED4ext::StackArgs_t noArgs;
    if (!m_getAnimationController || !game::ReturnIs(m_getAnimationController, "handle:entAnimationControllerComponent") ||
        !game::CallMethod(npc.instance, m_getAnimationController, &source.Controller, noArgs) || !source.Controller)
    {
        return;
    }
    source.Body = npc;
    if (anim_tap::WatchNpc(source.NetId, source.Controller.instance, npc.instance))
    {
        ++m_graphsWatched;
    }
}

void NpcShare::LetGoOfAll()
{
    for (auto& [entityHash, source] : m_sources)
    {
        anim_tap::StopWatchingNpc(source.NetId);
        new RED4ext::Handle<RED4ext::IScriptable>(std::move(source.Controller));
        new RED4ext::Handle<RED4ext::IScriptable>(std::move(source.Body));
    }
    m_sources.clear();
    m_playerAgent.Reset();
    m_player.Reset();
    m_partnerAgents.clear();
    m_diedAtMs.clear();
}

void NpcShare::FillStates(uint64_t nowMs, Source& source, RED4ext::Handle<RED4ext::IScriptable>& npc, const std::vector<Partner>& partners,
                          net::NpcSnapshot& snapshot)
{
    npc_states::ReadStates(npc, snapshot.States);
    auto npcAgent = npc_states::AttitudeAgentOf(npc);
    snapshot.Attitude = npc_states::AttitudeTowards(npcAgent, m_playerAgent);
    for (size_t index = 0; index < partners.size() && index < m_partnerAgents.size() && snapshot.PartnerAttitudes.size() < MostPartnerAttitudes; ++index)
    {
        if (!m_partnerAgents[index])
        {
            continue;
        }
        const uint8_t attitude = npc_states::AttitudeTowards(npcAgent, m_partnerAgents[index]);
        snapshot.PartnerAttitudes.push_back({static_cast<uint8_t>(partners[index].PeerNumber), attitude});
        const float deltaX = snapshot.Position[0] - partners[index].Position[0];
        const float deltaY = snapshot.Position[1] - partners[index].Position[1];
        const float deltaZ = snapshot.Position[2] - partners[index].Position[2];
        auto& nextThreatMs = source.NextThreatMsByPeer[partners[index].PeerNumber];
        if (snapshot.States[npc_states::HighLevelStateSlot] == HighLevelCombat && attitude == AttitudeHostile && nowMs >= nextThreatMs &&
            deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ <= ThreatWithinMeters * ThreatWithinMeters)
        {
            nextThreatMs = nowMs + ThreatEveryMs;
            auto body = partners[index].Body;
            npc_combat::ShareThreat(npc, body);
        }
    }
    if (nowMs >= source.NextHandsMs)
    {
        source.NextHandsMs = nowMs + HandsEveryMs;
        source.RightHandRecord = npc_states::HandRecord(npc, true);
        source.LeftHandRecord = npc_states::HandRecord(npc, false);
    }
    snapshot.RightHandItem = source.RightHandRecord;
    snapshot.LeftHandItem = source.LeftHandRecord;
    bool ignoresGlobal = false;
    snapshot.TimeDilation = npc_states::TimeDilation(npc, ignoresGlobal);
    snapshot.DilationFlags = static_cast<uint8_t>(ignoresGlobal ? 3 : 1);
    if (npc_states::IsIncapacitated(npc) && snapshot.Health > 0.0f)
    {
        snapshot.Flags |= FlagIncapacitated;
    }
}

void NpcShare::Send(uint64_t nowMs, uint64_t entityHash, Source& source, RED4ext::Handle<RED4ext::IScriptable>& npc, const std::vector<Partner>& partners,
                    net::RelaySession& session)
{
    RED4ext::StackArgs_t noArgs;
    RED4ext::Vector4 position {};
    float yaw = 0;
    game::CallMethod(npc.instance, m_getWorldPosition, &position, noArgs);
    game::CallMethod(npc.instance, m_getWorldYaw, &yaw, noArgs);
    source.LastYaw = yaw;
    if (nowMs >= source.NextSpawnMs)
    {
        source.NextSpawnMs = nowMs + SpawnEveryMs;
        net::NpcSpawn spawn;
        spawn.NetId = source.NetId;
        spawn.Record = source.Record;
        spawn.Appearance = source.Appearance;
        spawn.OwnerEntity = entityHash;
        spawn.MatchKeyA = source.SpawnerKey;
        spawn.MatchKeyB = source.CommunityEntryKey;
        spawn.Position[0] = position.X;
        spawn.Position[1] = position.Y;
        spawn.Position[2] = position.Z;
        spawn.Yaw = yaw;
        session.SendGameMessage(net::WriteNpcSpawn(spawn));
    }
    if (nowMs < source.NextSnapshotMs)
    {
        return;
    }
    source.NextSnapshotMs = nowMs + SnapshotEveryMs;
    float speed = 0;
    if (source.LastSampleMs && nowMs > source.LastSampleMs)
    {
        const float deltaX = position.X - source.LastPosition[0];
        const float deltaY = position.Y - source.LastPosition[1];
        speed = std::sqrt(deltaX * deltaX + deltaY * deltaY) * 1000.0f / static_cast<float>(nowMs - source.LastSampleMs);
    }
    source.LastPosition[0] = position.X;
    source.LastPosition[1] = position.Y;
    source.LastPosition[2] = position.Z;
    source.LastSampleMs = nowMs;
    const float health = npc_states::HealthPercent(entityHash);
    net::NpcSnapshot snapshot;
    snapshot.NetId = source.NetId;
    snapshot.Position[0] = position.X;
    snapshot.Position[1] = position.Y;
    snapshot.Position[2] = position.Z;
    snapshot.Yaw = yaw;
    snapshot.Speed = std::isfinite(speed) && speed < 100.0f ? speed : 0.0f;
    snapshot.Health = std::isfinite(health) ? std::clamp(health, 0.0f, 100.0f) : 100.0f;
    snapshot.OwnerClockMs = session.SessionClockMs();
    FillStates(nowMs, source, npc, partners, snapshot);
    session.SendGameMessage(net::WriteNpcSnapshot(snapshot), net::kEveryPeer, false);
}

void NpcShare::Scan(uint64_t nowMs, bool shareOwnNpcs, const std::vector<Partner>& partners, net::RelaySession& session)
{
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> entities;
    RED4ext::StackArgs_t listArgs;
    listArgs.emplace_back(nullptr, &gameInstance);
    RED4ext::Handle<RED4ext::IScriptable> dynamicEntities;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallStatic(m_getEntityList, &entities, listArgs) || !game::CallStatic(m_getDynamicEntitySystem, &dynamicEntities, noArgs) || !dynamicEntities)
    {
        return;
    }
    std::vector<std::pair<float, RED4ext::Handle<RED4ext::IScriptable>>> candidates;
    m_lastScanEntities = entities.Size();
    m_lastScanNpcs = 0;
    m_lastScanManaged = 0;
    m_lastScanNearest = 1e9f;
    for (auto& entity : entities)
    {
        auto* entityClass = entity ? entity->GetType() : nullptr;
        if (!entityClass || !entityClass->IsA(m_npcClass))
        {
            continue;
        }
        ++m_lastScanNpcs;
        uint64_t entityHash = 0;
        game::CallMethod(entity.instance, m_getEntityId, &entityHash, noArgs);
        RED4ext::ent::EntityID entityId(entityHash);
        bool managed = false;
        RED4ext::StackArgs_t managedArgs;
        managedArgs.emplace_back(nullptr, &entityId);
        game::CallMethod(dynamicEntities.instance, m_isManaged, &managed, managedArgs);
        const auto handedSource = m_sources.find(entityHash);
        const bool handedToUs = handedSource != m_sources.end() && handedSource->second.Handed;
        if ((managed && !handedToUs) || (m_isPartnerPuppet && m_isPartnerPuppet(entityHash)))
        {
            ++m_lastScanManaged;
            continue;
        }
        RED4ext::Vector4 position {};
        game::CallMethod(entity.instance, m_getWorldPosition, &position, noArgs);
        const float place[3] = {position.X, position.Y, position.Z};
        const float distance = NearestPartner(place, partners);
        m_lastScanNearest = std::min(m_lastScanNearest, distance);
        const bool shared = m_sources.count(entityHash) != 0;
        if (distance <= ShareWithinMeters || (shared && distance <= StopPastMeters))
        {
            candidates.push_back({distance, entity});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& first, const auto& second) { return first.first < second.first; });
    if (candidates.size() > MostShared)
    {
        candidates.resize(MostShared);
    }
    m_lastScanCandidates = static_cast<uint32_t>(candidates.size());
    m_lastScanDead = 0;
    for (auto& [hash, source] : m_sources)
    {
        source.Seen = false;
    }
    for (auto& [distance, npc] : candidates)
    {
        uint64_t entityHash = 0;
        bool dead = false;
        game::CallMethod(npc.instance, m_getEntityId, &entityHash, noArgs);
        game::CallMethod(npc.instance, m_isDead, &dead, noArgs);
        auto source = m_sources.find(entityHash);
        if (source == m_sources.end())
        {
            if (dead)
            {
                ++m_lastScanDead;
                continue;
            }
            if (!shareOwnNpcs)
            {
                continue;
            }
            Source fresh;
            fresh.NetId = (session.GetStatus().MyPeerNumber << 24) | (++m_netIdCounter & 0xFFFFFF);
            RED4ext::TweakDBID record;
            RED4ext::CName appearance;
            game::CallMethod(npc.instance, m_getRecordId, &record, noArgs);
            game::CallMethod(npc.instance, m_getAppearance, &appearance, noArgs);
            fresh.Record = record.value & 0xFFFFFFFFFFULL;
            fresh.Appearance = appearance.hash;
            fresh.SharedSinceMs = nowMs;
            npc_states::MatchKeys(entityHash, fresh.SpawnerKey, fresh.CommunityEntryKey);
            source = m_sources.emplace(entityHash, fresh).first;
            ++m_sharesStarted;
            WatchGraph(source->second, npc);
        }
        source->second.Seen = true;
        if (dead)
        {
            const uint32_t killer = KillerOf(npc, partners, session.GetStatus().MyPeerNumber);
            m_diedAtMs[source->second.NetId] = nowMs;
            char line[120];
            std::snprintf(line, sizeof(line), "npcs: our shared npc %#010x died", source->second.NetId);
            m_log(std::string(line) + (killer != 0xFFFFFFFF ? ", killed by peer " + std::to_string(killer) : ""));
            StopSharing(entityHash, source->second, ReasonDied, session, killer);
            m_sources.erase(source);
        }
    }
    for (auto source = m_sources.begin(); source != m_sources.end();)
    {
        if (!source->second.Seen)
        {
            StopSharing(source->first, source->second, ReasonStopped, session);
            source = m_sources.erase(source);
        }
        else
        {
            ++source;
        }
    }
}

void NpcShare::Tick(uint64_t nowMs, bool inSave, bool shareOwnNpcs, const std::vector<Partner>& partners, net::RelaySession& session)
{
    if (!inSave || !session.GetStatus().InSession || (partners.empty() && m_sources.empty()) || !Resolve())
    {
        if (!inSave)
        {
            LetGoOfAll();
        }
        return;
    }
    if (nowMs >= m_nextScanMs)
    {
        m_nextScanMs = nowMs + ScanEveryMs;
        Scan(nowMs, shareOwnNpcs, partners, session);
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t playerArgs;
    playerArgs.emplace_back(nullptr, &gameInstance);
    if (m_getPlayer)
    {
        game::CallStatic(m_getPlayer, &player, playerArgs);
    }
    m_playerAgent = npc_states::AttitudeAgentOf(player);
    m_player = player;
    for (auto died = m_diedAtMs.begin(); died != m_diedAtMs.end();)
    {
        died = nowMs - died->second > 60000 ? m_diedAtMs.erase(died) : std::next(died);
    }
    m_partnerAgents.clear();
    for (const auto& partner : partners)
    {
        auto body = partner.Body;
        m_partnerAgents.push_back(npc_states::AttitudeAgentOf(body));
    }
    for (auto source = m_sources.begin(); source != m_sources.end();)
    {
        RED4ext::Handle<RED4ext::IScriptable> npc;
        static auto* findEntity = game::FindFunction("ScriptGameInstance", "FindEntityByID");
        RED4ext::ent::EntityID entityId(source->first);
        RED4ext::StackArgs_t findArgs;
        findArgs.emplace_back(nullptr, &gameInstance);
        findArgs.emplace_back(nullptr, &entityId);
        if (!findEntity || !game::CallStatic(findEntity, &npc, findArgs) || !npc)
        {
            StopSharing(source->first, source->second, ReasonGone, session);
            source = m_sources.erase(source);
            continue;
        }
        Send(nowMs, source->first, source->second, npc, partners, session);
        ++source;
    }
    if (nowMs >= m_nextOfferCheckMs && !partners.empty())
    {
        m_nextOfferCheckMs = nowMs + OfferCheckEveryMs;
        OfferHandoffs(nowMs, partners, session);
    }
    if (nowMs >= m_nextReportMs)
    {
        m_nextReportMs = nowMs + ReportEveryMs;
        m_log("npcs: " + std::to_string(m_sources.size()) + " of our npc(s) shared now, " + std::to_string(m_sharesStarted) +
              " started so far, " + std::to_string(anim_tap::WatchedNpcCount()) + " graph(s) streamed (last scan: " + std::to_string(m_lastScanEntities) + " entities, " + std::to_string(m_lastScanNpcs) + " npcs, " +
              std::to_string(m_lastScanManaged) + " ours, " + std::to_string(m_lastScanCandidates) + " near, " + std::to_string(m_lastScanDead) + " dead, nearest " + std::to_string(static_cast<int>(m_lastScanNearest)) + " m from a partner; handoffs " +
              std::to_string(m_handoffsOffered) + " offered, " + std::to_string(m_handoffsGiven) + " given, " + std::to_string(m_handoffsTaken) + " taken)");
    }
}
}
