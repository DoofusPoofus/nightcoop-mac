#include "plugin/PhoneShare.hpp"

#include "net/GameMessages.hpp"
#include "net/PlayerExtrasMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>

#include <set>

namespace nightcoop_mac::phone_share
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t ReportEveryMs = 30000;
constexpr int32_t ModeOff = 0;
constexpr int32_t ModeHost = 1;
constexpr int32_t ModeGuest = 2;

struct ScriptCalls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Configure = nullptr;
    RED4ext::CBaseFunction* TakeOutgoing = nullptr;
    RED4ext::CBaseFunction* Play = nullptr;
    RED4ext::CBaseFunction* Describe = nullptr;
    RED4ext::CClass* CallClass = nullptr;
};

ScriptCalls Script;
uint64_t NextTickMs = 0;
uint64_t NextReportMs = 0;
int32_t Mode = ModeOff;
uint32_t NextCommit = 1;
uint32_t NextRequest = 1;
uint32_t CallId = 0;
std::string CallContact;
bool CallByPlayer = false;
std::set<uint32_t> PlayedCommits;
bool TroubleLogged = false;

bool Ready()
{
    if (Script.Resolved)
    {
        return Script.Usable;
    }
    Script.Resolved = true;
    Script.Configure = game::FindFunction("NightCoopPhone", "Configure");
    Script.TakeOutgoing = game::FindFunction("NightCoopPhone", "TakeOutgoing");
    Script.Play = game::FindFunction("NightCoopPhone", "Play");
    Script.Describe = game::FindFunction("NightCoopPhone", "Describe");
    Script.CallClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopPhoneCall");
    Script.Usable = Script.CallClass && game::ParamsAre(Script.Configure, {"Int32"}) && game::ParamsAre(Script.TakeOutgoing, {}) &&
                    game::ParamsAre(Script.Play, {"CName", "Int32", "Int32", "Int32", "Int32"}) && game::ParamsAre(Script.Describe, {});
    return Script.Usable;
}

template<typename Value>
Value Field(RED4ext::IScriptable* object, const char* name)
{
    Value value {};
    auto* property = Script.CallClass->GetProperty(name);
    if (property && property->type && property->type->GetSize() == sizeof(Value))
    {
        value = *property->GetValuePtr<Value>(object);
    }
    return value;
}

bool Play(const net::PhoneSync& phone)
{
    auto service = game::ScriptService("NightCoopPhone");
    if (!service || phone.Action != net::kPhoneActionCall)
    {
        return false;
    }
    RED4ext::CName contact(phone.Contact.c_str());
    int32_t callPhase = phone.CallPhase;
    int32_t callMode = phone.CallMode;
    int32_t visuals = phone.Visuals;
    int32_t flags = phone.Flags;
    bool played = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &contact);
    args.emplace_back(nullptr, &callPhase);
    args.emplace_back(nullptr, &callMode);
    args.emplace_back(nullptr, &visuals);
    args.emplace_back(nullptr, &flags);
    return game::CallMethod(service.instance, Script.Play, &played, args) && played;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::PhoneSync);
}

