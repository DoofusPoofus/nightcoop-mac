#include "net/CombatMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr uint8_t MostHitFlags = 24;
constexpr uint8_t LastAttackType = 14;
constexpr float FarthestHitOffset = 2.5f;

bool OpenMessage(nc::proto::Reader& reader, nc::proto::MsgType expectedType)
{
    return reader.ReadHeader() == expectedType && reader.Ok();
}

void PutDetails(nc::proto::Writer& writer, const HitDetails& details)
{
    writer.PutU64(details.AttackRecord);
    writer.PutU8(details.AttackType);
    writer.PutU8(details.HitType);
    writer.PutF32(details.WeaponCharge);
    const float length = std::sqrt(details.Offset[0] * details.Offset[0] + details.Offset[1] * details.Offset[1] + details.Offset[2] * details.Offset[2]);
    const bool offsetOk = std::isfinite(length) && length <= FarthestHitOffset;
    writer.PutF32(offsetOk ? details.Offset[0] : 0.0f);
    writer.PutF32(offsetOk ? details.Offset[1] : 0.0f);
    writer.PutF32(offsetOk ? details.Offset[2] : 1.3f);
    for (float value : details.Direction)
    {
        writer.PutF32(std::isfinite(value) ? value : 0.0f);
    }
    const uint8_t flagCount = static_cast<uint8_t>(std::min<size_t>(details.Flags.size(), MostHitFlags));
    writer.PutU8(flagCount);
    for (uint8_t index = 0; index < flagCount; ++index)
    {
        writer.PutU32(details.Flags[index]);
    }
}

bool GetDetails(nc::proto::Reader& reader, HitDetails& details)
{
    details.AttackRecord = reader.GetU64();
    details.AttackType = reader.GetU8();
    details.HitType = reader.GetU8();
    details.WeaponCharge = reader.GetF32();
    for (float& value : details.Offset)
    {
        value = reader.GetF32();
    }
    for (float& value : details.Direction)
    {
        value = reader.GetF32();
    }
    const uint8_t flagCount = reader.GetU8();
    if (!reader.Ok() || flagCount > MostHitFlags || (details.AttackType > LastAttackType && details.AttackType != 0xFF))
    {
        return false;
    }
    details.Flags.clear();
    for (uint8_t index = 0; index < flagCount; ++index)
    {
        details.Flags.push_back(reader.GetU32());
    }
    const bool finite = std::isfinite(details.WeaponCharge) && std::all_of(details.Offset, details.Offset + 3, [](float value) { return std::isfinite(value); }) &&
                        std::all_of(details.Direction, details.Direction + 3, [](float value) { return std::isfinite(value); });
    return reader.Ok() && finite;
}
}

bool KeepClaimFlag(uint32_t flag)
{
    switch (flag)
    {
    case 18:
    case 19:
    case 22:
    case 26:
    case 28:
    case 29:
    case 35:
    case 42: return true;
    default: return false;
    }
}

bool KeepPlayerHitFlag(uint32_t flag)
{
    switch (flag)
    {
    case 0:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 23:
    case 31:
    case 34:
    case 45:
    case 49: return false;
    default: return true;
    }
}

std::vector<uint8_t> WriteHitClaim(const HitClaim& claim)
{
    nc::proto::Writer writer(nc::proto::MsgType::HitClaim);
    writer.PutU32(claim.ClaimNumber);
    writer.PutU64(claim.NetId);
    writer.PutF32(claim.HealthPercent);
    writer.PutU8(claim.HealthWrites);
    writer.PutU8(claim.Flags);
    writer.PutU64(claim.ClaimTimeMs);
    PutDetails(writer, claim.Details);
    return writer.Data();
}

bool ReadHitClaim(const std::vector<uint8_t>& message, HitClaim& claim)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::HitClaim))
    {
        return false;
    }
    claim.ClaimNumber = reader.GetU32();
    claim.NetId = reader.GetU64();
    claim.HealthPercent = reader.GetF32();
    claim.HealthWrites = reader.GetU8();
    claim.Flags = reader.GetU8();
    claim.ClaimTimeMs = reader.GetU64();
    return reader.Ok() && GetDetails(reader, claim.Details) && reader.Remaining() == 0 && std::isfinite(claim.HealthPercent) && claim.HealthPercent > 0.0f;
}

