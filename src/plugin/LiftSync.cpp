#include "plugin/LiftSync.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/WorldMessages.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <cstdio>
#include <map>

namespace nightcoop_mac::lift_sync
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t DepartureLeadMs = 150;
constexpr int32_t StartedHere = 0;
constexpr int32_t NoSuchLift = 6;
constexpr uint8_t ResultGoes = 1;
constexpr uint8_t InStepDeparture = 0x40;

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Configure = nullptr;
    RED4ext::CBaseFunction* ClearOutgoing = nullptr;
    RED4ext::CBaseFunction* StartLift = nullptr;
    RED4ext::CProperty* RequestLifts = nullptr;
    RED4ext::CProperty* RequestFloors = nullptr;
    RED4ext::CProperty* DepartureLifts = nullptr;
    RED4ext::CProperty* DepartureFloors = nullptr;
};

Calls Game;
uint64_t NextTickMs = 0;
uint32_t DepartureCounter = 0;
uint16_t RequestCounter = 0;
std::map<uint16_t, std::pair<uint64_t, int32_t>> OpenRequests;

RED4ext::CProperty* ArrayProperty(RED4ext::CClass* owner, const char* name, const char* typeName)
{
    auto* property = owner ? owner->GetProperty(name) : nullptr;
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName ? property : nullptr;
}

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* syncClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopLiftSync");
    Game.Configure = game::FindFunction("NightCoopLiftSync", "Configure");
    Game.ClearOutgoing = game::FindFunction("NightCoopLiftSync", "ClearOutgoing");
    Game.StartLift = game::FindFunction("NightCoopLiftSync", "StartLift");
    Game.RequestLifts = ArrayProperty(syncClass, "requestLifts", "array:entEntityID");
    Game.RequestFloors = ArrayProperty(syncClass, "requestFloors", "array:Int32");
    Game.DepartureLifts = ArrayProperty(syncClass, "departureLifts", "array:entEntityID");
    Game.DepartureFloors = ArrayProperty(syncClass, "departureFloors", "array:Int32");
    Game.Usable = game::ParamsAre(Game.Configure, {"Bool", "Bool"}) && game::ParamsAre(Game.ClearOutgoing, {}) &&
                  game::ParamsAre(Game.StartLift, {"entEntityID", "Int32"}) && game::ReturnIs(Game.StartLift, "Int32") && Game.RequestLifts &&
                  Game.RequestFloors && Game.DepartureLifts && Game.DepartureFloors;
    log(Game.Usable ? "lifts: one cab mode, the host owns evry lift and guests ask it" : "lifts: no NightCoopLiftSync script service, evry player keeps their own lifts");
    return Game.Usable;
}

std::string KeyText(uint64_t key)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(key));
    return text;
}

int32_t StartLift(uint64_t key, int32_t floor)
{
    auto service = game::ScriptService("NightCoopLiftSync");
    int32_t result = NoSuchLift;
    if (!service)
    {
        return result;
    }
    RED4ext::ent::EntityID lift(key);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &lift);
    args.emplace_back(nullptr, &floor);
    game::CallMethod(service.instance, Game.StartLift, &result, args);
    return result;
}
}

