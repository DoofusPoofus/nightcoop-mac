#include "plugin/Downed.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Hud.hpp"
#include "plugin/KeyWatch.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/StatsObjectID.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::downed
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t FallsStayAfterPartnerMs = 45000;
constexpr uint64_t PassOutHoldMs = 2000;
constexpr uint64_t HelpHoldMs = 1000;
constexpr uint64_t HelpAnswerMs = 5000;
constexpr uint64_t EveryoneDownMs = 4000;
constexpr float AcceptReviveWithinMeters = 8.0f;
constexpr float HintWithinMeters = 8.0f;
constexpr int32_t StateUp = 0;
constexpr int32_t StateDown = 1;
constexpr float DegreesPerRadian = 57.29578f;

RED4ext::CBaseFunction* PluginTick = nullptr;
RED4ext::CBaseFunction* AskRise = nullptr;
bool Resolved = false;
bool Usable = false;
uint64_t NextTickMs = 0;
uint64_t LastPartnerMs = 0;
bool FallsInForce = false;
int32_t OurState = StateUp;
uint64_t PassOutHeldSinceMs = 0;
uint32_t ReachPeer = 0;
uint64_t HelpHeldSinceMs = 0;
uint32_t WaitingPeer = 0;
uint64_t WaitingSinceMs = 0;
uint32_t HintedPeer = 0;
uint64_t EveryoneDownSinceMs = 0;
bool EveryoneDownSent = false;
uint64_t TestAtMs = 0;
bool TestDone = false;

bool Resolve(const LogFunction& log)
{
    if (Resolved)
    {
        return Usable;
    }
    Resolved = true;
    PluginTick = game::FindFunction("NightCoopDowned", "PluginTick");
    AskRise = game::FindFunction("NightCoopDowned", "AskRise");
    Usable = game::ParamsAre(PluginTick, {"Bool"}) && game::ReturnIs(PluginTick, "Int32") && game::ParamsAre(AskRise, {});
    log(Usable ? "downed: the script half is there, in co-op our player falls instead of dying and partners can help each other up"
               : "downed: no NightCoopDowned script service (scripts/NightCoopMac not compiled?), players die like in single player");
    return Usable;
}

void AskToRise()
{
    auto service = game::ScriptService("NightCoopDowned");
    RED4ext::StackArgs_t noArgs;
    if (service)
    {
        game::CallMethod(service.instance, AskRise, nullptr, noArgs);
    }
}

