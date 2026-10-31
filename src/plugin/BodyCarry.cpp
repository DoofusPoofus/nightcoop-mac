#include "plugin/BodyCarry.hpp"

#include "net/PlayerExtrasMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/AnimTap.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/NpcCopies.hpp"
#include "plugin/NpcShare.hpp"
#include "plugin/RemoteBodies.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace nightcoop_mac::body_carry
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t KeepAliveMs = 1000;
constexpr uint64_t EndAgainAfterMs[] = {250, 750};
constexpr size_t EndRepeats = sizeof(EndAgainAfterMs) / sizeof(EndAgainAfterMs[0]);
constexpr uint64_t PlaceAfterMs = 300;
constexpr uint64_t CarrierQuietMs = 5000;
constexpr size_t MostPendingPerPeer = 32;
constexpr uint64_t RecordMask = 0xFFFFFFFFFFULL;
constexpr uint8_t FeatureFlagBits = net::kCarryBothHands | net::kCarryInstant | net::kCarryFastMode | net::kCarryFriendly | net::kCarryThrown;
const char* ServiceName = "NightCoopBodyCarry";
const char* KindNames[] = {"none", "pickup", "carry", "drop", "release", "dispose", "jump", "aim", "throw"};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* FindCarried = nullptr;
    RED4ext::CBaseFunction* Describe = nullptr;
    RED4ext::CBaseFunction* Attach = nullptr;
    RED4ext::CBaseFunction* Pose = nullptr;
    RED4ext::CBaseFunction* Detach = nullptr;
    RED4ext::CBaseFunction* Place = nullptr;
    RED4ext::CBaseFunction* HideForGood = nullptr;
    RED4ext::CProperty* CarriedId = nullptr;
    RED4ext::CProperty* CarriedInArms = nullptr;
    RED4ext::CProperty* CarryingDisposal = nullptr;
    RED4ext::CProperty* DescribedRecord = nullptr;
    RED4ext::CProperty* DescribedAppearance = nullptr;
    RED4ext::CProperty* DescribedPosition = nullptr;
    RED4ext::CProperty* DescribedIsPuppet = nullptr;
};

struct CarryFeature
{
    bool Present = false;
    uint8_t State = net::kCarryNone;
    uint8_t PickupAnimation = 0;
    uint8_t Flags = 0;
};

struct Described
{
    uint64_t Record = 0;
    uint64_t Appearance = 0;
    float Position[3] = {0, 0, 0};
    bool IsPuppet = false;
};

struct OwnCarry
{
    bool Active = false;
    uint64_t ChildHash = 0;
    uint32_t HeldCopy = 0;
    bool DisposalSeen = false;
    uint64_t LastSentMs = 0;
    net::PlayerCarry Sent;
    size_t EndsSent = EndRepeats;
    uint64_t EndSentMs = 0;
};

struct PartnerCarry
{
    std::vector<net::PlayerCarry> Pending;
    bool Known = false;
    net::PlayerCarry Latest;
    uint64_t LastHeardMs = 0;
    uint64_t NpcHash = 0;
    uint32_t HeldCopy = 0;
    bool Attached = false;
    bool Posed = false;
    uint8_t PosedKind = 0;
    uint8_t PosedAnimation = 0;
    uint8_t PosedFlags = 0;
    uint64_t PlaceAtMs = 0;
    float PlacePosition[3] = {0, 0, 0};
    bool HideWhenPlaced = false;
    bool NoBodyLogged = false;
    bool ArmsLogged = false;
};

Calls Game;
uint64_t NextTickMs = 0;
uint32_t CarryCounter = 0;
OwnCarry Own;
std::map<uint32_t, PartnerCarry> Partners;
uint32_t BadMessages = 0;
uint64_t NextBadLogMs = 0;

