#include "plugin/TeamBuild.hpp"

#include "net/PlayerExtrasMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/PartnerRoster.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/StatsObjectID.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <algorithm>
#include <cmath>
#include <map>

namespace nightcoop_mac::team_build
{
namespace
{
constexpr uint64_t ReadEveryMs = 1000;
constexpr uint64_t SendAgainMs = 5000;
constexpr uint64_t HandAgainMs = 5000;
constexpr uint64_t ForgetPartnerAfterMs = 15000;
constexpr int32_t MostScore = 20;
constexpr int NoBonus = -1;
const char* ServiceName = "NightCoopTeamBuild";

struct BuildRule
{
    const char* Name;
    uint8_t Attribute;
    int Bonus;
};

const BuildRule Builds[] = {{"cool", net::kBuildCool, NoBonus},
                            {"netrunner", net::kBuildIntelligence, net::kBuildIntelligenceBonus},
                            {"reflexes", net::kBuildReflexes, NoBonus},
                            {"solo", net::kBuildStrength, net::kBuildStrengthBonus},
                            {"techie", net::kBuildTechnicalAbility, net::kBuildTechnicalAbilityBonus}};
constexpr size_t BuildCount = sizeof(Builds) / sizeof(Builds[0]);

struct StatSheet
{
    float Values[net::kBuildStatCount] = {};
    uint32_t Known = 0;
};

struct Partner
{
    StatSheet Sheet;
    uint32_t Revision = 0;
    uint64_t HeardMs = 0;
};

struct Calls
{
    bool Resolved = false;
    bool StatsUsable = false;
    bool ScriptUsable = false;
    RED4ext::CBaseFunction* GetPlayer = nullptr;
    RED4ext::CBaseFunction* GetEntityId = nullptr;
    RED4ext::CBaseFunction* GetStatsSystem = nullptr;
    RED4ext::CBaseFunction* GetStatValue = nullptr;
    RED4ext::CBaseFunction* UseTeamBest = nullptr;
    RED4ext::CProperty* Lifted = nullptr;
    RED4ext::CProperty* LastOurs = nullptr;
    RED4ext::CProperty* LastTeam = nullptr;
    RED4ext::CProperty* LastBuild = nullptr;
    uint32_t StatTypes[net::kBuildStatCount] = {};
    uint32_t StatTypesKnown = 0;
};

Calls Game;
StatSheet Ours;
uint32_t OurRevision = 0;
uint64_t NextReadMs = 0;
uint64_t LastSentMs = 0;
bool OursEverSent = false;
std::map<uint32_t, Partner> Partners;
int32_t HandedBest[BuildCount] = {-1, -1, -1, -1, -1};
bool HandedOn = false;
uint64_t LastHandedMs = 0;
int32_t LiftedSeen = 0;

RED4ext::CProperty* IntField(RED4ext::CClass* owner, const char* name)
{
    auto* property = owner ? owner->GetProperty(name) : nullptr;
    return property && property->type && std::string(property->type->GetName().ToString()) == "Int32" ? property : nullptr;
}

void Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return;
    }
    Game.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    Game.GetPlayer = rtti->GetFunction("GetPlayer;GameInstance");
    Game.GetEntityId = game::FindFunction("entEntity", "GetEntityID");
    Game.GetStatsSystem = game::FindFunction("ScriptGameInstance", "GetStatsSystem");
    Game.GetStatValue = game::FindFunction("gameStatsSystem", "GetStatValue");
    std::string unknownStats;
    if (auto* statTypes = rtti->GetEnum("gamedataStatType"))
    {
        for (uint8_t index = 0; index < net::kBuildStatCount; ++index)
        {
            const RED4ext::CName wanted(net::BuildStatName(index));
            for (uint32_t entry = 0; entry < statTypes->hashList.Size() && entry < statTypes->valueList.Size(); ++entry)
            {
                if (statTypes->hashList[entry] == wanted)
                {
                    Game.StatTypes[index] = static_cast<uint32_t>(statTypes->valueList[entry]);
                    Game.StatTypesKnown |= 1u << index;
                }
            }
            if (!(Game.StatTypesKnown & (1u << index)))
            {
                unknownStats += std::string(unknownStats.empty() ? "" : ", ") + net::BuildStatName(index);
            }
        }
    }
    Game.StatsUsable = Game.GetPlayer && game::ParamsAre(Game.GetEntityId, {}) && game::ParamsAre(Game.GetStatsSystem, {"ScriptGameInstance"}) &&
                       game::ParamsAre(Game.GetStatValue, {"gameStatsObjectID", "gamedataStatType"}) && game::ReturnIs(Game.GetStatValue, "Float") &&
                       Game.StatTypesKnown != 0;
    auto* teamClass = rtti->GetClass(ServiceName);
    Game.UseTeamBest = game::FindFunction(ServiceName, "UseTeamBest");
    Game.Lifted = IntField(teamClass, "lifted");
    Game.LastOurs = IntField(teamClass, "lastOurs");
    Game.LastTeam = IntField(teamClass, "lastTeam");
    Game.LastBuild = IntField(teamClass, "lastBuild");
    Game.ScriptUsable = game::ParamsAre(Game.UseTeamBest, {"Bool", "Int32", "Int32", "Int32", "Int32", "Int32"}) && Game.Lifted && Game.LastOurs &&
                        Game.LastTeam && Game.LastBuild;
    log(std::string("teambuild: ") + (Game.StatsUsable ? "our build goes to partners" : "our stats cant be read, partners dont get our build") + ", " +
        (Game.ScriptUsable ? "attribute checks count the best of the team" : "no NightCoopTeamBuild script service, attribute checks stay ours") +
        (unknownStats.empty() ? "" : " (stats the game does not know: " + unknownStats + ")"));
}

