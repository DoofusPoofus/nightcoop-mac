#include "plugin/NpcCombat.hpp"

#include "net/CombatMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/NpcCopies.hpp"
#include "plugin/NpcShare.hpp"
#include "plugin/RemoteBodies.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <map>

namespace nightcoop_mac::npc_combat
{
namespace
{
constexpr uint64_t ReportEveryMs = 10000;
constexpr uint32_t ClaimWindow = 1024;
constexpr float LethalMarginPercent = 0.01f;

struct ScriptCalls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* MarkCopy = nullptr;
    RED4ext::CBaseFunction* ReleaseCopy = nullptr;
    RED4ext::CBaseFunction* ForgetCopy = nullptr;
    RED4ext::CBaseFunction* ForgetAllCopies = nullptr;
    RED4ext::CBaseFunction* TakeClaims = nullptr;
    RED4ext::CBaseFunction* ApplyClaim = nullptr;
    RED4ext::CBaseFunction* Describe = nullptr;
    RED4ext::CBaseFunction* FindEntity = nullptr;
    RED4ext::CBaseFunction* MarkBody = nullptr;
    RED4ext::CBaseFunction* ForgetBodies = nullptr;
    RED4ext::CBaseFunction* TakeBodyHits = nullptr;
    RED4ext::CBaseFunction* ApplyPlayerHit = nullptr;
    RED4ext::CBaseFunction* ShareThreat = nullptr;
    RED4ext::CBaseFunction* TakeHackStages = nullptr;
    RED4ext::CBaseFunction* ApplyHackStage = nullptr;
    RED4ext::CClass* ClaimClass = nullptr;
    RED4ext::CClass* BodyHitClass = nullptr;
};

struct PeerClaims
{
    uint32_t Newest = 0;
    bool AnySeen = false;
    std::bitset<ClaimWindow> Seen;
};

ScriptCalls Script;
uint32_t NextClaimNumber = 1;
std::map<uint32_t, PeerClaims> ClaimsByPeer;
uint64_t NextReportMs = 0;
uint32_t ClaimsSent = 0;
uint32_t ClaimsWithoutOwner = 0;
uint32_t ResultsApplied = 0;
uint32_t ResultsOther = 0;
uint32_t ClaimsTaken = 0;
uint32_t ClaimsRefused = 0;
uint32_t NextPlayerHitNumber = 1;
uint32_t BodyHitsSent = 0;
uint32_t PlayerHitsApplied = 0;
uint32_t PlayerHitsDropped = 0;
uint32_t NextHackSerial = 1;
uint32_t HacksSent = 0;
uint32_t HacksApplied = 0;
bool TroubleLogged = false;
uint64_t NextTestClaimMs = 0;
constexpr uint64_t TestClaimEveryMs = 5000;

bool Ready()
{
    if (Script.Resolved)
    {
        return Script.Usable;
    }
    Script.Resolved = true;
    Script.MarkCopy = game::FindFunction("NightCoopCombat", "MarkCopy");
    Script.ReleaseCopy = game::FindFunction("NightCoopCombat", "ReleaseCopy");
    Script.ForgetCopy = game::FindFunction("NightCoopCombat", "ForgetCopy");
    Script.ForgetAllCopies = game::FindFunction("NightCoopCombat", "ForgetAllCopies");
    Script.TakeClaims = game::FindFunction("NightCoopCombat", "TakeClaims");
    Script.ApplyClaim = game::FindFunction("NightCoopCombat", "ApplyClaim");
    Script.Describe = game::FindFunction("NightCoopCombat", "Describe");
    Script.FindEntity = game::FindFunction("ScriptGameInstance", "FindEntityByID");
    Script.ClaimClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopCombatClaim");
    Script.BodyHitClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopBodyHit");
    Script.MarkBody = game::FindFunction("NightCoopCombat", "MarkBody");
    Script.ForgetBodies = game::FindFunction("NightCoopCombat", "ForgetBodies");
    Script.TakeBodyHits = game::FindFunction("NightCoopCombat", "TakeBodyHits");
    Script.ApplyPlayerHit = game::FindFunction("NightCoopCombat", "ApplyPlayerHit");
    Script.ShareThreat = game::FindFunction("NightCoopCombat", "ShareThreat");
    Script.TakeHackStages = game::FindFunction("NightCoopCombat", "TakeHackStages");
    Script.ApplyHackStage = game::FindFunction("NightCoopCombat", "ApplyHackStage");
    if (!game::ParamsAre(Script.TakeHackStages, {}) || !game::ParamsAre(Script.ApplyHackStage, {"handle:ScriptedPuppet", "TweakDBID", "Int32", "handle:gameObject"}))
    {
        Script.TakeHackStages = nullptr;
    }
    Script.Usable = Script.ClaimClass && game::ParamsAre(Script.MarkCopy, {"handle:gameObject"}) &&
                    game::ParamsAre(Script.ReleaseCopy, {"handle:gameObject", "Bool"}) && game::ParamsAre(Script.ForgetCopy, {"entEntityID"}) &&
                    game::ParamsAre(Script.ForgetAllCopies, {}) && game::ParamsAre(Script.TakeClaims, {}) &&
                    game::ParamsAre(Script.ApplyClaim, {"handle:ScriptedPuppet", "handle:gameObject", "Float", "Bool"}) &&
                    game::ParamsAre(Script.FindEntity, {"ScriptGameInstance", "entEntityID"}) && Script.BodyHitClass &&
                    game::ParamsAre(Script.MarkBody, {"handle:gameObject", "Int32"}) && game::ParamsAre(Script.ForgetBodies, {}) &&
                    game::ParamsAre(Script.TakeBodyHits, {}) && game::ParamsAre(Script.ShareThreat, {"handle:ScriptedPuppet", "handle:gameObject"}) &&
                    game::ParamsAre(Script.ApplyPlayerHit, {"handle:gameObject", "TweakDBID", "Int32", "Float", "Vector4", "Vector4", "array:Int32"});
    return Script.Usable;
}

template<typename Value>
Value Field(RED4ext::IScriptable* object, const char* name)
{
    Value value {};
    auto* property = object->GetType()->GetProperty(name);
    if (property && property->type && property->type->GetSize() == sizeof(Value))
    {
        value = *property->GetValuePtr<Value>(object);
    }
    return value;
}

std::vector<uint32_t> HitFlags(RED4ext::IScriptable* object, bool (*keep)(uint32_t))
{
    std::vector<uint32_t> flags;
    auto* property = object->GetType()->GetProperty("flags");
    if (!property || !property->type || property->type->GetType() != RED4ext::rtti::ERTTIType::Array)
    {
        return flags;
    }
    for (int32_t flag : *property->GetValuePtr<RED4ext::DynArray<int32_t>>(object))
    {
        if (flag >= 0 && keep(static_cast<uint32_t>(flag)))
        {
            flags.push_back(static_cast<uint32_t>(flag));
        }
    }
    return flags;
}

RED4ext::Handle<RED4ext::IScriptable> EntityById(uint64_t entityHash)
{
    RED4ext::Handle<RED4ext::IScriptable> entity;
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    args.emplace_back(nullptr, &entityId);
    game::CallStatic(Script.FindEntity, &entity, args);
    return entity;
}

bool FreshClaim(uint32_t peer, uint32_t claimNumber, uint8_t& outcome)
{
    auto& claims = ClaimsByPeer[peer];
    if (!claims.AnySeen)
    {
        claims.AnySeen = true;
        claims.Newest = claimNumber;
        claims.Seen.reset();
        claims.Seen.set(claimNumber % ClaimWindow);
        return true;
    }
    if (static_cast<int32_t>(claimNumber - claims.Newest) > 0)
    {
        const uint32_t step = claimNumber - claims.Newest;
        if (step >= ClaimWindow)
        {
            claims.Seen.reset();
        }
        else
        {
            for (uint32_t number = claims.Newest + 1; number != claimNumber; ++number)
            {
                claims.Seen.reset(number % ClaimWindow);
            }
        }
        claims.Newest = claimNumber;
        claims.Seen.set(claimNumber % ClaimWindow);
        return true;
    }
    if (claims.Newest - claimNumber >= ClaimWindow)
    {
        outcome = net::kHitTooOld;
        return false;
    }
    if (claims.Seen.test(claimNumber % ClaimWindow))
    {
        outcome = net::kHitDuplicate;
        return false;
    }
    claims.Seen.set(claimNumber % ClaimWindow);
    return true;
}

void SendClaims(uint64_t nowMs, NpcCopies& copies, net::RelaySession& session, const LogFunction& log)
{
    auto service = game::ScriptService("NightCoopCombat");
    if (!service)
    {
        return;
    }
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> claims;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(service.instance, Script.TakeClaims, &claims, noArgs))
    {
        return;
    }
    for (auto& claimObject : claims)
    {
        if (!claimObject)
        {
            continue;
        }
        const auto target = Field<RED4ext::ent::EntityID>(claimObject.instance, "target");
        uint32_t netId = 0;
        uint32_t ownerPeer = 0;
        float ownerHealth = 100.0f;
        if (!copies.FindByEntity(target.hash, netId, ownerPeer, ownerHealth))
        {
            ++ClaimsWithoutOwner;
            continue;
        }
        net::HitClaim claim;
        claim.ClaimNumber = NextClaimNumber++;
        claim.NetId = netId;
        claim.HealthPercent = Field<float>(claimObject.instance, "percent");
        claim.Flags = claim.HealthPercent + LethalMarginPercent >= ownerHealth ? net::kHitClaimLethal : 0;
        claim.ClaimTimeMs = nowMs;
        claim.Details.AttackRecord = Field<uint64_t>(claimObject.instance, "attackRecord");
        const int32_t attackType = Field<int32_t>(claimObject.instance, "attackType");
        claim.Details.AttackType = attackType >= 0 && attackType <= 14 ? static_cast<uint8_t>(attackType) : 0xFF;
        claim.Details.WeaponCharge = Field<float>(claimObject.instance, "charge");
        const auto hitPosition = Field<RED4ext::Vector4>(claimObject.instance, "hitPosition");
        const auto targetPosition = Field<RED4ext::Vector4>(claimObject.instance, "targetPosition");
        const auto hitDirection = Field<RED4ext::Vector4>(claimObject.instance, "hitDirection");
        claim.Details.Offset[0] = hitPosition.X - targetPosition.X;
        claim.Details.Offset[1] = hitPosition.Y - targetPosition.Y;
        claim.Details.Offset[2] = hitPosition.Z - targetPosition.Z;
        if (hitPosition.X == 0.0f && hitPosition.Y == 0.0f && hitPosition.Z == 0.0f)
        {
            claim.Details.Offset[0] = 0.0f;
            claim.Details.Offset[1] = 0.0f;
            claim.Details.Offset[2] = 1.3f;
        }
        claim.Details.Direction[0] = hitDirection.X;
        claim.Details.Direction[1] = hitDirection.Y;
        claim.Details.Direction[2] = hitDirection.Z;
        claim.Details.Flags = HitFlags(claimObject.instance, net::KeepClaimFlag);
        if (!std::isfinite(claim.HealthPercent) || claim.HealthPercent <= 0.0f)
        {
            continue;
        }
        session.SendGameMessage(net::WriteHitClaim(claim), static_cast<int32_t>(ownerPeer));
        ++ClaimsSent;
        char line[200];
        std::snprintf(line, sizeof(line), "combat: claim #%u on npc %#010x of peer %u: %.2f %% of its health%s", claim.ClaimNumber, netId, ownerPeer,
                      claim.HealthPercent, claim.Flags & net::kHitClaimLethal ? ", lethal" : "");
        log(line);
    }
}