void Tick(uint64_t nowMs, bool inSave, bool weAreHost, bool partnerInOurWorld, bool guestShares, net::RelaySession& session, const LogFunction& log)
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
            log("phone: the NightCoopPhone script service looks different than expected, calls stay ours");
        }
        return;
    }
    auto service = game::ScriptService("NightCoopPhone");
    if (!service)
    {
        return;
    }
    int32_t mode = weAreHost ? (partnerInOurWorld ? ModeHost : ModeOff) : (guestShares ? ModeGuest : ModeOff);
    if (mode != Mode)
    {
        Mode = mode;
        log(mode == ModeHost ? "phone: our calls go to the partners in our world" : mode == ModeGuest ? "phone: we take the hosts calls" : "phone: calls are ours");
    }
    RED4ext::StackArgs_t configureArgs;
    configureArgs.emplace_back(nullptr, &mode);
    game::CallMethod(service.instance, Script.Configure, nullptr, configureArgs);
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> calls;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(service.instance, Script.TakeOutgoing, &calls, noArgs))
    {
        return;
    }
    for (auto& callObject : calls)
    {
        if (!callObject)
        {
            continue;
        }
        net::PhoneSync phone;
        phone.Action = net::kPhoneActionCall;
        phone.Contact = Field<RED4ext::CName>(callObject.instance, "contact").ToString();
        phone.CallPhase = static_cast<uint8_t>(Field<int32_t>(callObject.instance, "callPhase"));
        phone.CallMode = static_cast<uint8_t>(Field<int32_t>(callObject.instance, "callMode"));
        phone.Visuals = static_cast<uint8_t>(Field<int32_t>(callObject.instance, "visuals"));
        phone.Flags = static_cast<uint8_t>(Field<int32_t>(callObject.instance, "flags"));
        if (!net::PhoneContactNameOk(phone.Contact))
        {
            continue;
        }
        const bool byPlayer = (phone.Flags & net::kPhonePlayerCalling) != 0;
        if (phone.Contact != CallContact || byPlayer != CallByPlayer || CallId == 0)
        {
            ++CallId;
            CallContact = phone.Contact;
            CallByPlayer = byPlayer;
        }
        phone.CallId = CallId;
        phone.HostClockMs = session.SessionClockMs();
        if (Mode == ModeHost)
        {
            phone.Kind = net::kPhoneCommitted;
            phone.CommitNumber = NextCommit++;
            session.SendGameMessage(net::WritePhoneSync(phone));
            log("phone: call with " + phone.Contact + " phase " + std::to_string(phone.CallPhase) + " goes to evryone (#" + std::to_string(phone.CommitNumber) + ")");
        }
        else if (Mode == ModeGuest)
        {
            phone.Kind = net::kPhoneRequest;
            phone.RequestNumber = NextRequest++;
            phone.AskingPeer = session.GetStatus().MyPeerNumber;
            session.SendGameMessage(net::WritePhoneSync(phone), 0);
            log("phone: we dialed " + phone.Contact + ", the host makes the call");
        }
    }
    if (nowMs >= NextReportMs && Mode != ModeOff)
    {
        NextReportMs = nowMs + ReportEveryMs;
        RED4ext::CString text;
        if (game::CallMethod(service.instance, Script.Describe, &text, noArgs))
        {
            log(std::string("phone: ") + text.c_str());
        }
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool guestShares, net::RelaySession& session, const LogFunction& log)
{
    net::PhoneSync phone;
    if (!Ready() || !net::ReadPhoneSync(message, phone))
    {
        return;
    }
    if (weAreHost && phone.Kind == net::kPhoneRequest)
    {
        if (Mode != ModeHost || !Play(phone))
        {
            phone.Kind = net::kPhoneRefused;
            phone.AskingPeer = fromPeer;
            session.SendGameMessage(net::WritePhoneSync(phone), static_cast<int32_t>(fromPeer));
            log("phone: peer " + std::to_string(fromPeer) + " wanted to call " + phone.Contact + ", refused");
            return;
        }
        log("phone: peer " + std::to_string(fromPeer) + " calls " + phone.Contact + " thru our phone");
        return;
    }
    if (weAreHost || fromPeer != 0 || !guestShares)
    {
        return;
    }
    if (phone.Kind == net::kPhoneCommitted && PlayedCommits.insert(phone.CommitNumber).second)
    {
        const bool played = Play(phone);
        log("phone: the hosts call with " + phone.Contact + " phase " + std::to_string(phone.CallPhase) + (played ? " plays here" : " could not play here") +
            " (#" + std::to_string(phone.CommitNumber) + ")");
    }
    else if (phone.Kind == net::kPhoneRefused)
    {
        log("phone: the host refused our call to " + phone.Contact);
    }
}
}