bool ReadOurStats(StatSheet& sheet)
{
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::Handle<RED4ext::IScriptable> statsSystem;
    RED4ext::StackArgs_t gameArgs;
    gameArgs.emplace_back(nullptr, &gameInstance);
    uint64_t entityHash = 0;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallStatic(Game.GetPlayer, &player, gameArgs) || !player || !game::CallMethod(player.instance, Game.GetEntityId, &entityHash, noArgs) ||
        !entityHash || !game::CallStatic(Game.GetStatsSystem, &statsSystem, gameArgs) || !statsSystem)
    {
        return false;
    }
    RED4ext::game::StatsObjectID statsObject {};
    statsObject.entityHash = entityHash;
    statsObject.idType = RED4ext::game::StatIDType::EntityID;
    sheet = StatSheet {};
    for (uint8_t index = 0; index < net::kBuildStatCount; ++index)
    {
        if (!(Game.StatTypesKnown & (1u << index)))
        {
            continue;
        }
        uint32_t statType = Game.StatTypes[index];
        float value = 0;
        RED4ext::StackArgs_t valueArgs;
        valueArgs.emplace_back(nullptr, &statsObject);
        valueArgs.emplace_back(nullptr, &statType);
        if (game::CallMethod(statsSystem.instance, Game.GetStatValue, &value, valueArgs) && std::isfinite(value))
        {
            sheet.Values[index] = std::clamp(value, 0.0f, 10000.0f);
            sheet.Known |= 1u << index;
        }
    }
    return sheet.Known != 0;
}

bool SameSheet(const StatSheet& first, const StatSheet& second)
{
    if (first.Known != second.Known)
    {
        return false;
    }
    for (uint8_t index = 0; index < net::kBuildStatCount; ++index)
    {
        if (std::fabs(first.Values[index] - second.Values[index]) > 0.001f)
        {
            return false;
        }
    }
    return true;
}

std::string DescribeSheet(const StatSheet& sheet)
{
    std::string text;
    for (uint8_t index = 0; index < net::kBuildStatCount; ++index)
    {
        if (sheet.Known & (1u << index))
        {
            text += std::string(text.empty() ? "" : " ") + net::BuildStatName(index) + " " + std::to_string(static_cast<int>(sheet.Values[index]));
        }
    }
    return text.empty() ? "nothing" : text;
}

void ShareOurs(uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    StatSheet sheet;
    if (!Game.StatsUsable || !ReadOurStats(sheet))
    {
        return;
    }
    const bool changed = !SameSheet(sheet, Ours);
    if (!changed && OursEverSent && nowMs - LastSentMs < SendAgainMs)
    {
        return;
    }
    if (changed)
    {
        Ours = sheet;
        ++OurRevision;
        log("teambuild: our build goes out (" + DescribeSheet(Ours) + ", rev " + std::to_string(OurRevision) + ")");
    }
    net::BuildStats stats;
    stats.Revision = OurRevision;
    stats.Flags = net::kBuildChecksAny;
    for (uint8_t index = 0; index < net::kBuildStatCount; ++index)
    {
        if (Ours.Known & (1u << index))
        {
            stats.Stats.push_back({index, Ours.Values[index]});
        }
    }
    session.SendGameMessage(net::WriteBuildStats(stats));
    LastSentMs = nowMs;
    OursEverSent = true;
}