void SendBodyHits(NpcCopies& copies, NpcShare& ourNpcs, net::RelaySession& session)
{
    auto service = game::ScriptService("NightCoopCombat");
    if (!service)
    {
        return;
    }
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> bodyHits;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(service.instance, Script.TakeBodyHits, &bodyHits, noArgs))
    {
        return;
    }
    for (auto& bodyHitObject : bodyHits)
    {
        if (!bodyHitObject)
        {
            continue;
        }
        const int32_t peer = Field<int32_t>(bodyHitObject.instance, "peer");
        const auto attacker = Field<RED4ext::ent::EntityID>(bodyHitObject.instance, "attacker");
        uint32_t attackerNetId = ourNpcs.NetIdOfEntity(attacker.hash);
        if (!attackerNetId)
        {
            uint32_t ownerPeer = 0;
            float ownerHealth = 0;
            copies.FindByEntity(attacker.hash, attackerNetId, ownerPeer, ownerHealth);
        }
        net::PlayerHit hit;
        hit.HitNumber = NextPlayerHitNumber++;
        hit.AttackerNetId = attackerNetId;
        hit.SessionTimeMs = session.SessionClockMs();
        hit.Details.AttackRecord = Field<uint64_t>(bodyHitObject.instance, "attackRecord");
        const int32_t attackType = Field<int32_t>(bodyHitObject.instance, "attackType");
        hit.Details.AttackType = attackType >= 0 && attackType <= 14 ? static_cast<uint8_t>(attackType) : 0xFF;
        hit.Details.WeaponCharge = Field<float>(bodyHitObject.instance, "charge");
        const auto hitPosition = Field<RED4ext::Vector4>(bodyHitObject.instance, "hitPosition");
        const auto targetPosition = Field<RED4ext::Vector4>(bodyHitObject.instance, "targetPosition");
        const auto hitDirection = Field<RED4ext::Vector4>(bodyHitObject.instance, "hitDirection");
        hit.Details.Offset[0] = hitPosition.X - targetPosition.X;
        hit.Details.Offset[1] = hitPosition.Y - targetPosition.Y;
        hit.Details.Offset[2] = hitPosition.Z - targetPosition.Z;
        hit.Details.Direction[0] = hitDirection.X;
        hit.Details.Direction[1] = hitDirection.Y;
        hit.Details.Direction[2] = hitDirection.Z;
        hit.Details.Flags = HitFlags(bodyHitObject.instance, net::KeepPlayerHitFlag);
        if (peer >= 0)
        {
            session.SendGameMessage(net::WritePlayerHit(hit), peer);
            ++BodyHitsSent;
        }
    }
}