RED4ext::CProperty* FieldOf(RED4ext::CClass* owner, const char* name, const char* typeName)
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
    auto* carryClass = RED4ext::CRTTISystem::Get()->GetClass(ServiceName);
    Game.FindCarried = game::FindFunction(ServiceName, "FindCarried");
    Game.Describe = game::FindFunction(ServiceName, "Describe");
    Game.Attach = game::FindFunction(ServiceName, "Attach");
    Game.Pose = game::FindFunction(ServiceName, "Pose");
    Game.Detach = game::FindFunction(ServiceName, "Detach");
    Game.Place = game::FindFunction(ServiceName, "Place");
    Game.HideForGood = game::FindFunction(ServiceName, "HideForGood");
    Game.CarriedId = FieldOf(carryClass, "carriedId", "entEntityID");
    Game.CarriedInArms = FieldOf(carryClass, "carriedInArms", "Bool");
    Game.CarryingDisposal = FieldOf(carryClass, "carryingDisposal", "Bool");
    Game.DescribedRecord = FieldOf(carryClass, "describedRecord", "TweakDBID");
    Game.DescribedAppearance = FieldOf(carryClass, "describedAppearance", "CName");
    Game.DescribedPosition = FieldOf(carryClass, "describedPosition", "Vector4");
    Game.DescribedIsPuppet = FieldOf(carryClass, "describedIsPuppet", "Bool");
    Game.Usable = game::ParamsAre(Game.FindCarried, {"Bool"}) && game::ReturnIs(Game.FindCarried, "Bool") &&
                  game::ParamsAre(Game.Describe, {"entEntityID"}) && game::ReturnIs(Game.Describe, "Bool") &&
                  game::ParamsAre(Game.Attach, {"handle:gameObject", "entEntityID"}) && game::ReturnIs(Game.Attach, "Bool") &&
                  game::ParamsAre(Game.Pose, {"entEntityID", "Int32", "Int32", "Int32"}) && game::ParamsAre(Game.Detach, {"entEntityID"}) &&
                  game::ParamsAre(Game.Place, {"entEntityID", "Vector4"}) && game::ParamsAre(Game.HideForGood, {"entEntityID"}) && Game.CarriedId &&
                  Game.CarriedInArms && Game.CarryingDisposal && Game.DescribedRecord && Game.DescribedAppearance && Game.DescribedPosition &&
                  Game.DescribedIsPuppet;
    log(Game.Usable ? "carry: bodies we carry show on our body at partners, theirs on theirs here"
                    : "carry: no NightCoopBodyCarry script service (scripts/NightCoopMac not compiled?), carried bodies stay local");
    return Game.Usable;
}

const char* KindName(uint8_t kind)
{
    return kind < sizeof(KindNames) / sizeof(KindNames[0]) ? KindNames[kind] : "?";
}

std::string HexText(uint64_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(value));
    return text;
}

void CallWithEntity(RED4ext::IScriptable* service, RED4ext::CBaseFunction* function, uint64_t entityHash)
{
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    game::CallMethod(service, function, nullptr, args);
}

bool DescribeEntity(RED4ext::IScriptable* service, uint64_t entityHash, Described& described)
{
    bool known = false;
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    if (!entityHash || !game::CallMethod(service, Game.Describe, &known, args) || !known)
    {
        return false;
    }
    described.Record = Game.DescribedRecord->GetValuePtr<RED4ext::TweakDBID>(service)->value;
    described.Appearance = Game.DescribedAppearance->GetValuePtr<RED4ext::CName>(service)->hash;
    const auto* position = Game.DescribedPosition->GetValuePtr<RED4ext::Vector4>(service);
    described.Position[0] = position->X;
    described.Position[1] = position->Y;
    described.Position[2] = position->Z;
    described.IsPuppet = *Game.DescribedIsPuppet->GetValuePtr<bool>(service);
    return true;
}

