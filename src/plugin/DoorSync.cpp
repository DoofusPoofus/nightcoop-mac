#include "plugin/DoorSync.hpp"

#include "net/DoorMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/PartnerRoster.hpp"
#include "plugin/RemoteBodies.hpp"
#include "plugin/WorldHandoff.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <random>
#include <set>

namespace nightcoop_mac::door_sync
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t TouchedPollEveryMs = 500;
constexpr uint64_t QuietPollEveryMs = 1000;
constexpr uint64_t SoonPollMs = 200;
constexpr uint64_t TouchedForMs = 300000;
constexpr uint32_t MostPollsPerTick = 48;
constexpr size_t MostTrackedDoors = 2048;
constexpr uint64_t CauseWindowMs = 3000;
constexpr uint64_t HostQuestMemoryMs = 120000;
constexpr uint64_t AnswerWaitMs = 5000;
constexpr uint64_t BusyWaitMs = 3000;
constexpr uint32_t MostApplyTries = 6;
constexpr uint64_t ForgetSettledAfterMs = 30ull * 60 * 1000;
constexpr uint64_t ForgetCheckEveryMs = 10000;
constexpr uint64_t LogBudgetWindowMs = 10000;
constexpr uint32_t MostLinesPerWindow = 40;
constexpr int32_t DoorNotHere = -1;
constexpr int32_t DoorIsLiftDoor = -2;
constexpr int32_t DoorIsBusy = -3;
constexpr int32_t MovedHere = 0;
constexpr int32_t SeenClosedByItself = 1;
constexpr int32_t SeenQuestAction = 2;
constexpr uint8_t HighestQuestOp = net::kDoorOpOtherQuestAction;

enum class HostAnswer : uint8_t
{
    Taken = 0,
    AlreadySo = 1,
    HostQuestDidItAlready = 2,
    RefusedSealed = 3,
    RefusedLocked = 4,
    KeptOpenForSomeone = 5,
};

struct TrackedDoor
{
    bool LocalKnown = false;
    uint8_t LocalState = 0;
    bool SharedKnown = false;
    uint8_t SharedState = 0;
    uint64_t Version = 0;
    bool LiftDoor = false;
    bool NotHere = false;
    uint64_t TouchedMs = 0;
    uint64_t NextPollMs = 0;
    uint64_t ClosedByItselfSeenMs = 0;
    uint16_t QuestOpsSeen = 0;
    uint64_t QuestOpsSeenMs = 0;
    uint64_t OurQuestOpMs[HighestQuestOp + 1] = {};
    uint16_t OpenRequest = 0;
    uint64_t WaitingForAnswerUntilMs = 0;
    uint32_t ApplyTries = 0;
    uint64_t BusySinceMs = 0;
};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Configure = nullptr;
    RED4ext::CBaseFunction* ClearOutgoing = nullptr;
    RED4ext::CBaseFunction* ClearPartners = nullptr;
    RED4ext::CBaseFunction* AddPartner = nullptr;
    RED4ext::CBaseFunction* ReadDoor = nullptr;
    RED4ext::CBaseFunction* ApplyDoor = nullptr;
    RED4ext::CBaseFunction* OurSide = nullptr;
    RED4ext::CBaseFunction* AnybodyAt = nullptr;
    RED4ext::CBaseFunction* ServeHeld = nullptr;
    RED4ext::CBaseFunction* HeldCount = nullptr;
    RED4ext::CProperty* SeenDoors = nullptr;
    RED4ext::CProperty* SeenBefore = nullptr;
    RED4ext::CProperty* SeenCause = nullptr;
    RED4ext::CProperty* SeenQuestOp = nullptr;
};