void SendHackStages(NpcCopies& copies, net::RelaySession& session, const LogFunction& log)
{
    auto service = game::ScriptService("NightCoopCombat");
    if (!service || !Script.TakeHackStages)
    {
        return;
    }
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> stages;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallMethod(service.instance, Script.TakeHackStages, &stages, noArgs))
    {
        return;
    }
    for (auto& stageObject : stages)
    {
        if (!stageObject)
        {
            continue;
        }
        const auto target = Field<RED4ext::ent::EntityID>(stageObject.instance, "target");
        uint32_t netId = 0;
        uint32_t ownerPeer = 0;
        float ownerHealth = 0;
        if (!copies.FindByEntity(target.hash, netId, ownerPeer, ownerHealth))
        {
            continue;
        }
        net::HackSync hack;
        hack.Stage = Field<int32_t>(stageObject.instance, "stage") == 2 ? net::kHackStageCompletion : net::kHackStageStart;
        hack.Serial = NextHackSerial++;
        hack.HackerPeer = session.GetStatus().MyPeerNumber;
        hack.NetId = netId;
        hack.ActionRecord = Field<uint64_t>(stageObject.instance, "action");
        if (!hack.ActionRecord)
        {
            continue;
        }
        session.SendGameMessage(net::WriteHackSync(hack), static_cast<int32_t>(ownerPeer));
        ++HacksSent;
        char line[160];
        std::snprintf(line, sizeof(line), "combat: our quickhack %#llx %s on npc %#010x of peer %u", static_cast<unsigned long long>(hack.ActionRecord),
                      hack.Stage == net::kHackStageStart ? "started" : "finished", netId, ownerPeer);
        log(line);
    }
}