CarryFeature ReadCarryFeature()
{
    static const uint64_t CarryName = RED4ext::CName("Carry").hash;
    static const uint64_t StateName = RED4ext::CName("state").hash;
    static const uint64_t PickupAnimationName = RED4ext::CName("pickupAnimation").hash;
    static const std::pair<uint64_t, uint8_t> FlagFields[] = {{RED4ext::CName("useBothHands").hash, net::kCarryBothHands},
                                                              {RED4ext::CName("instant").hash, net::kCarryInstant},
                                                              {RED4ext::CName("fastMode").hash, net::kCarryFastMode},
                                                              {RED4ext::CName("isCarryActive").hash, net::kCarryActive},
                                                              {RED4ext::CName("isFriendlyCarry").hash, net::kCarryFriendly},
                                                              {RED4ext::CName("wasThrown").hash, net::kCarryThrown}};
    CarryFeature feature;
    net::AnimEntry entry;
    if (!anim_tap::LatestOwnFeature(CarryName, entry))
    {
        return feature;
    }
    feature.Present = true;
    for (const auto& field : entry.Fields)
    {
        if (field.Bytes.size() == sizeof(int32_t) && (field.Name == StateName || field.Name == PickupAnimationName))
        {
            int32_t value = 0;
            std::memcpy(&value, field.Bytes.data(), sizeof(value));
            if (field.Name == StateName)
            {
                feature.State = static_cast<uint8_t>(std::clamp(value, 0, static_cast<int32_t>(net::kCarryThrow)));
            }
            else
            {
                feature.PickupAnimation = static_cast<uint8_t>(std::clamp(value, 0, 16));
            }
            continue;
        }
        for (const auto& [name, bit] : FlagFields)
        {
            if (field.Name == name && field.Bytes.size() == 1 && field.Bytes[0] != 0)
            {
                feature.Flags |= bit;
            }
        }
    }
    return feature;
}

void SendOwn(uint64_t nowMs, RED4ext::IScriptable* service, net::RelaySession& session)
{
    Described described;
    if (DescribeEntity(service, Own.ChildHash, described))
    {
        std::copy(described.Position, described.Position + 3, Own.Sent.Position);
        Own.Sent.Record = described.Record;
        Own.Sent.Look = described.Appearance;
    }
    Own.Sent.SenderClockMs = session.SessionClockMs();
    Own.LastSentMs = nowMs;
    session.SendGameMessage(net::WritePlayerCarry(Own.Sent));
}

void EndOwnCarry(uint64_t nowMs, const CarryFeature& feature, bool disposal, RED4ext::IScriptable* service, net::RelaySession& session,
                 const LogFunction& log)
{
    uint8_t endKind = feature.Present && feature.State >= net::kCarryDrop && feature.State <= net::kCarryDispose ? feature.State : net::kCarryRelease;
    if (Own.DisposalSeen || disposal)
    {
        endKind = net::kCarryDispose;
    }
    Own.Sent.Kind = endKind;
    Own.Sent.Flags &= static_cast<uint8_t>(~net::kCarryActive);
    SendOwn(nowMs, service, session);
    Own.EndsSent = 0;
    Own.EndSentMs = nowMs;
    Own.Active = false;
    log("carry: we let go of " + HexText(Own.ChildHash) + " (" + KindName(endKind) + ", carry " + std::to_string(Own.Sent.CarryNumber) + ")");
}