int32_t ScoreOf(const StatSheet& sheet, const BuildRule& build)
{
    if (!(sheet.Known & (1u << build.Attribute)) || (build.Bonus != NoBonus && !(sheet.Known & (1u << build.Bonus))))
    {
        return -1;
    }
    const int32_t bonus = build.Bonus != NoBonus ? static_cast<int32_t>(sheet.Values[build.Bonus]) : 0;
    return std::min(static_cast<int32_t>(sheet.Values[build.Attribute]) + bonus, MostScore);
}

void HandTeamBest(uint64_t nowMs, bool weAreHost, const LogFunction& log)
{
    if (!Game.ScriptUsable)
    {
        return;
    }
    auto service = game::ScriptService(ServiceName);
    if (!service)
    {
        return;
    }
    int32_t best[BuildCount] = {0, 0, 0, 0, 0};
    bool anyPartner = false;
    for (const auto& [peerNumber, partner] : Partners)
    {
        if (weAreHost && !partner_roster::PeerInHostWorld(peerNumber))
        {
            continue;
        }
        anyPartner = true;
        for (size_t build = 0; build < BuildCount; ++build)
        {
            best[build] = std::max(best[build], ScoreOf(partner.Sheet, Builds[build]));
        }
    }
    const bool same = anyPartner == HandedOn && std::equal(best, best + BuildCount, HandedBest);
    if (!same || nowMs - LastHandedMs >= HandAgainMs)
    {
        bool on = anyPartner;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &on);
        for (auto& score : best)
        {
            args.emplace_back(nullptr, &score);
        }
        game::CallMethod(service.instance, Game.UseTeamBest, nullptr, args);
        LastHandedMs = nowMs;
        if (!same)
        {
            log("teambuild: the teams best for attribute checks is cool " + std::to_string(best[0]) + ", netrunner " + std::to_string(best[1]) +
                ", reflexes " + std::to_string(best[2]) + ", solo " + std::to_string(best[3]) + ", techie " + std::to_string(best[4]) +
                (anyPartner ? "" : " (no partner in the hosts world, checks stay ours)"));
        }
        HandedOn = anyPartner;
        std::copy(best, best + BuildCount, HandedBest);
    }
    const int32_t lifted = *Game.Lifted->GetValuePtr<int32_t>(service.instance);
    if (lifted != LiftedSeen)
    {
        LiftedSeen = lifted;
        const int32_t build = *Game.LastBuild->GetValuePtr<int32_t>(service.instance);
        log("teambuild: a " + std::string(build >= 0 && build < static_cast<int32_t>(BuildCount) ? Builds[build].Name : "?") +
            " check counted the teams " + std::to_string(*Game.LastTeam->GetValuePtr<int32_t>(service.instance)) + " instead of our " +
            std::to_string(*Game.LastOurs->GetValuePtr<int32_t>(service.instance)) + " (" + std::to_string(lifted) + " lifted so far)");
    }
}
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, const LogFunction& log)
{
    net::BuildStats stats;
    if (!net::ReadBuildStats(message, stats))
    {
        log("teambuild: bad BuildStats from peer " + std::to_string(fromPeer) + " (" + std::to_string(message.size()) + " bytes)");
        return;
    }
    auto& partner = Partners[fromPeer];
    StatSheet sheet;
    for (const auto& stat : stats.Stats)
    {
        if (stat.Index < net::kBuildStatCount)
        {
            sheet.Values[stat.Index] = stat.Value;
            sheet.Known |= 1u << stat.Index;
        }
    }
    if (partner.HeardMs == 0 || !SameSheet(sheet, partner.Sheet))
    {
        log("teambuild: peer " + std::to_string(fromPeer) + " build is " + DescribeSheet(sheet) + " (rev " + std::to_string(stats.Revision) + ")");
    }
    partner.Sheet = sheet;
    partner.Revision = stats.Revision;
    partner.HeardMs = nowMs;
}

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log)
{
    if (nowMs < NextReadMs)
    {
        return;
    }
    NextReadMs = nowMs + ReadEveryMs;
    std::erase_if(Partners, [nowMs](const auto& entry) { return nowMs - entry.second.HeardMs > ForgetPartnerAfterMs; });
    const auto status = session.GetStatus();
    if (!inSave || !status.InSession)
    {
        Partners.clear();
        if (HandedOn)
        {
            HandTeamBest(nowMs, false, log);
        }
        return;
    }
    Resolve(log);
    ShareOurs(nowMs, session, log);
    HandTeamBest(nowMs, status.MyPeerNumber == 0, log);
}
}
