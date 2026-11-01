#include "plugin/SmsReplies.hpp"

#include "net/PlayerExtrasMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <sstream>

namespace nightcoop_mac::sms_replies
{
namespace
{
constexpr uint64_t LookEveryMs = 250;
constexpr uint64_t RulesEveryMs = 5000;
constexpr int32_t ModeOff = 0;
constexpr int32_t ModeHost = 1;
constexpr int32_t ModeGuest = 2;

uint64_t NextLookMs = 0;
uint64_t NextRulesMs = 0;
uint32_t NextRequest = 1;
bool HostSaysAny = true;

const char* VerdictText(uint8_t result)
{
    switch (result)
    {
    case 1: return "taken";
    case 2: return "the question was answered already";
    case 3: return "the question is not open at the host yet";
    case 4: return "the host has no such reply";
    case 5: return "we are not in the hosts world";
    case 6: return "the host answers its own chats only";
    default: return "?";
    }
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::SmsReply);
}

void Tick(uint64_t nowMs, bool inSave, bool weAreHost, bool partnerInOurWorld, bool guestInHostWorld, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || nowMs < NextLookMs)
    {
        return;
    }
    NextLookMs = nowMs + LookEveryMs;
    static auto* configure = game::FindFunction("NightCoopSms", "Configure");
    static auto* takeReplies = game::FindFunction("NightCoopSms", "TakeReplies");
    auto service = game::ScriptService("NightCoopSms");
    if (!configure || !takeReplies || !service)
    {
        return;
    }
    int32_t mode = weAreHost ? (partnerInOurWorld ? ModeHost : ModeOff) : (guestInHostWorld && HostSaysAny ? ModeGuest : ModeOff);
    RED4ext::StackArgs_t configureArgs;
    configureArgs.emplace_back(nullptr, &mode);
    game::CallMethod(service.instance, configure, nullptr, configureArgs);
    if (mode == ModeHost && nowMs >= NextRulesMs)
    {
        NextRulesMs = nowMs + RulesEveryMs;
        session.SendGameMessage(net::WriteSmsReply({net::kSmsRules, 1, 0, 0, 0, 0}));
    }
    RED4ext::CString lines;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, takeReplies, &lines, noArgs);
    std::istringstream input(lines.c_str());
    int64_t entryHash = 0;
    int64_t parentHash = 0;
    while (input >> entryHash >> parentHash)
    {
        net::SmsReply ask;
        ask.Kind = net::kSmsReplyAsk;
        ask.Request = NextRequest++;
        ask.EntryHash = static_cast<uint32_t>(entryHash);
        ask.ParentHash = static_cast<uint32_t>(parentHash);
        if (!ask.EntryHash || !ask.ParentHash)
        {
            continue;
        }
        session.SendGameMessage(net::WriteSmsReply(ask), 0);
        log("sms: we picked a reply in a phone chat, the host picks it for evryone (#" + std::to_string(ask.Request) + ")");
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool partnerInOurWorld, net::RelaySession& session, const LogFunction& log)
{
    net::SmsReply sms;
    if (!net::ReadSmsReply(message, sms))
    {
        return;
    }
    if (weAreHost && sms.Kind == net::kSmsReplyAsk && fromPeer != 0)
    {
        static auto* applyReply = game::FindFunction("NightCoopSms", "ApplyReply");
        auto service = game::ScriptService("NightCoopSms");
        int32_t result = partnerInOurWorld ? 4 : 5;
        if (partnerInOurWorld && applyReply && service)
        {
            int32_t entryHash = static_cast<int32_t>(sms.EntryHash);
            int32_t parentHash = static_cast<int32_t>(sms.ParentHash);
            RED4ext::StackArgs_t args;
            args.emplace_back(nullptr, &entryHash);
            args.emplace_back(nullptr, &parentHash);
            game::CallMethod(service.instance, applyReply, &result, args);
        }
        session.SendGameMessage(net::WriteSmsReply({net::kSmsVerdict, 0, static_cast<uint8_t>(result), sms.Request, sms.EntryHash, 0}), static_cast<int32_t>(fromPeer));
        log("sms: peer " + std::to_string(fromPeer) + " picked a chat reply, " + VerdictText(static_cast<uint8_t>(result)));
        return;
    }
    if (weAreHost || fromPeer != 0)
    {
        return;
    }
    if (sms.Kind == net::kSmsRules)
    {
        HostSaysAny = (sms.Flags & 1) != 0;
    }
    else if (sms.Kind == net::kSmsVerdict)
    {
        log("sms: the host on our chat reply #" + std::to_string(sms.Request) + ": " + VerdictText(sms.Result));
    }
}
}