Calls Game;
std::map<uint64_t, TrackedDoor> Doors;
std::set<uint32_t> PeersInOurWorld;
bool SharedNow = false;
bool WeAreHost = false;
bool TooManyDoorsSaid = false;
uint64_t NextTickMs = 0;
uint64_t NextForgetMs = 0;
uint64_t LastPolledKey = 0;
uint16_t RequestCounter = static_cast<uint16_t>(std::random_device {}());
float OurPosition[3] = {0, 0, 0};
int32_t HeldReported = 0;
uint64_t LogWindowStartMs = 0;
uint32_t LinesThisWindow = 0;
uint32_t LinesSkipped = 0;

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
    auto* syncClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopDoorSync");
    Game.Configure = game::FindFunction("NightCoopDoorSync", "Configure");
    Game.ClearOutgoing = game::FindFunction("NightCoopDoorSync", "ClearOutgoing");
    Game.ClearPartners = game::FindFunction("NightCoopDoorSync", "ClearPartners");
    Game.AddPartner = game::FindFunction("NightCoopDoorSync", "AddPartner");
    Game.ReadDoor = game::FindFunction("NightCoopDoorSync", "ReadDoor");
    Game.ApplyDoor = game::FindFunction("NightCoopDoorSync", "ApplyDoor");
    Game.OurSide = game::FindFunction("NightCoopDoorSync", "OurSide");
    Game.AnybodyAt = game::FindFunction("NightCoopDoorSync", "AnybodyAt");
    Game.ServeHeld = game::FindFunction("NightCoopDoorSync", "ServeHeld");
    Game.HeldCount = game::FindFunction("NightCoopDoorSync", "HeldCount");
    Game.SeenDoors = ArrayProperty(syncClass, "seenDoors", "array:entEntityID");
    Game.SeenBefore = ArrayProperty(syncClass, "seenBefore", "array:Int32");
    Game.SeenCause = ArrayProperty(syncClass, "seenCause", "array:Int32");
    Game.SeenQuestOp = ArrayProperty(syncClass, "seenQuestOp", "array:Int32");
    Game.Usable = game::ParamsAre(Game.Configure, {"Bool"}) && game::ParamsAre(Game.ClearOutgoing, {}) && game::ParamsAre(Game.ClearPartners, {}) &&
                  game::ParamsAre(Game.AddPartner, {"Int32", "Float", "Float", "Float"}) && game::ParamsAre(Game.ReadDoor, {"entEntityID"}) &&
                  game::ReturnIs(Game.ReadDoor, "Int32") && game::ParamsAre(Game.ApplyDoor, {"entEntityID", "Int32", "Bool"}) &&
                  game::ReturnIs(Game.ApplyDoor, "Int32") && game::ParamsAre(Game.OurSide, {"entEntityID"}) && game::ReturnIs(Game.OurSide, "Int32") &&
                  game::ParamsAre(Game.AnybodyAt, {"entEntityID", "Int32"}) && game::ReturnIs(Game.AnybodyAt, "Bool") &&
                  game::ParamsAre(Game.ServeHeld, {}) && game::ReturnIs(Game.HeldCount, "Int32") && Game.SeenDoors && Game.SeenBefore &&
                  Game.SeenCause && Game.SeenQuestOp;
    log(Game.Usable ? "doors: the host keeps evry door, copies follow it" : "doors: no NightCoopDoorSync script service, evry player keeps their own doors");
    return Game.Usable;
}

void Say(const std::string& line, uint64_t nowMs, const LogFunction& log)
{
    if (nowMs - LogWindowStartMs >= LogBudgetWindowMs)
    {
        if (LinesSkipped)
        {
            log("doors: " + std::to_string(LinesSkipped) + " more door line(s) left out");
        }
        LogWindowStartMs = nowMs;
        LinesThisWindow = 0;
        LinesSkipped = 0;
    }
    if (LinesThisWindow++ < MostLinesPerWindow)
    {
        log(line);
    }
    else
    {
        ++LinesSkipped;
    }
}

std::string KeyText(uint64_t key)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(key));
    return text;
}

std::string StateText(uint8_t state)
{
    std::string text = state & net::kDoorOpen ? "open" : "closed";
    if (state & net::kDoorLocked)
    {
        text += " locked";
    }
    if (state & net::kDoorSealed)
    {
        text += " sealed";
    }
    return text;
}

std::string CauseText(uint8_t cause)
{
    switch (cause)
    {
    case net::kDoorCauseClosedByItself:
        return "closed by itself";
    case net::kDoorCauseQuest:
        return "the quest";
    case net::kDoorCauseKeptOpen:
        return "kept open for a player at it";
    default:
        return "a player or an npc";
    }
}