void ApplyHackSync(uint32_t fromPeer, const net::HackSync& hack, NpcShare& ourNpcs, RemoteBodies& bodies, const LogFunction& log)
{
    if (hack.Kind != net::kHackOnNpc || (hack.Flags & net::kHackFlagVisual) || !Script.TakeHackStages)
    {
        return;
    }
    const uint64_t entityHash = ourNpcs.EntityOfNetId(hack.NetId);
    auto npc = entityHash ? EntityById(entityHash) : RED4ext::Handle<RED4ext::IScriptable>();
    auto service = game::ScriptService("NightCoopCombat");
    if (!npc || !service)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> body;
    bodies.GetBody(fromPeer, body);
    RED4ext::TweakDBID actionRecord(hack.ActionRecord);
    int32_t stage = hack.Stage;
    int32_t applied = 0;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &npc);
    args.emplace_back(nullptr, &actionRecord);
    args.emplace_back(nullptr, &stage);
    args.emplace_back(nullptr, &body);
    game::CallMethod(service.instance, Script.ApplyHackStage, &applied, args);
    HacksApplied += static_cast<uint32_t>(std::max(applied, 0));
    char line[200];
    std::snprintf(line, sizeof(line), "combat: peer %u quickhacked our npc %#010x with %#llx (%s), %d status(es) hung on it%s", fromPeer, hack.NetId,
                  static_cast<unsigned long long>(hack.ActionRecord), hack.Stage == net::kHackStageStart ? "start" : "completion", applied,
                  body ? "" : " (their body is not here, no instigator)");
    log(line);
}