void Tick(uint64_t nowMs, bool inSave, bool partnersHere, net::RelaySession& session, const LogFunction& log)
{
    if (nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    const auto status = session.GetStatus();
    if (!inSave || !status.InSession || !Resolve(log))
    {
        return;
    }
    auto service = game::ScriptService("NightCoopLiftSync");
    if (!service)
    {
        return;
    }
    bool oneCab = partnersHere;
    bool isHost = status.MyPeerNumber == 0;
    RED4ext::StackArgs_t configureArgs;
    configureArgs.emplace_back(nullptr, &oneCab);
    configureArgs.emplace_back(nullptr, &isHost);
    game::CallMethod(service.instance, Game.Configure, nullptr, configureArgs);

    const auto* requestLifts = Game.RequestLifts->GetValuePtr<RED4ext::DynArray<uint64_t>>(service.instance);
    const auto* requestFloors = Game.RequestFloors->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    const auto* departureLifts = Game.DepartureLifts->GetValuePtr<RED4ext::DynArray<uint64_t>>(service.instance);
    const auto* departureFloors = Game.DepartureFloors->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    if (requestLifts->Size() == 0 && departureLifts->Size() == 0)
    {
        return;
    }
    for (uint32_t index = 0; index < departureLifts->Size() && index < departureFloors->Size(); ++index)
    {
        net::LiftSync departure;
        departure.Kind = net::kLiftDeparture;
        departure.Peer = status.MyPeerNumber;
        departure.LiftKey = (*departureLifts)[index];
        departure.FloorTo = static_cast<int8_t>((*departureFloors)[index]);
        departure.DepartureMs = session.SessionClockMs() + DepartureLeadMs;
        departure.DepartureNumber = ++DepartureCounter;
        departure.Flags = InStepDeparture;
        session.SendGameMessage(net::WriteLiftSync(departure));
        log("lifts: our lift " + KeyText(departure.LiftKey) + " leaves for floor " + std::to_string(departure.FloorTo) + " (departure " +
            std::to_string(departure.DepartureNumber) + ")");
    }
    for (uint32_t index = 0; index < requestLifts->Size() && index < requestFloors->Size(); ++index)
    {
        net::LiftSync request;
        request.Kind = net::kLiftRequest;
        request.Peer = status.MyPeerNumber;
        request.LiftKey = (*requestLifts)[index];
        request.FloorTo = static_cast<int8_t>((*requestFloors)[index]);
        if (++RequestCounter == 0)
        {
            ++RequestCounter;
        }
        request.RequestNumber = RequestCounter;
        request.RequestKind = 1;
        OpenRequests[request.RequestNumber] = {request.LiftKey, request.FloorTo};
        session.SendGameMessage(net::WriteLiftSync(request), 0);
        log("lifts: asked the host to send lift " + KeyText(request.LiftKey) + " to floor " + std::to_string(request.FloorTo));
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, Game.ClearOutgoing, nullptr, noArgs);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    net::LiftSync lift;
    if (!Game.Usable || net::MessageTypeOf(message) != static_cast<uint8_t>(nc::proto::MsgType::LiftSync) || !net::ReadLiftSync(message, lift))
    {
        return;
    }
    const bool weAreHost = session.GetStatus().MyPeerNumber == 0;
    if (weAreHost && lift.Kind == net::kLiftRequest)
    {
        const int32_t started = StartLift(lift.LiftKey, lift.FloorTo);
        net::LiftSync reply;
        reply.Kind = net::kLiftReply;
        reply.LiftKey = lift.LiftKey;
        reply.FloorTo = lift.FloorTo;
        reply.RequestNumber = lift.RequestNumber;
        reply.Result = started == StartedHere ? ResultGoes : static_cast<uint8_t>(started);
        session.SendGameMessage(net::WriteLiftSync(reply), static_cast<int32_t>(fromPeer));
        log("lifts: peer " + std::to_string(fromPeer) + " asked for lift " + KeyText(lift.LiftKey) + " to floor " + std::to_string(lift.FloorTo) +
            ", answer " + std::to_string(reply.Result));
    }
    else if (!weAreHost && fromPeer == 0 && lift.Kind == net::kLiftDeparture)
    {
        const int32_t started = StartLift(lift.LiftKey, lift.FloorTo);
        log("lifts: the host lift " + KeyText(lift.LiftKey) + " leaves for floor " + std::to_string(lift.FloorTo) +
            (started == StartedHere ? ", ours goes along" : started == NoSuchLift ? ", we dont have that lift here" : ", ours did not start"));
    }
    else if (!weAreHost && fromPeer == 0 && lift.Kind == net::kLiftReply)
    {
        const auto asked = OpenRequests.find(lift.RequestNumber);
        if (asked == OpenRequests.end())
        {
            return;
        }
        if (lift.Result == NoSuchLift)
        {
            StartLift(asked->second.first, asked->second.second);
            log("lifts: the host has no lift " + KeyText(asked->second.first) + ", ours goes by itself");
        }
        else
        {
            log("lifts: the host answered " + std::to_string(lift.Result) + " for lift " + KeyText(asked->second.first));
        }
        OpenRequests.erase(asked);
    }
}
}