void TickOwnCarry(uint64_t nowMs, RED4ext::IScriptable* service, net::RelaySession& session, NpcCopies& copies, NpcShare& ourNpcs,
                  const LogFunction& log)
{
    bool allowArms = true;
    bool carrying = false;
    RED4ext::StackArgs_t findArgs;
    findArgs.emplace_back(nullptr, &allowArms);
    game::CallMethod(service, Game.FindCarried, &carrying, findArgs);
    const bool disposal = *Game.CarryingDisposal->GetValuePtr<bool>(service);
    const CarryFeature feature = ReadCarryFeature();
    const uint64_t childHash = carrying ? Game.CarriedId->GetValuePtr<RED4ext::ent::EntityID>(service)->hash : 0;
    if (Own.Active && (!carrying || childHash != Own.ChildHash))
    {
        EndOwnCarry(nowMs, feature, disposal, service, session, log);
    }
    if (carrying && childHash)
    {
        const bool inArms = *Game.CarriedInArms->GetValuePtr<bool>(service);
        const bool fresh = !Own.Active;
        if (fresh)
        {
            if (Own.HeldCopy)
            {
                copies.SetHeld(Own.HeldCopy, false);
            }
            Own = OwnCarry {};
            Own.Active = true;
            Own.ChildHash = childHash;
            Own.Sent.CarryNumber = ++CarryCounter;
            Own.Sent.EntityHash = childHash;
            Own.Sent.NetId = copies.NetIdOfCopy(childHash);
            if (Own.Sent.NetId)
            {
                Own.HeldCopy = Own.Sent.NetId;
                copies.SetHeld(Own.HeldCopy, true);
            }
            else
            {
                Own.Sent.NetId = ourNpcs.NetIdOf(childHash);
            }
        }
        Own.DisposalSeen = Own.DisposalSeen || disposal;
        const uint8_t kind = feature.Present && feature.State != net::kCarryNone ? feature.State : net::kCarryHolding;
        const uint8_t flags = static_cast<uint8_t>((feature.Flags & FeatureFlagBits) | (net::CarryKindIsActive(kind) ? net::kCarryActive : 0) |
                                                   (inArms ? net::kCarryInArms : 0));
        const bool changed = kind != Own.Sent.Kind || feature.PickupAnimation != Own.Sent.PickupAnimation || flags != Own.Sent.Flags;
        if (fresh || changed || nowMs - Own.LastSentMs >= KeepAliveMs)
        {
            Own.Sent.Kind = kind;
            Own.Sent.PickupAnimation = feature.PickupAnimation;
            Own.Sent.Flags = flags;
            SendOwn(nowMs, service, session);
            if (fresh || changed)
            {
                log("carry: we " + std::string(fresh ? "picked up " : "carry ") + HexText(childHash) + " (" + KindName(kind) + ", carry " +
                    std::to_string(Own.Sent.CarryNumber) + ", net id " + HexText(Own.Sent.NetId) + ", record " + HexText(Own.Sent.Record & RecordMask) +
                    (inArms ? ", in the arms" : ", on the shoulder") + (feature.Present ? "" : ", no Carry feature seen yet") + ")");
            }
        }
    }
    if (!Own.Active && Own.EndsSent < EndRepeats && nowMs >= Own.EndSentMs + EndAgainAfterMs[Own.EndsSent])
    {
        SendOwn(nowMs, service, session);
        if (++Own.EndsSent == EndRepeats && Own.HeldCopy)
        {
            copies.SetHeld(Own.HeldCopy, false);
            Own.HeldCopy = 0;
        }
    }
}

void ReleaseHeld(PartnerCarry& partner, NpcCopies& copies)
{
    if (partner.HeldCopy)
    {
        copies.SetHeld(partner.HeldCopy, false);
        partner.HeldCopy = 0;
    }
}

void DetachFromPartner(uint32_t peerNumber, PartnerCarry& partner, RED4ext::IScriptable* service, const char* why, const LogFunction& log)
{
    CallWithEntity(service, Game.Detach, partner.NpcHash);
    partner.Attached = false;
    partner.Posed = false;
    log("carry: " + HexText(partner.NpcHash) + " comes off peer " + std::to_string(peerNumber) + "s body (" + why + ")");
}

uint64_t FindOurBody(uint32_t peerNumber, PartnerCarry& partner, RED4ext::IScriptable* service, NpcCopies& copies, NpcShare& ourNpcs,
                     uint32_t& heldCopy, const LogFunction& log)
{
    const auto& carry = partner.Latest;
    heldCopy = 0;
    if (carry.NetId)
    {
        uint32_t owner = 0;
        if (const uint64_t copyHash = copies.CopyEntity(carry.NetId, owner))
        {
            if (owner != peerNumber)
            {
                if (!partner.NoBodyLogged)
                {
                    partner.NoBodyLogged = true;
                    log("carry: peer " + std::to_string(peerNumber) + " carries npc " + HexText(carry.NetId) + " of peer " + std::to_string(owner) +
                        ", its owners snapshots carry our copy");
                }
                return 0;
            }
            heldCopy = carry.NetId;
            return copyHash;
        }
        if (const uint64_t ownHash = ourNpcs.EntityOf(carry.NetId))
        {
            return ownHash;
        }
    }
    Described described;
    if (DescribeEntity(service, carry.EntityHash, described) && described.IsPuppet &&
        (carry.Record == 0 || (described.Record & RecordMask) == (carry.Record & RecordMask)))
    {
        return carry.EntityHash;
    }
    if (!partner.NoBodyLogged)
    {
        partner.NoBodyLogged = true;
        log("carry: peer " + std::to_string(peerNumber) + " carries " + HexText(carry.EntityHash) + " (net id " + HexText(carry.NetId) + ", record " +
            HexText(carry.Record & RecordMask) + "), we have no body of that npc so their shoulder stays empty");
    }
    return 0;
}

