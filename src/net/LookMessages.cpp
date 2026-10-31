#include "net/LookMessages.hpp"

#include <ProtocolCore.hpp>

namespace nightcoop_mac::net
{
namespace
{
constexpr uint8_t LookVersion = 1;
constexpr uint16_t MostGroups = 128;
constexpr uint8_t MostEntries = 255;
constexpr uint16_t MostItems = 64;
constexpr uint64_t RecordHashAndLength = 0xFFFFFFFFFFull;

uint8_t Capped(size_t count)
{
    return static_cast<uint8_t>(count < MostEntries ? count : MostEntries);
}

void WriteGroups(nc::proto::Writer& writer, const std::vector<LookGroup>& groups)
{
    const uint16_t groupCount = static_cast<uint16_t>(groups.size() < MostGroups ? groups.size() : MostGroups);
    writer.PutU16(groupCount);
    for (uint16_t index = 0; index < groupCount; ++index)
    {
        const auto& group = groups[index];
        writer.PutU64(group.Name);
        const uint8_t partCount = Capped(group.Parts.size());
        writer.PutU8(partCount);
        for (uint8_t part = 0; part < partCount; ++part)
        {
            writer.PutU64(group.Parts[part].Resource);
            writer.PutU64(group.Parts[part].Definition);
            writer.PutU64(group.Parts[part].Name);
            writer.PutU32(group.Parts[part].CensorFlag);
            writer.PutU8(group.Parts[part].CensorAction);
        }
        const uint8_t morphCount = Capped(group.Morphs.size());
        writer.PutU8(morphCount);
        for (uint8_t morph = 0; morph < morphCount; ++morph)
        {
            writer.PutU64(group.Morphs[morph].RegionName);
            writer.PutU64(group.Morphs[morph].TargetName);
            writer.PutU32(group.Morphs[morph].CensorFlag);
            writer.PutU8(group.Morphs[morph].CensorAction);
        }
    }
}

bool ReadGroups(nc::proto::Reader& reader, std::vector<LookGroup>& groups)
{
    const uint16_t groupCount = reader.GetU16();
    if (!reader.Ok() || groupCount > MostGroups)
    {
        return false;
    }
    groups.resize(groupCount);
    for (auto& group : groups)
    {
        group.Name = reader.GetU64();
        group.Parts.resize(reader.GetU8());
        for (auto& part : group.Parts)
        {
            part.Resource = reader.GetU64();
            part.Definition = reader.GetU64();
            part.Name = reader.GetU64();
            part.CensorFlag = reader.GetU32();
            part.CensorAction = reader.GetU8();
        }
        group.Morphs.resize(reader.GetU8());
        for (auto& morph : group.Morphs)
        {
            morph.RegionName = reader.GetU64();
            morph.TargetName = reader.GetU64();
            morph.CensorFlag = reader.GetU32();
            morph.CensorAction = reader.GetU8();
        }
        if (!reader.Ok())
        {
            return false;
        }
    }
    return true;
}
}

std::vector<uint8_t> WritePlayerCustomization(const PlayerCustomization& look)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerCustomization);
    writer.PutU8(LookVersion);
    writer.PutU32(look.DataVersion);
    writer.PutU8(look.BodyMale ? 1 : 0);
    writer.PutU8(look.BrainMale ? 1 : 0);
    WriteGroups(writer, look.Head);
    WriteGroups(writer, look.Body);
    WriteGroups(writer, look.Arms);
    const uint8_t perspectiveCount = Capped(look.Perspectives.size());
    writer.PutU8(perspectiveCount);
    for (uint8_t index = 0; index < perspectiveCount; ++index)
    {
        writer.PutU64(look.Perspectives[index].Name);
        writer.PutU64(look.Perspectives[index].Fpp);
        writer.PutU64(look.Perspectives[index].Tpp);
    }
    const uint8_t tagCount = Capped(look.Tags.size());
    writer.PutU8(tagCount);
    for (uint8_t index = 0; index < tagCount; ++index)
    {
        writer.PutU64(look.Tags[index]);
    }
    return writer.Data();
}

bool ReadPlayerCustomization(const std::vector<uint8_t>& message, PlayerCustomization& look)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::PlayerCustomization || !reader.Ok() || reader.GetU8() != LookVersion)
    {
        return false;
    }
    look = PlayerCustomization {};
    look.DataVersion = reader.GetU32();
    look.BodyMale = reader.GetU8() != 0;
    look.BrainMale = reader.GetU8() != 0;
    if (!ReadGroups(reader, look.Head) || !ReadGroups(reader, look.Body) || !ReadGroups(reader, look.Arms))
    {
        return false;
    }
    look.Perspectives.resize(reader.GetU8());
    for (auto& perspective : look.Perspectives)
    {
        perspective.Name = reader.GetU64();
        perspective.Fpp = reader.GetU64();
        perspective.Tpp = reader.GetU64();
    }
    look.Tags.resize(reader.GetU8());
    for (auto& tag : look.Tags)
    {
        tag = reader.GetU64();
    }
    return reader.Ok();
}

uint64_t LookFingerprint(const std::vector<uint8_t>& message)
{
    uint64_t hash = 0xcbf29ce484222325;
    for (uint8_t byte : message)
    {
        hash = (hash ^ byte) * 0x100000001b3;
    }
    return hash;
}

std::vector<uint8_t> WritePlayerClothes(const PlayerClothes& clothes)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerLook);
    writer.PutU8(clothes.Female ? 1 : 0);
    const uint16_t itemCount = static_cast<uint16_t>(clothes.Items.size() < MostItems ? clothes.Items.size() : MostItems);
    writer.PutU16(itemCount);
    for (uint16_t index = 0; index < itemCount; ++index)
    {
        const auto& item = clothes.Items[index];
        writer.PutU64(item.Record);
        writer.PutU32(item.RngSeed);
        writer.PutU16(item.UniqueCounter);
        writer.PutU8(item.Structure);
        writer.PutU8(item.Flags);
    }
    writer.PutU64(clothes.Arms);
    const bool slotsFit = clothes.Slots.size() == clothes.Items.size();
    writer.PutU16(slotsFit ? itemCount : 0);
    for (uint16_t index = 0; slotsFit && index < itemCount; ++index)
    {
        writer.PutU64(clothes.Slots[index]);
    }
    return writer.Data();
}

bool ReadPlayerClothes(const std::vector<uint8_t>& message, PlayerClothes& clothes)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::PlayerLook || !reader.Ok())
    {
        return false;
    }
    clothes = PlayerClothes {};
    clothes.Female = reader.GetU8() != 0;
    const uint16_t itemCount = reader.GetU16();
    if (!reader.Ok() || itemCount > MostItems)
    {
        return false;
    }
    clothes.Items.resize(itemCount);
    for (auto& item : clothes.Items)
    {
        item.Record = reader.GetU64();
        item.RngSeed = reader.GetU32();
        item.UniqueCounter = reader.GetU16();
        item.Structure = reader.GetU8();
        item.Flags = reader.GetU8();
    }
    clothes.Arms = reader.GetU64();
    if (!reader.Ok())
    {
        return false;
    }
    if (reader.Remaining() >= 2)
    {
        const uint16_t slotCount = reader.GetU16();
        std::vector<uint64_t> slots(slotCount);
        for (auto& slot : slots)
        {
            slot = reader.GetU64();
        }
        if (reader.Ok() && slotCount == itemCount)
        {
            clothes.Slots = std::move(slots);
        }
    }
    return true;
}

uint64_t RecordWithoutOffset(uint64_t record)
{
    return record & RecordHashAndLength;
}
}
