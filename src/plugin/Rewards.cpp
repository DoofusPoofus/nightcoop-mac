#include "plugin/Rewards.hpp"

#include "net/CombatMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <cstdio>
#include <set>

namespace nightcoop_mac::rewards
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t ReportEveryMs = 30000;

struct ScriptCalls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Configure = nullptr;
    RED4ext::CBaseFunction* TakeOutgoing = nullptr;
    RED4ext::CBaseFunction* GiveHostReward = nullptr;
    RED4ext::CBaseFunction* Describe = nullptr;
};

ScriptCalls Script;
uint64_t NextTickMs = 0;
uint64_t NextReportMs = 0;
uint32_t NextSerial = 1;
std::set<uint32_t> TakenSerials;
bool TroubleLogged = false;

bool Ready()
{
    if (Script.Resolved)
    {
        return Script.Usable;
    }
    Script.Resolved = true;
    Script.Configure = game::FindFunction("NightCoopRewards", "Configure");
    Script.TakeOutgoing = game::FindFunction("NightCoopRewards", "TakeOutgoing");
    Script.GiveHostReward = game::FindFunction("NightCoopRewards", "GiveHostReward");
    Script.Describe = game::FindFunction("NightCoopRewards", "Describe");
    Script.Usable = game::ParamsAre(Script.Configure, {"Bool", "Bool"}) && game::ParamsAre(Script.TakeOutgoing, {}) &&
                    game::ParamsAre(Script.GiveHostReward, {"TweakDBID"}) && game::ParamsAre(Script.Describe, {});
    return Script.Usable;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == net::kRewardWordType;
}

void Tick(uint64_t nowMs, bool inSave, bool weAreHost, bool partnerInOurWorld, bool takeHostRewards, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    if (!Ready())
    {
        if (!TroubleLogged)
        {
            TroubleLogged = true;
            log("rewards: the NightCoopRewards script service looks different than expected, quest rewards stay ours");
        }
        return;
    }
    auto service = game::ScriptService("NightCoopRewards");
    if (!service)
    {
        return;
    }
    bool takeHost = takeHostRewards && !weAreHost;
    bool shareOurs = weAreHost && partnerInOurWorld;
    RED4ext::StackArgs_t configureArgs;
    configureArgs.emplace_back(nullptr, &takeHost);
    configureArgs.emplace_back(nullptr, &shareOurs);
    game::CallMethod(service.instance, Script.Configure, nullptr, configureArgs);
    RED4ext::DynArray<RED4ext::TweakDBID> outgoing;
    RED4ext::StackArgs_t noArgs;
    if (game::CallMethod(service.instance, Script.TakeOutgoing, &outgoing, noArgs))
    {
        for (const auto& reward : outgoing)
        {
            net::RewardWord word;
            word.Kind = net::kRewardQuest;
            word.Reward = reward.value;
            word.Serial = NextSerial++;
            session.SendGameMessage(net::WriteRewardWord(word));
            char line[120];
            std::snprintf(line, sizeof(line), "rewards: our quest reward %#llx goes to the partners in our world (#%u)",
                          static_cast<unsigned long long>(word.Reward), word.Serial);
            log(line);
        }
    }
    if (nowMs >= NextReportMs)
    {
        NextReportMs = nowMs + ReportEveryMs;
        RED4ext::CString text;
        if ((takeHost || shareOurs) && game::CallMethod(service.instance, Script.Describe, &text, noArgs))
        {
            log(std::string("rewards: ") + (takeHost ? "we take the hosts quest rewards" : "our quest rewards go to partners") + " (" + text.c_str() + ")");
        }
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool takeHostRewards, const LogFunction& log)
{
    net::RewardWord word;
    if (!net::ReadRewardWord(message, word) || word.Kind != net::kRewardQuest || fromPeer != 0 || !takeHostRewards || !Ready())
    {
        return;
    }
    if (!TakenSerials.insert(word.Serial).second)
    {
        return;
    }
    auto service = game::ScriptService("NightCoopRewards");
    RED4ext::TweakDBID reward(word.Reward);
    bool given = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &reward);
    if (service)
    {
        game::CallMethod(service.instance, Script.GiveHostReward, &given, args);
    }
    char line[140];
    std::snprintf(line, sizeof(line), "rewards: the hosts quest reward %#llx (#%u) %s", static_cast<unsigned long long>(word.Reward), word.Serial,
                  given ? "is ours too now" : "could not be given");
    log(line);
}
}
