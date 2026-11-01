#include "plugin/NpcDeaths.hpp"

#include "net/NpcMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/NpcStates.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace nightcoop_mac::npc_deaths
{
namespace
{
constexpr uint64_t FullTableEveryMs = 30000;
constexpr uint64_t MatchEveryMs = 3000;
constexpr float SamePlaceMeters = 3.0f;

std::vector<net::DeadPerson> DeadTable;
std::vector<net::DeadPerson> FreshDeaths;
std::vector<net::DeadPerson> WaitingKills;
std::set<uint64_t> HandledEntities;
std::map<uint64_t, int> KillTries;
uint64_t NextFullTableMs = 0;
uint64_t NextMatchMs = 0;
bool WasInSave = false;

float MetersBetween(const float* first, const float* second)
{
    const float x = first[0] - second[0];
    const float y = first[1] - second[1];
    const float z = first[2] - second[2];
    return std::sqrt(x * x + y * y + z * z);
}

bool AlreadyListed(const std::vector<net::DeadPerson>& people, const net::DeadPerson& person)
{
    return std::any_of(people.begin(), people.end(), [&person](const net::DeadPerson& listed) { return listed.Entity == person.Entity; });
}

bool AddToTable(const net::DeadPerson& person)
{
    if (AlreadyListed(DeadTable, person))
    {
        return false;
    }
    if (DeadTable.size() >= net::kMostDeadInTable)
    {
        DeadTable.erase(DeadTable.begin());
    }
    DeadTable.push_back(person);
    return true;
}

bool WaitToKill(const net::DeadPerson& person)
{
    if (HandledEntities.count(person.Entity) || AlreadyListed(WaitingKills, person))
    {
        return false;
    }
    if (WaitingKills.size() >= net::kMostDeadInTable)
    {
        WaitingKills.erase(WaitingKills.begin());
    }
    WaitingKills.push_back(person);
    return true;
}

void Forget()
{
    DeadTable.clear();
    FreshDeaths.clear();
    WaitingKills.clear();
    HandledEntities.clear();
    KillTries.clear();
    NextFullTableMs = 0;
}

void KillMatches(const LogFunction& log)
{
    static auto* getEntityList = game::FindFunction("ScriptGameInstance", "GetEntityList");
    static auto* getEntityId = game::FindFunction("entEntity", "GetEntityID");
    static auto* getWorldPosition = game::FindFunction("entEntity", "GetWorldPosition");
    static auto* getRecordId = game::FindFunction("gamePuppetBase", "GetRecordID");
    static auto* isDead = game::FindFunction("NPCPuppet", "IsDead");
    static auto* kill = game::FindFunction("NightCoopCombat", "KillForDeathTable");
    static auto* npcClass = RED4ext::CRTTISystem::Get()->GetClass("NPCPuppet");
    static const bool usable = game::ParamsAre(getEntityList, {"ScriptGameInstance"}) && game::ParamsAre(getEntityId, {}) &&
                               game::ParamsAre(getWorldPosition, {}) && game::ParamsAre(getRecordId, {}) && game::ParamsAre(isDead, {}) && kill && npcClass;
    auto combat = game::ScriptService("NightCoopCombat");
    if (!usable || !combat)
    {
        static bool toldOnce = false;
        if (!toldOnce)
        {
            toldOnce = true;
            log("npc deaths: the entity list or the npc kill call looks different, deaths of the others stay theirs");
        }
        WaitingKills.clear();
        return;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> entities;
    RED4ext::StackArgs_t listArgs;
    listArgs.emplace_back(nullptr, &gameInstance);
    if (!game::CallStatic(getEntityList, &entities, listArgs))
    {
        return;
    }
    struct LocalNpc
    {
        RED4ext::Handle<RED4ext::IScriptable> Npc;
        net::DeadPerson Person;
        bool KeysRead = false;
        bool Taken = false;
    };
    std::vector<LocalNpc> localNpcs;
    RED4ext::StackArgs_t noArgs;
    for (auto& entity : entities)
    {
        auto* entityClass = entity ? entity->GetType() : nullptr;
        if (!entityClass || !entityClass->IsA(npcClass))
        {
            continue;
        }
        LocalNpc local;
        local.Npc = entity;
        RED4ext::TweakDBID record;
        RED4ext::Vector4 position {};
        game::CallMethod(entity.instance, getEntityId, &local.Person.Entity, noArgs);
        game::CallMethod(entity.instance, getRecordId, &record, noArgs);
        game::CallMethod(entity.instance, getWorldPosition, &position, noArgs);
        local.Person.Record = record.value & 0xFFFFFFFFFFULL;
        local.Person.Position[0] = position.X;
        local.Person.Position[1] = position.Y;
        local.Person.Position[2] = position.Z;
        localNpcs.push_back(std::move(local));
    }
    for (auto waiting = WaitingKills.begin(); waiting != WaitingKills.end();)
    {
        LocalNpc* match = nullptr;
        for (auto& local : localNpcs)
        {
            if (!local.Taken && local.Person.Entity == waiting->Entity)
            {
                match = &local;
                break;
            }
        }
        const bool sameEntity = match != nullptr;
        float nearest = SamePlaceMeters;
        for (auto& local : localNpcs)
        {
            const float meters = MetersBetween(waiting->Position, local.Person.Position);
            if (sameEntity || local.Taken || local.Person.Record != waiting->Record || meters > nearest)
            {
                continue;
            }
            if (!local.KeysRead)
            {
                npc_states::MatchKeys(local.Person.Entity, local.Person.MatchKeyA, local.Person.MatchKeyB);
                local.KeysRead = true;
            }
            if ((waiting->MatchKeyA || waiting->MatchKeyB) && (waiting->MatchKeyA != local.Person.MatchKeyA || waiting->MatchKeyB != local.Person.MatchKeyB))
            {
                continue;
            }
            nearest = meters;
            match = &local;
        }
        if (!match)
        {
            ++waiting;
            continue;
        }
        match->Taken = true;
        bool dead = false;
        game::CallMethod(match->Npc.instance, isDead, &dead, noArgs);
        int& tries = KillTries[waiting->Entity];
        char line[160];
        if (!dead && tries < 2)
        {
            ++tries;
            RED4ext::CString killed;
            RED4ext::StackArgs_t killArgs;
            killArgs.emplace_back(nullptr, &match->Npc);
            const bool called = game::CallMethod(combat.instance, kill, &killed, killArgs);
            std::snprintf(line, sizeof(line), "npc deaths: our matching npc %#llx (record %#llx) dies too (", static_cast<unsigned long long>(match->Person.Entity),
                          static_cast<unsigned long long>(match->Person.Record));
            log(std::string(line) + (called ? killed.c_str() : "the kill call failed") + ")");
            ++waiting;
            continue;
        }
        if (!dead)
        {
            std::snprintf(line, sizeof(line), "npc deaths: our npc %#llx (record %#llx) would not die here (no health pool or protected)",
                          static_cast<unsigned long long>(match->Person.Entity), static_cast<unsigned long long>(match->Person.Record));
            log(line);
        }
        KillTries.erase(waiting->Entity);
        HandledEntities.insert(waiting->Entity);
        waiting = WaitingKills.erase(waiting);
    }
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::NpcDead) || messageType == static_cast<uint8_t>(nc::proto::MsgType::NpcDefeat);
}

void NoteDeath(const net::DeadPerson& person)
{
    HandledEntities.insert(person.Entity);
    if (AddToTable(person))
    {
        FreshDeaths.push_back(person);
    }
}

void Tick(uint64_t nowMs, bool inSave, bool weAreHost, const std::vector<uint32_t>& peersInOurWorld, bool takeHostDeaths, net::RelaySession& session,
          const LogFunction& log)
{
    if (!inSave)
    {
        if (WasInSave)
        {
            Forget();
        }
        WasInSave = false;
        return;
    }
    WasInSave = true;
    if (weAreHost)
    {
        if (!FreshDeaths.empty() && !peersInOurWorld.empty())
        {
            const auto message = net::WriteNpcDead(false, FreshDeaths);
            for (const uint32_t peer : peersInOurWorld)
            {
                session.SendGameMessage(message, static_cast<int32_t>(peer));
            }
            log("npc deaths: " + std::to_string(FreshDeaths.size()) + " new dead npcs go to the " + std::to_string(peersInOurWorld.size()) +
                " partner(s) in our world (" + std::to_string(DeadTable.size()) + " in the table)");
        }
        FreshDeaths.clear();
        if (!DeadTable.empty() && !peersInOurWorld.empty() && nowMs >= NextFullTableMs)
        {
            NextFullTableMs = nowMs + FullTableEveryMs;
            const auto message = net::WriteNpcDead(true, DeadTable);
            for (const uint32_t peer : peersInOurWorld)
            {
                session.SendGameMessage(message, static_cast<int32_t>(peer));
            }
        }
    }
    else
    {
        if (takeHostDeaths && session.GetStatus().InSession)
        {
            for (size_t first = 0; first < FreshDeaths.size(); first += net::kMostDefeatsAtOnce)
            {
                const std::vector<net::DeadPerson> part(FreshDeaths.begin() + static_cast<std::ptrdiff_t>(first),
                                                        FreshDeaths.begin() + static_cast<std::ptrdiff_t>(std::min(FreshDeaths.size(), first + net::kMostDefeatsAtOnce)));
                session.SendGameMessage(net::WriteNpcDefeat(session.SessionClockMs(), part), 0);
            }
            if (!FreshDeaths.empty())
            {
                log("npc deaths: told the host " + std::to_string(FreshDeaths.size()) + " of our npcs died");
            }
        }
        FreshDeaths.clear();
    }
    if (!WaitingKills.empty() && nowMs >= NextMatchMs)
    {
        NextMatchMs = nowMs + MatchEveryMs;
        KillMatches(log);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool takeHostDeaths, const LogFunction& log)
{
    std::vector<net::DeadPerson> people;
    bool fullTable = false;
    uint64_t senderClockMs = 0;
    if (!weAreHost && fromPeer == 0 && net::ReadNpcDead(message, fullTable, people))
    {
        if (!takeHostDeaths)
        {
            return;
        }
        size_t waiting = 0;
        for (const auto& person : people)
        {
            waiting += WaitToKill(person) ? 1 : 0;
        }
        if (waiting)
        {
            log("npc deaths: the host says " + std::to_string(people.size()) + " npcs died there" + (fullTable ? " (whole table)" : "") + ", " +
                std::to_string(waiting) + " new for us");
        }
    }
    else if (weAreHost && fromPeer != 0 && net::ReadNpcDefeat(message, senderClockMs, people))
    {
        size_t added = 0;
        for (const auto& person : people)
        {
            if (AddToTable(person))
            {
                FreshDeaths.push_back(person);
                WaitToKill(person);
                ++added;
            }
        }
        log("npc deaths: peer " + std::to_string(fromPeer) + " says " + std::to_string(people.size()) + " of its npcs died (" + std::to_string(added) +
            " new), our matching ones die too");
    }
}
}