void ApplyPlayerHit(uint32_t fromPeer, const net::PlayerHit& hit, NpcCopies& copies, NpcShare& ourNpcs, const LogFunction& log)
{
    uint64_t attackerHash = hit.AttackerNetId ? copies.EntityOfNetId(hit.AttackerNetId) : 0;
    if (!attackerHash && hit.AttackerNetId)
    {
        attackerHash = ourNpcs.EntityOfNetId(hit.AttackerNetId);
    }
    auto attacker = attackerHash ? EntityById(attackerHash) : RED4ext::Handle<RED4ext::IScriptable>();
    auto service = game::ScriptService("NightCoopCombat");
    if (!attacker || !service)
    {
        ++PlayerHitsDropped;
        return;
    }
    RED4ext::TweakDBID attackRecord(hit.Details.AttackRecord);
    int32_t attackType = hit.Details.AttackType == 0xFF ? -1 : hit.Details.AttackType;
    float charge = hit.Details.WeaponCharge;
    RED4ext::Vector4 offset {hit.Details.Offset[0], hit.Details.Offset[1], hit.Details.Offset[2], 0.0f};
    RED4ext::Vector4 direction {hit.Details.Direction[0], hit.Details.Direction[1], hit.Details.Direction[2], 0.0f};
    RED4ext::DynArray<int32_t> flags;
    for (uint32_t flag : hit.Details.Flags)
    {
        flags.PushBack(static_cast<int32_t>(flag));
    }
    bool applied = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &attacker);
    args.emplace_back(nullptr, &attackRecord);
    args.emplace_back(nullptr, &attackType);
    args.emplace_back(nullptr, &charge);
    args.emplace_back(nullptr, &offset);
    args.emplace_back(nullptr, &direction);
    args.emplace_back(nullptr, &flags);
    game::CallMethod(service.instance, Script.ApplyPlayerHit, &applied, args);
    ++(applied ? PlayerHitsApplied : PlayerHitsDropped);
    if (applied && PlayerHitsApplied <= 3)
    {
        char line[200];
        std::snprintf(line, sizeof(line), "combat: peer %u says npc %#010x hit our body there, attack %#llx queued on our player", fromPeer, hit.AttackerNetId,
                      static_cast<unsigned long long>(hit.Details.AttackRecord));
        log(line);
    }
}

void Report(uint64_t nowMs, const LogFunction& log)
{
    if (nowMs < NextReportMs)
    {
        return;
    }
    NextReportMs = nowMs + ReportEveryMs;
    if (!ClaimsSent && !ClaimsWithoutOwner && !ResultsApplied && !ResultsOther && !ClaimsTaken && !ClaimsRefused && !BodyHitsSent && !PlayerHitsApplied &&
        !PlayerHitsDropped)
    {
        return;
    }
    std::string scriptState;
    auto service = game::ScriptService("NightCoopCombat");
    RED4ext::CString text;
    RED4ext::StackArgs_t noArgs;
    if (service && Script.Describe && game::CallMethod(service.instance, Script.Describe, &text, noArgs))
    {
        scriptState = std::string(" (") + text.c_str() + ")";
    }
    log("combat: in 10 s " + std::to_string(ClaimsSent) + " claim(s) sent, " + std::to_string(ClaimsWithoutOwner) + " without a copy, " +
        std::to_string(ResultsApplied) + " applied, " + std::to_string(ResultsOther) + " other answer(s); " + std::to_string(ClaimsTaken) +
        " partner claim(s) applied here, " + std::to_string(ClaimsRefused) + " refused; " + std::to_string(BodyHitsSent) + " body hit(s) sent, " +
        std::to_string(PlayerHitsApplied) + " partner hit(s) on us applied, " + std::to_string(PlayerHitsDropped) + " dropped" + scriptState);
    ClaimsSent = ClaimsWithoutOwner = ResultsApplied = ResultsOther = ClaimsTaken = ClaimsRefused = 0;
    BodyHitsSent = PlayerHitsApplied = PlayerHitsDropped = 0;
}
}