std::string AnswerText(HostAnswer answer)
{
    switch (answer)
    {
    case HostAnswer::Taken:
        return "taken";
    case HostAnswer::AlreadySo:
        return "already so";
    case HostAnswer::HostQuestDidItAlready:
        return "the hosts quest did the same already";
    case HostAnswer::RefusedSealed:
        return "refused: it is sealed here (only a quest unseals it)";
    case HostAnswer::RefusedLocked:
        return "refused: it is locked here and was opened without unlocking";
    default:
        return "it stays open for someone at it";
    }
}

uint8_t BitsTouchedByQuest(uint16_t questOps)
{
    uint8_t touched = 0;
    if (questOps & ((1 << net::kDoorOpForceOpen) | (1 << net::kDoorOpForceOpenScene)))
    {
        touched |= net::kDoorStateBits;
    }
    if (questOps & ((1 << net::kDoorOpForceClose) | (1 << net::kDoorOpForceCloseImmediate) | (1 << net::kDoorOpForceCloseScene) |
                    (1 << net::kDoorOpForceSeal)))
    {
        touched |= net::kDoorOpen | net::kDoorSealed;
    }
    if (questOps & ((1 << net::kDoorOpForceLock) | (1 << net::kDoorOpForceUnlock)))
    {
        touched |= net::kDoorLocked | net::kDoorSealed;
    }
    if (questOps & (1 << net::kDoorOpForceUnseal))
    {
        touched |= net::kDoorSealed;
    }
    return touched;
}

uint8_t ChangedMaskFor(uint8_t changed, uint8_t cause, uint16_t questOps)
{
    if (cause == net::kDoorCauseQuest)
    {
        changed |= BitsTouchedByQuest(questOps);
    }
    if (cause == net::kDoorCauseClosedByItself || cause == net::kDoorCauseKeptOpen)
    {
        changed &= net::kDoorOpen;
    }
    return changed & net::kDoorStateBits;
}

uint64_t NextVersion(uint64_t version, net::RelaySession& session)
{
    return std::max(version + 1, session.SessionClockMs());
}

uint16_t NextRequestNumber()
{
    if (++RequestCounter == 0)
    {
        ++RequestCounter;
    }
    return RequestCounter;
}

TrackedDoor* Track(uint64_t key, uint64_t nowMs, const LogFunction& log)
{
    if (key == 0)
    {
        return nullptr;
    }
    auto found = Doors.find(key);
    if (found != Doors.end())
    {
        return &found->second;
    }
    if (Doors.size() >= MostTrackedDoors)
    {
        if (!TooManyDoorsSaid)
        {
            TooManyDoorsSaid = true;
            log("doors: " + std::to_string(MostTrackedDoors) + " doors tracked already, new ones are left alone for a while");
        }
        return nullptr;
    }
    auto& door = Doors[key];
    door.TouchedMs = nowMs;
    door.NextPollMs = nowMs;
    return &door;
}

int32_t ReadDoor(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t key)
{
    RED4ext::ent::EntityID door(key);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &door);
    int32_t state = DoorNotHere;
    game::CallMethod(service.instance, Game.ReadDoor, &state, args);
    return state;
}

int32_t ApplyDoor(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t key, uint8_t wanted, bool evenWhenBusy)
{
    RED4ext::ent::EntityID door(key);
    int32_t wantedState = wanted;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &door);
    args.emplace_back(nullptr, &wantedState);
    args.emplace_back(nullptr, &evenWhenBusy);
    int32_t result = DoorNotHere;
    game::CallMethod(service.instance, Game.ApplyDoor, &result, args);
    return result;
}

uint8_t OurSide(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t key)
{
    RED4ext::ent::EntityID door(key);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &door);
    int32_t side = 0;
    game::CallMethod(service.instance, Game.OurSide, &side, args);
    return side >= 0 && side <= 2 ? static_cast<uint8_t>(side) : 0;
}

bool AnybodyAt(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t key, uint32_t exceptPeer)
{
    RED4ext::ent::EntityID door(key);
    int32_t except = static_cast<int32_t>(exceptPeer);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &door);
    args.emplace_back(nullptr, &except);
    bool somebody = false;
    game::CallMethod(service.instance, Game.AnybodyAt, &somebody, args);
    return somebody;
}