float DistanceBetween(const net::PlayerState& first, const float* second)
{
    const float deltaX = first.PositionX - second[0];
    const float deltaY = first.PositionY - second[1];
    const float deltaZ = first.PositionZ - second[2];
    return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

bool InReach(const net::PlayerState& ours, const net::PlayerState& theirs, bool keeping)
{
    const float deltaX = theirs.PositionX - ours.PositionX;
    const float deltaY = theirs.PositionY - ours.PositionY;
    const float deltaZ = theirs.PositionZ - ours.PositionZ;
    const float flat = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    const float full = std::sqrt(flat * flat + deltaZ * deltaZ);
    if (full > (keeping ? 3.0f : 2.5f) || std::fabs(deltaZ) > (keeping ? 1.9f : 1.6f))
    {
        return false;
    }
    if (flat <= (keeping ? 1.2f : 0.9f))
    {
        return true;
    }
    if (std::fabs(ours.CameraPitch) > (keeping ? 45.0f : 35.0f))
    {
        return false;
    }
    const float lookYaw = (ours.Yaw + ours.AimYawOffset) / DegreesPerRadian;
    const float lookX = -std::sin(lookYaw);
    const float lookY = std::cos(lookYaw);
    const float cosine = (deltaX * lookX + deltaY * lookY) / flat;
    return cosine >= std::cos((keeping ? 75.0f : 60.0f) / DegreesPerRadian);
}

void HelpPartners(uint64_t nowMs, const net::PlayerState& ours, const std::vector<Partner>& partners, net::RelaySession& session, const LogFunction& log)
{
    if (WaitingPeer)
    {
        const Partner* waitingFor = nullptr;
        for (const auto& partner : partners)
        {
            if (partner.PeerNumber == WaitingPeer)
            {
                waitingFor = &partner;
            }
        }
        if (!waitingFor)
        {
            log("downed: the partner we helped up left");
            WaitingPeer = 0;
        }
        else if (!(waitingFor->State.Flags & DownFlag))
        {
            log("downed: we helped peer " + std::to_string(WaitingPeer) + " up");
            hud::Show("You helped up " + waitingFor->Name, 3.0f);
            WaitingPeer = 0;
        }
        else if (nowMs - WaitingSinceMs > HelpAnswerMs)
        {
            hud::Show(waitingFor->Name + " did not get up, come close and hold F again", 4.0f);
            WaitingPeer = 0;
        }
        return;
    }
    const Partner* inReach = nullptr;
    for (const auto& partner : partners)
    {
        if (!(partner.State.Flags & DownFlag))
        {
            continue;
        }
        if (InReach(ours, partner.State, partner.PeerNumber == ReachPeer))
        {
            inReach = &partner;
            break;
        }
        const float position[3] = {partner.State.PositionX, partner.State.PositionY, partner.State.PositionZ};
        if (HintedPeer != partner.PeerNumber && DistanceBetween(ours, position) <= HintWithinMeters)
        {
            HintedPeer = partner.PeerNumber;
            hud::Show(partner.Name + " lies nearby, come close, look at them and hold F", 4.0f);
        }
    }
    if (!inReach)
    {
        ReachPeer = 0;
        HelpHeldSinceMs = 0;
        return;
    }
    if (ReachPeer != inReach->PeerNumber)
    {
        ReachPeer = inReach->PeerNumber;
        hud::Show("Hold F to help up " + inReach->Name, 3.0f);
    }
    if (!KeyHeld(KeyF))
    {
        HelpHeldSinceMs = 0;
        return;
    }
    if (!HelpHeldSinceMs)
    {
        HelpHeldSinceMs = nowMs;
    }
    if (nowMs - HelpHeldSinceMs < HelpHoldMs)
    {
        return;
    }
    net::PlayerRevive revive;
    revive.TargetPeer = inReach->PeerNumber;
    revive.HelperPosition[0] = ours.PositionX;
    revive.HelperPosition[1] = ours.PositionY;
    revive.HelperPosition[2] = ours.PositionZ;
    session.SendGameMessage(net::WritePlayerRevive(revive), static_cast<int32_t>(inReach->PeerNumber));
    hud::Show("Helping up " + inReach->Name + "...", 2.0f);
    log("downed: helping peer " + std::to_string(inReach->PeerNumber) + " up (PlayerRevive sent)");
    WaitingPeer = inReach->PeerNumber;
    WaitingSinceMs = nowMs;
    HelpHeldSinceMs = 0;
}
}