void TakeCarry(uint32_t peerNumber, PartnerCarry& partner, const net::PlayerCarry& carry, uint64_t nowMs, RED4ext::IScriptable* service,
               NpcCopies& copies, const LogFunction& log)
{
    if (!partner.Known || carry.CarryNumber != partner.Latest.CarryNumber)
    {
        if (partner.Attached)
        {
            DetachFromPartner(peerNumber, partner, service, "another carry", log);
        }
        ReleaseHeld(partner, copies);
        auto pending = std::move(partner.Pending);
        partner = PartnerCarry {};
        partner.Pending = std::move(pending);
        partner.Known = true;
    }
    partner.Latest = carry;
    partner.LastHeardMs = nowMs;
    const bool active = net::CarryKindIsActive(carry.Kind) && (carry.Flags & net::kCarryActive);
    if (active)
    {
        return;
    }
    const bool placeKnown = carry.Position[0] != 0.0f || carry.Position[1] != 0.0f || carry.Position[2] != 0.0f;
    if (partner.Attached)
    {
        DetachFromPartner(peerNumber, partner, service, KindName(carry.Kind), log);
        partner.PlaceAtMs = nowMs + PlaceAfterMs;
        partner.HideWhenPlaced = carry.Kind == net::kCarryDispose;
        if (!placeKnown)
        {
            std::fill(partner.PlacePosition, partner.PlacePosition + 3, 0.0f);
        }
    }
    if (partner.PlaceAtMs && placeKnown)
    {
        std::copy(carry.Position, carry.Position + 3, partner.PlacePosition);
        partner.HideWhenPlaced = partner.HideWhenPlaced || carry.Kind == net::kCarryDispose;
    }
}