void CallPlain(const RED4ext::Handle<RED4ext::IScriptable>& service, RED4ext::CBaseFunction* function)
{
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, function, nullptr, noArgs);
}

void Configure(const RED4ext::Handle<RED4ext::IScriptable>& service, bool shared)
{
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &shared);
    game::CallMethod(service.instance, Game.Configure, nullptr, args);
}

void HandPartnersOver(const RED4ext::Handle<RED4ext::IScriptable>& service, const std::vector<RemotePeerView>& partners)
{
    CallPlain(service, Game.ClearPartners);
    for (const auto& partner : partners)
    {
        if (WeAreHost && !PeersInOurWorld.count(partner.PeerNumber))
        {
            continue;
        }
        int32_t peer = static_cast<int32_t>(partner.PeerNumber);
        float x = partner.State.PositionX;
        float y = partner.State.PositionY;
        float z = partner.State.PositionZ;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &peer);
        args.emplace_back(nullptr, &x);
        args.emplace_back(nullptr, &y);
        args.emplace_back(nullptr, &z);
        game::CallMethod(service.instance, Game.AddPartner, nullptr, args);
    }
}

void SendShared(uint64_t key, const TrackedDoor& door, uint8_t changed, uint8_t cause, uint16_t questOps, uint32_t actor, uint8_t side,
                const float* position, uint32_t asker, uint16_t askerRequest, bool onlyAsker, net::RelaySession& session)
{
    net::DoorSync shared;
    shared.Kind = net::kDoorShared;
    shared.Peer = actor;
    shared.DoorKey = key;
    shared.State = door.SharedState;
    shared.Changed = changed & net::kDoorStateBits;
    shared.Cause = questOps ? net::kDoorCauseQuest : (cause == net::kDoorCauseQuest ? net::kDoorCausePlayerOrNpc : cause);
    shared.QuestOps = shared.Cause == net::kDoorCauseQuest ? questOps : 0;
    shared.Side = side;
    shared.Version = door.Version;
    std::copy(position, position + 3, shared.Position);
    if (onlyAsker)
    {
        shared.RequestNumber = askerRequest;
        session.SendGameMessage(net::WriteDoorSync(shared), static_cast<int32_t>(asker));
        return;
    }
    for (uint32_t peer : PeersInOurWorld)
    {
        shared.RequestNumber = peer == asker ? askerRequest : 0;
        session.SendGameMessage(net::WriteDoorSync(shared), static_cast<int32_t>(peer));
    }
}

void TakeSeenDoors(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t nowMs, const LogFunction& log)
{
    const auto* seenDoors = Game.SeenDoors->GetValuePtr<RED4ext::DynArray<uint64_t>>(service.instance);
    const auto* seenBefore = Game.SeenBefore->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    const auto* seenCause = Game.SeenCause->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    const auto* seenQuestOp = Game.SeenQuestOp->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    if (seenDoors->Size() == 0)
    {
        return;
    }
    const uint32_t count = std::min({seenDoors->Size(), seenBefore->Size(), seenCause->Size(), seenQuestOp->Size()});
    for (uint32_t index = 0; index < count; ++index)
    {
        auto* door = Track((*seenDoors)[index], nowMs, log);
        if (!door)
        {
            continue;
        }
        door->TouchedMs = nowMs;
        door->NextPollMs = std::min(door->NextPollMs, nowMs + SoonPollMs);
        const int32_t before = (*seenBefore)[index];
        if (!door->LocalKnown && before >= 0)
        {
            door->LocalKnown = true;
            door->LocalState = static_cast<uint8_t>(before) & net::kDoorStateBits;
            if (WeAreHost && !door->SharedKnown)
            {
                door->SharedKnown = true;
                door->SharedState = door->LocalState;
            }
        }
        const int32_t cause = (*seenCause)[index];
        const int32_t questOp = (*seenQuestOp)[index];
        if (cause == SeenClosedByItself)
        {
            door->ClosedByItselfSeenMs = nowMs;
        }
        else if (cause == SeenQuestAction && questOp >= 1 && questOp <= HighestQuestOp)
        {
            if (nowMs - door->QuestOpsSeenMs > CauseWindowMs)
            {
                door->QuestOpsSeen = 0;
            }
            door->QuestOpsSeen |= static_cast<uint16_t>(1u << questOp);
            door->QuestOpsSeenMs = nowMs;
            door->OurQuestOpMs[questOp] = nowMs;
        }
    }
    CallPlain(service, Game.ClearOutgoing);
}

