#include "net/LookMessages.hpp"

#include <ProtocolCore.hpp>

namespace nightcoop_mac::net
{
namespace
{
constexpr uint8_t LookVersion = 1;
constexpr uint16_t MostGroups = 128;
constexpr uint8_t MostEntries = 255;

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
}
