#include "net/GameMessages.hpp"

#include <ProtocolCore.hpp>

#include <cstring>

namespace nightcoop_mac::net
{
namespace
{
constexpr uint16_t MaxAnimEntries = 4096;
constexpr uint8_t MaxFeatureFields = 48;
constexpr uint8_t MaxFeatureFieldBytes = 32;
constexpr size_t SmallestAnimEntry = 10;
constexpr uint16_t MaxAnimSets = 512;

bool OpenMessage(nc::proto::Reader& reader, nc::proto::MsgType expectedType)
{
    return reader.ReadHeader() == expectedType && reader.Ok();
}

void PutFloat(nc::proto::Writer& writer, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writer.PutU32(bits);
}

float GetFloat(nc::proto::Reader& reader)
{
    const uint32_t bits = reader.GetU32();
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
}

uint8_t MessageTypeOf(const std::vector<uint8_t>& message)
{
    nc::proto::Reader reader(message.data(), message.size());
    const auto type = reader.ReadHeader();
    return reader.Ok() ? static_cast<uint8_t>(type) : 0;
}

std::vector<uint8_t> WriteGameEnvelope(const GameEnvelope& envelope)
{
    nc::proto::Writer writer(nc::proto::MsgType::Game);
    writer.PutU32(static_cast<uint32_t>(envelope.ToPeer));
    writer.PutU32(envelope.FromPeer);
    writer.PutU8(envelope.Reliable ? 1 : 0);
    writer.PutBytes(envelope.Inner);
    return writer.Data();
}

bool ReadGameEnvelope(const std::vector<uint8_t>& message, GameEnvelope& envelope)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::Game))
    {
        return false;
    }
    envelope.ToPeer = static_cast<int32_t>(reader.GetU32());
    envelope.FromPeer = reader.GetU32();
    envelope.Reliable = reader.GetU8() != 0;
    envelope.Inner = reader.GetRest();
    return reader.Ok() && !envelope.Inner.empty();
}

std::vector<uint8_t> WriteAnimBatch(const AnimBatch& batch)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerAnim);
    writer.PutU32(batch.Unknown00);
    writer.PutU8(static_cast<uint8_t>((batch.FullState ? 1 : 0) | (batch.EntriesHaveExtra ? 2 : 0)));
    writer.PutU64(batch.Unknown08);
    writer.PutU32(batch.Unknown10);
    const uint16_t count = static_cast<uint16_t>(batch.Entries.size() < MaxAnimEntries ? batch.Entries.size() : MaxAnimEntries);
    writer.PutU16(count);
    for (uint16_t index = 0; index < count; ++index)
    {
        const auto& entry = batch.Entries[index];
        writer.PutU8(entry.Slot);
        writer.PutU8(static_cast<uint8_t>(entry.Kind));
        writer.PutU64(entry.Name);
        switch (entry.Kind)
        {
        case AnimKind::Feature:
        {
            writer.PutU64(entry.FeatureType);
            const uint8_t fields = static_cast<uint8_t>(entry.Fields.size() < MaxFeatureFields ? entry.Fields.size() : MaxFeatureFields);
            writer.PutU8(fields);
            for (uint8_t field = 0; field < fields; ++field)
            {
                const auto& value = entry.Fields[field];
                writer.PutU64(value.Name);
                const uint8_t size = static_cast<uint8_t>(value.Bytes.size() < MaxFeatureFieldBytes ? value.Bytes.size() : MaxFeatureFieldBytes);
                writer.PutU8(size);
                for (uint8_t byte = 0; byte < size; ++byte)
                {
                    writer.PutU8(value.Bytes[byte]);
                }
            }
            writer.PutU8(entry.Extra);
            break;
        }
        case AnimKind::Float:
        case AnimKind::Wrapper: PutFloat(writer, entry.Values[0]); break;
        case AnimKind::Int:
        case AnimKind::Bool: writer.PutU32(static_cast<uint32_t>(entry.IntValue)); break;
        case AnimKind::Vector:
        case AnimKind::Quaternion:
            for (float value : entry.Values)
            {
                PutFloat(writer, value);
            }
            break;
        case AnimKind::Event: break;
        case AnimKind::Attach:
        case AnimKind::Detach:
            writer.PutU64(entry.AttachFirst);
            writer.PutU64(entry.AttachSecond);
            break;
        }
        if (batch.EntriesHaveExtra && entry.Kind != AnimKind::Feature)
        {
            writer.PutU8(entry.Extra);
        }
    }
    return writer.Data();
}