void PickCause(TrackedDoor& door, uint8_t changed, uint8_t nowState, uint64_t nowMs, uint8_t& cause, uint16_t& questOps)
{
    cause = net::kDoorCausePlayerOrNpc;
    questOps = 0;
    if (door.QuestOpsSeen && nowMs - door.QuestOpsSeenMs <= CauseWindowMs)
    {
        cause = net::kDoorCauseQuest;
        questOps = door.QuestOpsSeen;
        door.QuestOpsSeen = 0;
        return;
    }
    if (door.ClosedByItselfSeenMs && nowMs - door.ClosedByItselfSeenMs <= CauseWindowMs && (changed & net::kDoorOpen) && !(nowState & net::kDoorOpen))
    {
        cause = net::kDoorCauseClosedByItself;
        door.ClosedByItselfSeenMs = 0;
    }
}

void OnLocalChange(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t key, TrackedDoor& door, uint8_t changed, uint8_t nowState,
                   uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    uint8_t cause = 0;
    uint16_t questOps = 0;
    PickCause(door, changed, nowState, nowMs, cause, questOps);
    const uint8_t changedMask = ChangedMaskFor(changed, cause, questOps);
    const uint8_t side = OurSide(service, key);
    char where[64];
    std::snprintf(where, sizeof(where), "at %.1f %.1f %.1f", OurPosition[0], OurPosition[1], OurPosition[2]);
    if (WeAreHost)
    {
        door.SharedKnown = true;
        door.SharedState = nowState;
        door.Version = NextVersion(door.Version, session);
        door.ApplyTries = 0;
        SendShared(key, door, changedMask, cause, questOps, 0, side, OurPosition, 0, 0, false, session);
        Say("doors: our door " + KeyText(key) + " is " + StateText(nowState) + " (" + CauseText(cause) + ", we are " + where + "), " +
                std::to_string(PeersInOurWorld.size()) + " guest(s) get it (version " + std::to_string(door.Version) + ")",
            nowMs, log);
        return;
    }
    if (changedMask == 0)
    {
        return;
    }
    net::DoorSync tell;
    tell.Kind = net::kDoorTell;
    tell.Peer = session.GetStatus().MyPeerNumber;
    tell.DoorKey = key;
    tell.State = nowState;
    tell.Changed = changedMask;
    tell.Cause = cause;
    tell.QuestOps = questOps;
    tell.Side = side;
    tell.Version = door.Version;
    tell.RequestNumber = NextRequestNumber();
    std::copy(OurPosition, OurPosition + 3, tell.Position);
    door.OpenRequest = tell.RequestNumber;
    door.WaitingForAnswerUntilMs = nowMs + AnswerWaitMs;
    session.SendGameMessage(net::WriteDoorSync(tell), 0);
    Say("doors: our door " + KeyText(key) + " is " + StateText(nowState) + " (" + CauseText(cause) + ", we are " + where + "), told the host (#" +
            std::to_string(tell.RequestNumber) + ", from its version " + std::to_string(door.Version) + ")",
        nowMs, log);
}

void MoveTowardsShared(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t key, TrackedDoor& door, uint64_t nowMs, const LogFunction& log)
{
    if (door.ApplyTries >= MostApplyTries || nowMs < door.WaitingForAnswerUntilMs)
    {
        return;
    }
    const bool evenWhenBusy = door.BusySinceMs && nowMs - door.BusySinceMs >= BusyWaitMs;
    const int32_t result = ApplyDoor(service, key, door.SharedState, evenWhenBusy);
    if (result == DoorIsBusy)
    {
        if (!door.BusySinceMs)
        {
            door.BusySinceMs = nowMs;
        }
        door.NextPollMs = std::min(door.NextPollMs, nowMs + SoonPollMs);
        return;
    }
    door.BusySinceMs = 0;
    if (result == DoorIsLiftDoor)
    {
        door.LiftDoor = true;
        return;
    }
    if (result == DoorNotHere)
    {
        door.NotHere = true;
        return;
    }
    if (result != MovedHere)
    {
        return;
    }
    door.NextPollMs = std::min(door.NextPollMs, nowMs + SoonPollMs + TickEveryMs);
    if (++door.ApplyTries == MostApplyTries)
    {
        Say("doors: " + KeyText(key) + " " + StateText(door.SharedState) + " wanted, " + StateText(door.LocalState) + " here after " +
                std::to_string(MostApplyTries) + " tries (disabled or unpowered here?)",
            nowMs, log);
    }
}