void MarkCopy(RED4ext::Handle<RED4ext::IScriptable>& copy)
{
    auto service = Ready() ? game::ScriptService("NightCoopCombat") : RED4ext::Handle<RED4ext::IScriptable>();
    if (!service || !copy)
    {
        return;
    }
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &copy);
    game::CallMethod(service.instance, Script.MarkCopy, nullptr, args);
}

void ReleaseCopy(RED4ext::Handle<RED4ext::IScriptable>& copy, bool rewardUs)
{
    auto service = Ready() ? game::ScriptService("NightCoopCombat") : RED4ext::Handle<RED4ext::IScriptable>();
    if (!service || !copy)
    {
        return;
    }
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &copy);
    args.emplace_back(nullptr, &rewardUs);
    game::CallMethod(service.instance, Script.ReleaseCopy, nullptr, args);
}

void ForgetCopy(uint64_t entityHash)
{
    auto service = Ready() ? game::ScriptService("NightCoopCombat") : RED4ext::Handle<RED4ext::IScriptable>();
    if (!service || !entityHash)
    {
        return;
    }
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    game::CallMethod(service.instance, Script.ForgetCopy, nullptr, args);
}

void ForgetAll()
{
    ClaimsByPeer.clear();
    auto service = Script.Usable ? game::ScriptService("NightCoopCombat") : RED4ext::Handle<RED4ext::IScriptable>();
    if (service)
    {
        RED4ext::StackArgs_t noArgs;
        game::CallMethod(service.instance, Script.ForgetAllCopies, nullptr, noArgs);
        game::CallMethod(service.instance, Script.ForgetBodies, nullptr, noArgs);
    }
}

void MarkBody(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body)
{
    auto service = Ready() ? game::ScriptService("NightCoopCombat") : RED4ext::Handle<RED4ext::IScriptable>();
    if (!service || !body)
    {
        return;
    }
    int32_t peer = static_cast<int32_t>(peerNumber);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &peer);
    game::CallMethod(service.instance, Script.MarkBody, nullptr, args);
}

void ShareThreat(RED4ext::Handle<RED4ext::IScriptable>& npc, RED4ext::Handle<RED4ext::IScriptable>& body)
{
    auto service = Ready() ? game::ScriptService("NightCoopCombat") : RED4ext::Handle<RED4ext::IScriptable>();
    if (!service || !npc || !body)
    {
        return;
    }
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &npc);
    args.emplace_back(nullptr, &body);
    game::CallMethod(service.instance, Script.ShareThreat, nullptr, args);
}

