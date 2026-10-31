#include "net/PlayerExtrasMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <bitset>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr size_t CarryBytes = 69;
constexpr size_t CarryWithLookBytes = 77;
constexpr uint8_t MostCarryKind = 8;
constexpr uint8_t MostPickupAnimation = 16;
constexpr float MostStatValue = 10000.0f;
constexpr size_t MostContactLength = 96;

const char* const BuildStatNames[kBuildStatCount] = {"Strength",
                                                     "Reflexes",
                                                     "TechnicalAbility",
                                                     "Intelligence",
                                                     "Cool",
                                                     "StrengthSkillcheckBonus",
                                                     "TechnicalAbilitySkillcheckBonus",
                                                     "IntelligenceSkillcheckBonus",
                                                     "Level",
                                                     "StreetCred",
                                                     "Hacking"};
}

bool CarryKindIsActive(uint8_t kind)
{
    return kind == kCarryPickup || kind == kCarryHolding || kind == kCarryJump || kind == kCarryAim;
}

std::vector<uint8_t> WritePlayerCarry(const PlayerCarry& carry)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerCarry);
    writer.PutU32(carry.CarryNumber);
    writer.PutU64(carry.SenderClockMs);
    writer.PutU8(carry.Kind);
    writer.PutU8(carry.Spare);
    writer.PutU8(carry.PickupAnimation);
    writer.PutU8(carry.Flags);
    writer.PutU32(carry.NetId);
    writer.PutU64(carry.EntityHash);
    writer.PutU64(carry.Record);
    writer.PutU64(carry.CommunityKeyLow);
    writer.PutU64(carry.CommunityKeyHigh);
    for (float coordinate : carry.Position)
    {
        writer.PutF32(coordinate);
    }
    if (carry.HasLook)
    {
        writer.PutU64(carry.Look);
    }
    return writer.Data();
}

bool ReadPlayerCarry(const std::vector<uint8_t>& message, PlayerCarry& carry)
{
    if (message.size() != CarryBytes && message.size() != CarryWithLookBytes)
    {
        return false;
    }
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::PlayerCarry)
    {
        return false;
    }
    carry.CarryNumber = reader.GetU32();
    carry.SenderClockMs = reader.GetU64();
    carry.Kind = reader.GetU8();
    carry.Spare = reader.GetU8();
    carry.PickupAnimation = reader.GetU8();
    carry.Flags = reader.GetU8();
    carry.NetId = reader.GetU32();
    carry.EntityHash = reader.GetU64();
    carry.Record = reader.GetU64();
    carry.CommunityKeyLow = reader.GetU64();
    carry.CommunityKeyHigh = reader.GetU64();
    for (float& coordinate : carry.Position)
    {
        coordinate = reader.GetF32();
    }
    carry.HasLook = message.size() == CarryWithLookBytes;
    carry.Look = carry.HasLook ? reader.GetU64() : 0;
    const bool positionFinite = std::all_of(carry.Position, carry.Position + 3, [](float value) { return std::isfinite(value); });
    return reader.Ok() && reader.Remaining() == 0 && carry.Kind <= MostCarryKind && carry.Spare <= MostCarryKind &&
           carry.PickupAnimation <= MostPickupAnimation && (carry.Flags & 0x80) == 0 && (carry.NetId != 0 || carry.EntityHash != 0) && positionFinite;
}

const char* BuildStatName(uint8_t index)
{
    return index < kBuildStatCount ? BuildStatNames[index] : "?";
}

std::vector<uint8_t> WriteBuildStats(const BuildStats& stats)
{
    nc::proto::Writer writer(nc::proto::MsgType::BuildStats);
    writer.PutU32(stats.Revision);
    writer.PutU8(stats.Flags);
    const auto count = static_cast<uint8_t>(std::min(stats.Stats.size(), kMostBuildStats));
    writer.PutU8(count);
    for (uint8_t position = 0; position < count; ++position)
    {
        writer.PutU8(stats.Stats[position].Index);
        writer.PutF32(stats.Stats[position].Value);
    }
    return writer.Data();
}

bool ReadBuildStats(const std::vector<uint8_t>& message, BuildStats& stats)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::BuildStats)
    {
        return false;
    }
    stats.Revision = reader.GetU32();
    stats.Flags = reader.GetU8();
    const uint8_t count = reader.GetU8();
    if (!reader.Ok() || (stats.Flags & 0xFC) != 0 || count > kMostBuildStats)
    {
        return false;
    }
    std::bitset<256> seenIndexes;
    stats.Stats.clear();
    for (uint8_t position = 0; position < count; ++position)
    {
        BuildStat stat;
        stat.Index = reader.GetU8();
        stat.Value = reader.GetF32();
        if (!reader.Ok() || !std::isfinite(stat.Value) || stat.Value < 0.0f || stat.Value > MostStatValue || seenIndexes.test(stat.Index))
        {
            return false;
        }
        seenIndexes.set(stat.Index);
        stats.Stats.push_back(stat);
    }
    return reader.Remaining() == 0;
}

bool PhoneContactNameOk(const std::string& contact)
{
    if (contact.empty() || contact.size() > MostContactLength || contact == "player" || contact == "Player")
    {
        return false;
    }
    return std::all_of(contact.begin(), contact.end(), [](char letter) { return letter >= 0x20 && letter <= 0x7E; });
}

std::vector<uint8_t> WritePhoneSync(const PhoneSync& phone)
{
    nc::proto::Writer writer(nc::proto::MsgType::PhoneSync);
    writer.PutU8(phone.Kind);
    writer.PutU8(phone.Action);
    writer.PutU32(phone.AskingPeer);
    writer.PutU32(phone.RequestNumber);
    writer.PutU32(phone.CommitNumber);
    writer.PutU32(phone.CallId);
    writer.PutU64(phone.HostClockMs);
    writer.PutString(phone.Contact);
    writer.PutU8(phone.CallPhase);
    writer.PutU8(phone.CallMode);
    writer.PutU8(phone.Visuals);
    writer.PutU8(phone.Flags);
    return writer.Data();
}

bool ReadPhoneSync(const std::vector<uint8_t>& message, PhoneSync& phone)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::PhoneSync)
    {
        return false;
    }
    phone.Kind = reader.GetU8();
    phone.Action = reader.GetU8();
    phone.AskingPeer = reader.GetU32();
    phone.RequestNumber = reader.GetU32();
    phone.CommitNumber = reader.GetU32();
    phone.CallId = reader.GetU32();
    phone.HostClockMs = reader.GetU64();
    phone.Contact = reader.GetString();
    phone.CallPhase = reader.GetU8();
    phone.CallMode = reader.GetU8();
    phone.Visuals = reader.GetU8();
    phone.Flags = reader.GetU8();
    return reader.Ok() && reader.Remaining() == 0 && phone.Kind >= kPhoneRequest && phone.Kind <= kPhoneRefused && phone.Action >= kPhoneActionCall &&
           phone.Action <= kPhoneActionPickup && PhoneContactNameOk(phone.Contact);
}
}