void PollDoor(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t key, TrackedDoor& door, uint64_t nowMs, net::RelaySession& session,
              const LogFunction& log)
{
    door.NextPollMs = nowMs + (nowMs - door.TouchedMs < TouchedForMs ? TouchedPollEveryMs : QuietPollEveryMs);
    const int32_t read = ReadDoor(service, key);
    if (read == DoorIsLiftDoor)
    {
        door.LiftDoor = true;
        return;
    }
    if (read < 0)
    {
        door.NotHere = true;
        return;
    }
    door.NotHere = false;
    const uint8_t nowState = static_cast<uint8_t>(read) & net::kDoorStateBits;
    if (!door.LocalKnown)
    {
        door.LocalKnown = true;
        door.LocalState = nowState;
        if (WeAreHost && !door.SharedKnown)
        {
            door.SharedKnown = true;
            door.SharedState = nowState;
            door.Version = NextVersion(door.Version, session);
        }
    }
    if (WeAreHost && door.Version == 0)
    {
        door.Version = NextVersion(0, session);
    }
    if (nowState != door.LocalState)
    {
        const uint8_t changed = nowState ^ door.LocalState;
        const bool onlyTowardsShared = door.SharedKnown && (changed & ~(door.LocalState ^ door.SharedState)) == 0;
        door.LocalState = nowState;
        door.TouchedMs = nowMs;
        if (!onlyTowardsShared)
        {
            OnLocalChange(service, key, door, changed, nowState, nowMs, session, log);
        }
    }
    if (!door.SharedKnown || door.LocalState == door.SharedState)
    {
        door.ApplyTries = 0;
        door.BusySinceMs = 0;
        return;
    }
    MoveTowardsShared(service, key, door, nowMs, log);
}

void PollDueDoors(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    if (Doors.empty())
    {
        return;
    }
    uint32_t polled = 0;
    auto door = Doors.upper_bound(LastPolledKey);
    for (size_t visited = 0; visited < Doors.size() && polled < MostPollsPerTick; ++visited, ++door)
    {
        if (door == Doors.end())
        {
            door = Doors.begin();
        }
        if (door->second.LiftDoor || nowMs < door->second.NextPollMs)
        {
            continue;
        }
        PollDoor(service, door->first, door->second, nowMs, session, log);
        LastPolledKey = door->first;
        ++polled;
    }
}

void ForgetSettledDoors(uint64_t nowMs)
{
    if (nowMs < NextForgetMs)
    {
        return;
    }
    NextForgetMs = nowMs + ForgetCheckEveryMs;
    std::erase_if(Doors, [nowMs](const auto& entry) {
        const auto& door = entry.second;
        const bool settled =
            !door.SharedKnown || door.NotHere || door.LiftDoor || door.LocalState == door.SharedState || door.ApplyTries >= MostApplyTries;
        return settled && nowMs - door.TouchedMs > ForgetSettledAfterMs && nowMs >= door.WaitingForAnswerUntilMs;
    });
    if (Doors.size() < MostTrackedDoors)
    {
        TooManyDoorsSaid = false;
    }
}

void ReportHeld(const RED4ext::Handle<RED4ext::IScriptable>& service, uint64_t nowMs, const LogFunction& log)
{
    RED4ext::StackArgs_t noArgs;
    int32_t held = 0;
    game::CallMethod(service.instance, Game.HeldCount, &held, noArgs);
    if (held != HeldReported)
    {
        Say(held > HeldReported ? "doors: a door stays open, a partner is at it (" + std::to_string(held) + " held)"
                                : "doors: " + std::to_string(held) + " door(s) held open for partners now",
            nowMs, log);
        HeldReported = held;
    }
}