void TickPartner(uint32_t peerNumber, PartnerCarry& partner, uint64_t nowMs, RED4ext::IScriptable* service, RemoteBodies& bodies, NpcCopies& copies,
                 NpcShare& ourNpcs, const LogFunction& log)
{
    for (const auto& carry : partner.Pending)
    {
        TakeCarry(peerNumber, partner, carry, nowMs, service, copies, log);
    }
    partner.Pending.clear();
    if (!partner.Known)
    {
        return;
    }
    const auto& latest = partner.Latest;
    const bool active = net::CarryKindIsActive(latest.Kind) && (latest.Flags & net::kCarryActive);
    if (active && (latest.Flags & net::kCarryInArms))
    {
        if (!partner.ArmsLogged)
        {
            partner.ArmsLogged = true;
            log("carry: peer " + std::to_string(peerNumber) + " carries " + HexText(latest.Record & RecordMask) +
                " in the arms (a quest carry), nightcoop shows a double there, the mac port does not yet");
        }
    }
    else if (active && !partner.Attached && nowMs - partner.LastHeardMs <= CarrierQuietMs)
    {
        RED4ext::Handle<RED4ext::IScriptable> body;
        uint32_t heldCopy = 0;
        const uint64_t npcHash = bodies.GetBody(peerNumber, body) ? FindOurBody(peerNumber, partner, service, copies, ourNpcs, heldCopy, log) : 0;
        bool attached = false;
        if (npcHash)
        {
            RED4ext::ent::EntityID npcId(npcHash);
            RED4ext::StackArgs_t args;
            args.emplace_back(nullptr, &body);
            args.emplace_back(nullptr, &npcId);
            game::CallMethod(service, Game.Attach, &attached, args);
        }
        if (attached)
        {
            partner.NpcHash = npcHash;
            partner.Attached = true;
            partner.PlaceAtMs = 0;
            if (heldCopy)
            {
                partner.HeldCopy = heldCopy;
                copies.SetHeld(heldCopy, true);
            }
            log("carry: peer " + std::to_string(peerNumber) + " carries " + HexText(npcHash) + " (" + KindName(latest.Kind) + ", carry " +
                std::to_string(latest.CarryNumber) + (heldCopy ? ", our copy of their npc" : "") + "), it hangs on their shoulder here");
        }
    }
    if (partner.Attached && (!partner.Posed || latest.Kind != partner.PosedKind || latest.PickupAnimation != partner.PosedAnimation ||
                             latest.Flags != partner.PosedFlags))
    {
        int32_t state = latest.Kind;
        int32_t pickupAnimation = latest.PickupAnimation;
        int32_t flags = latest.Flags;
        RED4ext::ent::EntityID npcId(partner.NpcHash);
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &npcId);
        args.emplace_back(nullptr, &state);
        args.emplace_back(nullptr, &pickupAnimation);
        args.emplace_back(nullptr, &flags);
        game::CallMethod(service, Game.Pose, nullptr, args);
        partner.Posed = true;
        partner.PosedKind = latest.Kind;
        partner.PosedAnimation = latest.PickupAnimation;
        partner.PosedFlags = latest.Flags;
    }
    if (partner.Attached && nowMs - partner.LastHeardMs > CarrierQuietMs)
    {
        DetachFromPartner(peerNumber, partner, service, "nothing heard of the carry for 5 s", log);
        ReleaseHeld(partner, copies);
    }
    if (partner.PlaceAtMs && nowMs >= partner.PlaceAtMs)
    {
        partner.PlaceAtMs = 0;
        if (partner.HideWhenPlaced)
        {
            CallWithEntity(service, Game.HideForGood, partner.NpcHash);
            log("carry: peer " + std::to_string(peerNumber) + " put " + HexText(partner.NpcHash) + " away, ours is hidden for good");
        }
        else if (partner.PlacePosition[0] != 0.0f || partner.PlacePosition[1] != 0.0f || partner.PlacePosition[2] != 0.0f)
        {
            RED4ext::ent::EntityID npcId(partner.NpcHash);
            RED4ext::Vector4 position {partner.PlacePosition[0], partner.PlacePosition[1], partner.PlacePosition[2], 1.0f};
            RED4ext::StackArgs_t args;
            args.emplace_back(nullptr, &npcId);
            args.emplace_back(nullptr, &position);
            game::CallMethod(service, Game.Place, nullptr, args);
            char line[160];
            std::snprintf(line, sizeof(line), "carry: %s put where peer %u left it (%.1f %.1f %.1f)", HexText(partner.NpcHash).c_str(), peerNumber,
                          partner.PlacePosition[0], partner.PlacePosition[1], partner.PlacePosition[2]);
            log(line);
        }
        ReleaseHeld(partner, copies);
    }
}

void Forget()
{
    Own = OwnCarry {};
    Partners.clear();
}
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, const LogFunction& log)
{
    net::PlayerCarry carry;
    if (!net::ReadPlayerCarry(message, carry))
    {
        ++BadMessages;
        if (nowMs >= NextBadLogMs)
        {
            NextBadLogMs = nowMs + 10000;
            log("carry: bad PlayerCarry from peer " + std::to_string(fromPeer) + " (" + std::to_string(message.size()) + " bytes, " +
                std::to_string(BadMessages) + " so far)");
        }
        return;
    }
    auto& pending = Partners[fromPeer].Pending;
    if (pending.size() < MostPendingPerPeer)
    {
        pending.push_back(carry);
    }
}

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, RemoteBodies& bodies, NpcCopies& copies, NpcShare& ourNpcs, const LogFunction& log)
{
    if (nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    if (!inSave || !session.GetStatus().InSession)
    {
        Forget();
        return;
    }
    if (!Resolve(log))
    {
        Partners.clear();
        return;
    }
    auto service = game::ScriptService(ServiceName);
    if (!service)
    {
        return;
    }
    TickOwnCarry(nowMs, service.instance, session, copies, ourNpcs, log);
    for (auto& [peerNumber, partner] : Partners)
    {
        TickPartner(peerNumber, partner, nowMs, service.instance, bodies, copies, ourNpcs, log);
    }
}
}