std::vector<uint8_t> WriteHitResult(const HitResult& result)
{
    nc::proto::Writer writer(nc::proto::MsgType::HitResult);
    writer.PutU32(result.ClaimNumber);
    writer.PutU8(result.Outcome);
    writer.PutF32(result.AppliedPercent);
    writer.PutF32(result.HealthAfter);
    return writer.Data();
}

bool ReadHitResult(const std::vector<uint8_t>& message, HitResult& result)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::HitResult))
    {
        return false;
    }
    result.ClaimNumber = reader.GetU32();
    result.Outcome = reader.GetU8();
    result.AppliedPercent = reader.GetF32();
    result.HealthAfter = reader.GetF32();
    return reader.Ok() && reader.Remaining() == 0 && std::isfinite(result.AppliedPercent) && std::isfinite(result.HealthAfter);
}

std::vector<uint8_t> WriteNpcAction(const NpcAction& action)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcAction);
    writer.PutU8(action.Kind);
    writer.PutU32(action.ClaimNumber);
    writer.PutU8(action.Zone);
    return writer.Data();
}

bool ReadNpcAction(const std::vector<uint8_t>& message, NpcAction& action)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::NpcAction))
    {
        return false;
    }
    action.Kind = reader.GetU8();
    action.ClaimNumber = reader.GetU32();
    action.Zone = reader.GetU8();
    return reader.Ok() && reader.Remaining() == 0 && action.Kind == 1 && action.Zone <= 10;
}

std::vector<uint8_t> WriteHackSync(const HackSync& hack)
{
    nc::proto::Writer writer(nc::proto::MsgType::HackSync);
    writer.PutU8(hack.Kind);
    writer.PutU8(hack.Stage);
    writer.PutU8(hack.Flags);
    writer.PutU32(hack.Serial);
    writer.PutU32(hack.HackerPeer);
    writer.PutU32(hack.NetId);
    writer.PutU64(hack.ActionRecord);
    return writer.Data();
}

bool ReadHackSync(const std::vector<uint8_t>& message, HackSync& hack)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::HackSync))
    {
        return false;
    }
    hack.Kind = reader.GetU8();
    hack.Stage = reader.GetU8();
    hack.Flags = reader.GetU8();
    hack.Serial = reader.GetU32();
    hack.HackerPeer = reader.GetU32();
    hack.NetId = reader.GetU32();
    hack.ActionRecord = reader.GetU64();
    const bool kindNetIdOk = hack.Kind == 2 ? hack.NetId == 0 : true;
    return reader.Ok() && reader.Remaining() == 0 && hack.Kind >= 1 && hack.Kind <= 3 && hack.Stage >= 1 && hack.Stage <= 2 && (hack.Flags & ~0x03) == 0 &&
           (hack.Kind == kHackOnNpc || (hack.Flags & kHackFlagVisual) == 0) && hack.Serial != 0 && hack.ActionRecord != 0 && kindNetIdOk;
}

std::vector<uint8_t> WriteRewardWord(const RewardWord& word)
{
    nc::proto::Writer writer(static_cast<nc::proto::MsgType>(kRewardWordType));
    writer.PutU8(word.Kind);
    writer.PutU64(word.Reward);
    writer.PutU32(word.Serial);
    writer.PutU32(word.NetId);
    return writer.Data();
}

bool ReadRewardWord(const std::vector<uint8_t>& message, RewardWord& word)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, static_cast<nc::proto::MsgType>(kRewardWordType)))
    {
        return false;
    }
    word.Kind = reader.GetU8();
    word.Reward = reader.GetU64();
    word.Serial = reader.GetU32();
    word.NetId = reader.GetU32();
    const bool kindOk = (word.Kind == kRewardQuest && word.Reward != 0) || (word.Kind == kRewardPartnerDefeat && word.NetId != 0);
    return reader.Ok() && reader.Remaining() == 0 && kindOk;
}

std::vector<uint8_t> WritePlayerHit(const PlayerHit& hit)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerHit);
    writer.PutU32(hit.HitNumber);
    writer.PutU32(hit.AttackerNetId);
    writer.PutU64(hit.SessionTimeMs);
    PutDetails(writer, hit.Details);
    return writer.Data();
}

bool ReadPlayerHit(const std::vector<uint8_t>& message, PlayerHit& hit)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::PlayerHit))
    {
        return false;
    }
    hit.HitNumber = reader.GetU32();
    hit.AttackerNetId = reader.GetU32();
    hit.SessionTimeMs = reader.GetU64();
    return reader.Ok() && GetDetails(reader, hit.Details) && reader.Remaining() == 0;
}
}