bool WorldIsShared(bool inSave, bool shareAlways, net::RelaySession& session)
{
    PeersInOurWorld.clear();
    if (WeAreHost)
    {
        for (const auto& [peerNumber, nickname] : session.PeerNicknames())
        {
            if (shareAlways || partner_roster::PeerInHostWorld(peerNumber))
            {
                PeersInOurWorld.insert(peerNumber);
            }
        }
        return inSave && !PeersInOurWorld.empty();
    }
    PeersInOurWorld.insert(0);
    return inSave && (shareAlways || (world_handoff::InHostWorld() && partner_roster::HostInItsWorld()));
}

void StopSharing(const LogFunction& log)
{
    if (SharedNow)
    {
        log("doors: the world is not shared now, our doors are ours (" + std::to_string(Doors.size()) + " forgotten)");
    }
    SharedNow = false;
    Doors.clear();
    HeldReported = 0;
    if (Game.Usable)
    {
        if (auto service = game::ScriptService("NightCoopDoorSync"))
        {
            Configure(service, false);
        }
    }
}
}

void Tick(uint64_t nowMs, bool inSave, bool shareAlways, const net::PlayerState* ourState, const std::vector<RemotePeerView>& partners,
          net::RelaySession& session, const LogFunction& log)
{
    if (nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    const auto status = session.GetStatus();
    if (!status.InSession || !Resolve(log))
    {
        if (SharedNow)
        {
            StopSharing(log);
        }
        return;
    }
    WeAreHost = status.MyPeerNumber == 0;
    if (!WorldIsShared(inSave, shareAlways, session))
    {
        if (SharedNow)
        {
            StopSharing(log);
        }
        return;
    }
    auto service = game::ScriptService("NightCoopDoorSync");
    if (!service)
    {
        return;
    }
    if (!SharedNow)
    {
        SharedNow = true;
        log(WeAreHost ? "doors: the world is shared, we keep evry door for " + std::to_string(PeersInOurWorld.size()) + " guest(s)"
                      : "doors: the world is shared, the host keeps evry door");
    }
    if (ourState)
    {
        OurPosition[0] = ourState->PositionX;
        OurPosition[1] = ourState->PositionY;
        OurPosition[2] = ourState->PositionZ;
    }
    Configure(service, true);
    HandPartnersOver(service, partners);
    TakeSeenDoors(service, nowMs, log);
    CallPlain(service, Game.ServeHeld);
    ReportHeld(service, nowMs, log);
    PollDueDoors(service, nowMs, session, log);
    ForgetSettledDoors(nowMs);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    net::DoorSync door;
    if (!Game.Usable || !SharedNow || net::MessageTypeOf(message) != static_cast<uint8_t>(nc::proto::MsgType::DoorSync) ||
        !net::ReadDoorSync(message, door))
    {
        return;
    }
    if (!WeAreHost && fromPeer == 0 && door.Kind == net::kDoorShared)
    {
        auto* tracked = Track(door.DoorKey, nowMs, log);
        if (!tracked)
        {
            return;
        }
        const bool answerToUs = door.RequestNumber != 0 && door.RequestNumber == tracked->OpenRequest;
        if (!answerToUs && door.Version <= tracked->Version)
        {
            return;
        }
        if (answerToUs)
        {
            tracked->OpenRequest = 0;
            tracked->WaitingForAnswerUntilMs = 0;
        }
        tracked->SharedKnown = true;
        tracked->SharedState = door.State;
        tracked->Version = std::max(tracked->Version, door.Version);
        tracked->ApplyTries = 0;
        tracked->TouchedMs = nowMs;
        tracked->NextPollMs = nowMs;
        Say("doors: the host says " + KeyText(door.DoorKey) + " is " + StateText(door.State) + " (" + CauseText(door.Cause) + ", peer " +
                std::to_string(door.Peer) + (answerToUs ? ", the answer to our #" + std::to_string(door.RequestNumber) : std::string()) + ", version " +
                std::to_string(door.Version) + ")",
            nowMs, log);
        return;
    }
    if (!WeAreHost || door.Kind != net::kDoorTell || door.Peer != fromPeer || fromPeer == 0 || !PeersInOurWorld.count(fromPeer))
    {
        return;
    }
    auto service = game::ScriptService("NightCoopDoorSync");
    auto* tracked = Track(door.DoorKey, nowMs, log);
    if (!service || !tracked)
    {
        return;
    }
    tracked->TouchedMs = nowMs;
    if (!tracked->LiftDoor && !tracked->LocalKnown)
    {
        const int32_t read = ReadDoor(service, door.DoorKey);
        if (read == DoorIsLiftDoor)
        {
            tracked->LiftDoor = true;
        }
        else if (read >= 0)
        {
            tracked->LocalKnown = true;
            tracked->LocalState = static_cast<uint8_t>(read) & net::kDoorStateBits;
            if (!tracked->SharedKnown)
            {
                tracked->SharedKnown = true;
                tracked->SharedState = tracked->LocalState;
                tracked->Version = NextVersion(tracked->Version, session);
            }
        }
    }
    if (tracked->LiftDoor)
    {
        Say("doors: peer " + std::to_string(fromPeer) + " told us about " + KeyText(door.DoorKey) + ", a lift door, the lifts keep those", nowMs, log);
        return;
    }
    if (!tracked->SharedKnown)
    {
        tracked->SharedKnown = true;
        tracked->SharedState = door.State ^ door.Changed;
        tracked->Version = NextVersion(tracked->Version, session);
    }
    const uint8_t current = tracked->SharedState;
    const uint8_t changedHere = door.Changed & (door.Cause == net::kDoorCauseClosedByItself ? net::kDoorOpen : net::kDoorStateBits);
    const uint8_t newState = static_cast<uint8_t>((door.State & changedHere) | (current & ~changedHere));
    const bool currentlyOpen = (current & net::kDoorOpen) != 0;
    const bool staysOrGetsOpen = (newState & net::kDoorOpen) != 0;
    HostAnswer answer = newState == current ? HostAnswer::AlreadySo : HostAnswer::Taken;
    if (door.Cause == net::kDoorCauseQuest)
    {
        uint16_t opsLeft = door.QuestOps;
        for (uint8_t op = 1; op <= HighestQuestOp; ++op)
        {
            const uint64_t ourQuestDidIt = tracked->OurQuestOpMs[op];
            if ((opsLeft & (1u << op)) && ourQuestDidIt && nowMs - ourQuestDidIt <= HostQuestMemoryMs)
            {
                opsLeft = static_cast<uint16_t>(opsLeft & ~(1u << op));
            }
        }
        if (opsLeft == 0)
        {
            answer = HostAnswer::HostQuestDidItAlready;
        }
    }
    else if (door.Cause == net::kDoorCauseClosedByItself && currentlyOpen)
    {
        if (!staysOrGetsOpen && AnybodyAt(service, door.DoorKey, fromPeer))
        {
            answer = HostAnswer::KeptOpenForSomeone;
        }
    }
    else if (staysOrGetsOpen && !currentlyOpen)
    {
        if (newState & net::kDoorSealed)
        {
            answer = HostAnswer::RefusedSealed;
        }
        else if (newState & net::kDoorLocked)
        {
            answer = HostAnswer::RefusedLocked;
        }
    }
    if (answer == HostAnswer::Taken)
    {
        tracked->SharedState = newState;
        tracked->Version = NextVersion(tracked->Version, session);
        tracked->ApplyTries = 0;
        tracked->NextPollMs = nowMs;
        SendShared(door.DoorKey, *tracked, newState ^ current, door.Cause, door.QuestOps, fromPeer, door.Side, door.Position, fromPeer, door.RequestNumber,
                   false, session);
    }
    else
    {
        SendShared(door.DoorKey, *tracked, 0, net::kDoorCausePlayerOrNpc, 0, fromPeer, 0, OurPosition, fromPeer, door.RequestNumber, true, session);
    }
    Say("doors: peer " + std::to_string(fromPeer) + " says " + KeyText(door.DoorKey) + " is " + StateText(door.State) + " (" + CauseText(door.Cause) + ", #" +
            std::to_string(door.RequestNumber) + "): " + AnswerText(answer) + ", ours is " + StateText(tracked->SharedState) + " (version " +
            std::to_string(tracked->Version) + ")",
        nowMs, log);
}
}