void Tick(uint64_t nowMs, bool inSave, const net::PlayerState* ourState, const std::vector<Partner>& partners, net::RelaySession& session,
          const LogFunction& log)
{
    if (nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    if (!inSave || !session.GetStatus().InSession)
    {
        FallsInForce = false;
        OurState = StateUp;
        return;
    }
    if (!Resolve(log))
    {
        return;
    }
    WatchKeys();
    if (!partners.empty())
    {
        LastPartnerMs = nowMs;
    }
    const bool fallsInForce = LastPartnerMs && nowMs - LastPartnerMs < FallsStayAfterPartnerMs;
    if (fallsInForce != FallsInForce)
    {
        FallsInForce = fallsInForce;
        log(fallsInForce ? "downed: in co-op our player falls instead of dying (immortal at 1 %)" : "downed: no partner for 45 s, our player dies normally again");
    }
    auto service = game::ScriptService("NightCoopDowned");
    if (!service)
    {
        return;
    }
    bool inForce = fallsInForce;
    int32_t state = OurState;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &inForce);
    game::CallMethod(service.instance, PluginTick, &state, args);
    if (state != OurState)
    {
        if (state == StateDown)
        {
            log("downed: our player fell, a partner can help us up (F) or we hold T to pass out");
            hud::Show("You are dying, a partner next to you can help you up (F). Hold T to pass out", 6.0f);
        }
        else if (state == StateUp)
        {
            log("downed: our player is up again");
        }
        OurState = state;
    }
    if (OurState == StateDown && KeyHeld(KeyT))
    {
        if (!PassOutHeldSinceMs)
        {
            PassOutHeldSinceMs = nowMs;
        }
        if (nowMs - PassOutHeldSinceMs >= PassOutHoldMs)
        {
            AskToRise();
            hud::Show("You came to", 4.0f);
            log("downed: T held, we pass out and come to where we lie");
            PassOutHeldSinceMs = 0;
        }
    }
    else
    {
        PassOutHeldSinceMs = 0;
    }
    if (ourState && OurState == StateUp)
    {
        HelpPartners(nowMs, *ourState, partners, session, log);
    }
    const bool everyoneDown = OurState != StateUp && !partners.empty() &&
                              std::all_of(partners.begin(), partners.end(), [](const Partner& partner) { return (partner.State.Flags & DownFlag) != 0; });
    if (session.GetStatus().MyPeerNumber == 0 && everyoneDown)
    {
        if (!EveryoneDownSinceMs)
        {
            EveryoneDownSinceMs = nowMs;
        }
        if (!EveryoneDownSent && nowMs - EveryoneDownSinceMs > EveryoneDownMs)
        {
            EveryoneDownSent = true;
            session.SendGameMessage(net::WriteDownedNotice(2));
            AskToRise();
            hud::Show("Everyone is down, everyone passes out", 4.0f);
            log("downed: evryone is down, evryone passes out (DownedNotice 2)");
        }
    }
    else
    {
        EveryoneDownSinceMs = 0;
        EveryoneDownSent = false;
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, const net::PlayerState* ourState, net::RelaySession& session,
               const LogFunction& log)
{
    if (!Usable)
    {
        return;
    }
    const uint8_t type = net::MessageTypeOf(message);
    net::PlayerRevive revive;
    uint8_t noticeKind = 0;
    if (type == static_cast<uint8_t>(nc::proto::MsgType::PlayerRevive) && net::ReadPlayerRevive(message, revive))
    {
        if (revive.TargetPeer != session.GetStatus().MyPeerNumber)
        {
            return;
        }
        if (OurState != StateDown)
        {
            log("downed: PlayerRevive from peer " + std::to_string(fromPeer) + " came but we are not down, nothing to do");
            return;
        }
        const float distance = ourState ? DistanceBetween(*ourState, revive.HelperPosition) : 0.0f;
        if (distance > AcceptReviveWithinMeters)
        {
            log("downed: peer " + std::to_string(fromPeer) + " asked to help us up from " + std::to_string(static_cast<int>(distance)) +
                " m, too far, ignored");
            return;
        }
        AskToRise();
        hud::Show("Your partner helped you up", 4.0f);
        log("downed: peer " + std::to_string(fromPeer) + " helps us up");
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::DownedNotice) && fromPeer == 0 && net::ReadDownedNotice(message, noticeKind))
    {
        if (OurState == StateDown)
        {
            AskToRise();
            hud::Show("Everyone is down, everyone passes out", 4.0f);
            log("downed: the host says evryone is down, we pass out");
        }
    }
}

bool WeAreDown()
{
    return OurState != StateUp;
}

void RunTest(uint64_t nowMs, bool partnerHere, const LogFunction& log)
{
    static uint64_t nextDescribeMs = 0;
    static auto* describe = game::FindFunction("NightCoopDowned", "Describe");
    if (TestDone && nowMs >= nextDescribeMs && describe)
    {
        nextDescribeMs = nowMs + 2000;
        auto service = game::ScriptService("NightCoopDowned");
        RED4ext::CString text;
        RED4ext::StackArgs_t noArgs;
        if (service && game::CallMethod(service.instance, describe, &text, noArgs))
        {
            log(std::string("test downed: ") + text.c_str());
        }
    }
    if (TestDone || !partnerHere || !FallsInForce)
    {
        return;
    }
    if (!TestAtMs)
    {
        TestAtMs = nowMs + 5000;
    }
    if (nowMs < TestAtMs)
    {
        return;
    }
    if (TestDone)
    {
        return;
    }
    TestDone = true;
    static auto* dropHealth = game::FindFunction("NightCoopDowned", "TestDropHealth");
    auto service = game::ScriptService("NightCoopDowned");
    RED4ext::StackArgs_t noArgs;
    if (!service || !dropHealth)
    {
        return;
    }
    game::CallMethod(service.instance, dropHealth, nullptr, noArgs);
    log("test downed: our health set to 1 %");
}
}