bool ReadAnimBatch(const std::vector<uint8_t>& message, AnimBatch& batch)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::PlayerAnim))
    {
        return false;
    }
    batch = AnimBatch {};
    batch.Unknown00 = reader.GetU32();
    const uint8_t flags = reader.GetU8();
    batch.FullState = flags & 1;
    batch.EntriesHaveExtra = flags & 2;
    batch.Unknown08 = reader.GetU64();
    batch.Unknown10 = reader.GetU32();
    const uint16_t count = reader.GetU16();
    if (!reader.Ok() || count > MaxAnimEntries || static_cast<size_t>(count) * SmallestAnimEntry > reader.Remaining())
    {
        return false;
    }
    batch.Entries.reserve(count);
    for (uint16_t index = 0; index < count; ++index)
    {
        AnimEntry entry;
        entry.Slot = reader.GetU8();
        const uint8_t kind = reader.GetU8();
        entry.Name = reader.GetU64();
        if (!reader.Ok() || kind > static_cast<uint8_t>(AnimKind::Detach))
        {
            return false;
        }
        entry.Kind = static_cast<AnimKind>(kind);
        switch (entry.Kind)
        {
        case AnimKind::Feature:
        {
            entry.FeatureType = reader.GetU64();
            const uint8_t fields = reader.GetU8();
            if (!reader.Ok() || fields > MaxFeatureFields)
            {
                return false;
            }
            for (uint8_t field = 0; field < fields; ++field)
            {
                AnimFeatureField value;
                value.Name = reader.GetU64();
                const uint8_t size = reader.GetU8();
                if (!reader.Ok() || size > MaxFeatureFieldBytes)
                {
                    return false;
                }
                value.Bytes.resize(size);
                reader.GetBytes(value.Bytes.data(), size);
                entry.Fields.push_back(std::move(value));
            }
            entry.Extra = reader.GetU8();
            break;
        }
        case AnimKind::Float:
        case AnimKind::Wrapper: entry.Values[0] = GetFloat(reader); break;
        case AnimKind::Int:
        case AnimKind::Bool: entry.IntValue = static_cast<int32_t>(reader.GetU32()); break;
        case AnimKind::Vector:
        case AnimKind::Quaternion:
            for (float& value : entry.Values)
            {
                value = GetFloat(reader);
            }
            break;
        case AnimKind::Event: break;
        case AnimKind::Attach:
        case AnimKind::Detach:
            entry.AttachFirst = reader.GetU64();
            entry.AttachSecond = reader.GetU64();
            if (entry.AttachFirst == 0)
            {
                return false;
            }
            break;
        }
        if (batch.EntriesHaveExtra && entry.Kind != AnimKind::Feature)
        {
            entry.Extra = reader.GetU8();
        }
        if (!reader.Ok())
        {
            return false;
        }
        batch.Entries.push_back(std::move(entry));
    }
    return true;
}

std::vector<uint8_t> WriteAnimWant(const AnimWant& want)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerAnimWant);
    writer.PutU8(want.First ? 1 : 0);
    writer.PutU8(want.Second ? 1 : 0);
    return writer.Data();
}

bool ReadAnimWant(const std::vector<uint8_t>& message, AnimWant& want)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::PlayerAnimWant))
    {
        return false;
    }
    want.First = reader.GetU8() != 0;
    want.Second = reader.GetU8() != 0;
    return reader.Ok();
}

bool ReadAnimSetup(const std::vector<uint8_t>& message, AnimSetup& setup)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::PlayerAnimSetup))
    {
        return false;
    }
    setup.Rig = reader.GetU64();
    setup.SetCount = reader.GetU16();
    return reader.Ok() && setup.SetCount <= MaxAnimSets;
}
}