void Tick(uint64_t nowMs, bool inSave, bool testClaims, NpcCopies& copies, NpcShare& ourNpcs, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave)
    {
        return;
    }
    if (!Ready())
    {
        if (!TroubleLogged)
        {
            TroubleLogged = true;
            log("combat: the NightCoopCombat script service looks different than expected, no hit claims");
        }
        return;
    }
    if (testClaims && nowMs >= NextTestClaimMs)
    {
        NextTestClaimMs = nowMs + TestClaimEveryMs;
        static auto* testHack = game::FindFunction("NightCoopCombat", "TestHackOnFirstCopy");
        static auto* testClaim = game::FindFunction("NightCoopCombat", "TestClaimOnFirstCopy");
        auto service = game::ScriptService("NightCoopCombat");
        float percent = 40.0f;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &percent);
        if (service && testClaim)
        {
            game::CallMethod(service.instance, testClaim, nullptr, args);
        }
        RED4ext::StackArgs_t noArgs;
        if (service && testHack)
        {
            game::CallMethod(service.instance, testHack, nullptr, noArgs);
        }
    }
    SendClaims(nowMs, copies, session, log);
    SendBodyHits(copies, ourNpcs, session);
    SendHackStages(copies, session, log);
    Report(nowMs, log);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, NpcCopies& copies, NpcShare& ourNpcs, RemoteBodies& bodies,
               net::RelaySession& session, const LogFunction& log)
{
    const uint8_t type = net::MessageTypeOf(message);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::HackSync))
    {
        net::HackSync hack;
        if (Ready() && net::ReadHackSync(message, hack))
        {
            ApplyHackSync(fromPeer, hack, ourNpcs, bodies, log);
        }
        return;
    }
    if (type == static_cast<uint8_t>(nc::proto::MsgType::PlayerHit))
    {
        net::PlayerHit hit;
        if (Ready() && net::ReadPlayerHit(message, hit))
        {
            ApplyPlayerHit(fromPeer, hit, copies, ourNpcs, log);
        }
        return;
    }
    if (type == static_cast<uint8_t>(nc::proto::MsgType::HitResult))
    {
        net::HitResult result;
        if (net::ReadHitResult(message, result))
        {
            ++(result.Outcome == net::kHitApplied ? ResultsApplied : ResultsOther);
            if (result.Outcome != net::kHitApplied || result.HealthAfter <= 0.0f)
            {
                char line[160];
                std::snprintf(line, sizeof(line), "combat: peer %u answered claim #%u: outcome %u, %.2f %% applied, health after %.2f %%", fromPeer,
                              result.ClaimNumber, result.Outcome, result.AppliedPercent, result.HealthAfter);
                log(line);
            }
        }
        return;
    }
    net::HitClaim claim;
    if (type != static_cast<uint8_t>(nc::proto::MsgType::HitClaim) || !net::ReadHitClaim(message, claim))
    {
        if (type == static_cast<uint8_t>(nc::proto::MsgType::HitClaim))
        {
            ++ClaimsRefused;
        }
        return;
    }
    net::HitResult result;
    result.ClaimNumber = claim.ClaimNumber;
    uint8_t outcome = net::kHitApplied;
    const uint32_t netId = static_cast<uint32_t>(claim.NetId);
    if (!Ready() || !FreshClaim(fromPeer, claim.ClaimNumber, outcome))
    {
        result.Outcome = Ready() ? outcome : net::kHitBlocked;
        session.SendGameMessage(net::WriteHitResult(result), static_cast<int32_t>(fromPeer));
        ++ClaimsRefused;
        return;
    }
    const uint64_t entityHash = ourNpcs.EntityOfNetId(netId);
    auto npc = entityHash ? EntityById(entityHash) : RED4ext::Handle<RED4ext::IScriptable>();
    auto service = game::ScriptService("NightCoopCombat");
    if (!npc || !service)
    {
        result.Outcome = ourNpcs.RecentlyDied(netId, nowMs) ? net::kHitTargetDead : net::kHitNoTarget;
        session.SendGameMessage(net::WriteHitResult(result), static_cast<int32_t>(fromPeer));
        ++ClaimsRefused;
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> body;
    bodies.GetBody(fromPeer, body);
    float percent = claim.HealthPercent;
    bool lethal = (claim.Flags & net::kHitClaimLethal) != 0;
    float healthAfter = 0.0f;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &npc);
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &percent);
    args.emplace_back(nullptr, &lethal);
    if (!game::CallMethod(service.instance, Script.ApplyClaim, &healthAfter, args))
    {
        result.Outcome = net::kHitBlocked;
        session.SendGameMessage(net::WriteHitResult(result), static_cast<int32_t>(fromPeer));
        ++ClaimsRefused;
        return;
    }
    result.Outcome = net::kHitApplied;
    result.AppliedPercent = percent;
    result.HealthAfter = healthAfter;
    session.SendGameMessage(net::WriteHitResult(result), static_cast<int32_t>(fromPeer));
    ++ClaimsTaken;
    char line[200];
    std::snprintf(line, sizeof(line), "combat: peer %u claim #%u on our npc %#010x: %.2f %% from their body%s, health now %.2f %%", fromPeer,
                  claim.ClaimNumber, netId, percent, body ? "" : " (no body here, no instigator)", healthAfter);
    log(line);
}
}
