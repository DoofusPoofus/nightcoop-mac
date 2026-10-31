#include "net/DeviceMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr float LongestDuration = 3600.0f;
constexpr uint64_t SharedBlastKeyBits = 0xFFFFFFFFFF000000ULL;

bool StateFitsTheWire(int8_t state)
{
    return (state >= kDeviceDisabled && state <= 2) || state == kDeviceStateUnknown;
}

void PutValue(nc::proto::Writer& writer, uint64_t key, uint64_t typeName, const std::vector<uint8_t>& bytes)
{
    writer.PutU64(key);
    writer.PutU64(typeName);
    const size_t length = std::min(bytes.size(), kMostDeviceValueBytes);
    writer.PutU8(static_cast<uint8_t>(length));
    for (size_t index = 0; index < length; ++index)
    {
        writer.PutU8(bytes[index]);
    }
}

bool ReadValueBytes(nc::proto::Reader& reader, std::vector<uint8_t>& bytes)
{
    const uint8_t length = reader.GetU8();
    if (!reader.Ok() || length == 0 || length > kMostDeviceValueBytes)
    {
        return false;
    }
    bytes.resize(length);
    reader.GetBytes(bytes.data(), length);
    return reader.Ok();
}
}

bool DeviceStateKnown(int8_t state)
{
    return state >= kDeviceDisabled && state <= kDeviceOn;
}

std::vector<uint8_t> WriteDeviceSync(const DeviceSync& sync)
{
    nc::proto::Writer writer(nc::proto::MsgType::DeviceSync);
    writer.PutU8(sync.Kind);
    writer.PutU32(sync.Peer);
    writer.PutU64(sync.EntityHash);
    writer.PutU64(sync.ComponentName);
    writer.PutU64(sync.PsClass);
    writer.PutU8(sync.ActionKind);
    writer.PutU8(sync.Skill);
    writer.PutU8(static_cast<uint8_t>(sync.StateBefore));
    writer.PutU8(static_cast<uint8_t>(sync.StateAfter));
    writer.PutU8(sync.Flags);
    writer.PutU64(sync.Version);
    writer.PutU16(sync.RequestNumber);
    writer.PutU32(sync.MsSinceAction);
    writer.PutU64(sync.ActionClass);
    writer.PutF32(sync.Duration);
    writer.PutU8(sync.HasProp ? 1 : 0);
    writer.PutU64(sync.PropName);
    writer.PutU64(sync.PropTypeName);
    writer.PutU8(sync.PropFlags);
    const size_t fieldCount = std::min(sync.Fields.size(), kMostDeviceFields);
    writer.PutU8(static_cast<uint8_t>(fieldCount));
    for (size_t index = 0; index < fieldCount; ++index)
    {
        PutValue(writer, sync.Fields[index].Name, sync.Fields[index].TypeName, sync.Fields[index].Bytes);
    }
    const size_t valueCount = std::min(sync.PropValues.size(), kMostDevicePropValues);
    writer.PutU8(static_cast<uint8_t>(valueCount));
    for (size_t index = 0; index < valueCount; ++index)
    {
        PutValue(writer, sync.PropValues[index].Index, sync.PropValues[index].TypeName, sync.PropValues[index].Bytes);
    }
    return writer.Data();
}

bool ReadDeviceSync(const std::vector<uint8_t>& message, DeviceSync& sync)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::DeviceSync)
    {
        return false;
    }
    sync.Kind = reader.GetU8();
    sync.Peer = reader.GetU32();
    sync.EntityHash = reader.GetU64();
    sync.ComponentName = reader.GetU64();
    sync.PsClass = reader.GetU64();
    sync.ActionKind = reader.GetU8();
    sync.Skill = reader.GetU8();
    sync.StateBefore = static_cast<int8_t>(reader.GetU8());
    sync.StateAfter = static_cast<int8_t>(reader.GetU8());
    sync.Flags = reader.GetU8();
    sync.Version = reader.GetU64();
    sync.RequestNumber = reader.GetU16();
    sync.MsSinceAction = reader.GetU32();
    sync.ActionClass = reader.GetU64();
    sync.Duration = reader.GetF32();
    const uint8_t hasProp = reader.GetU8();
    sync.HasProp = hasProp != 0;
    sync.PropName = reader.GetU64();
    sync.PropTypeName = reader.GetU64();
    sync.PropFlags = reader.GetU8();
    const uint8_t fieldCount = reader.GetU8();
    if (!reader.Ok() || fieldCount > kMostDeviceFields)
    {
        return false;
    }
    sync.Fields.assign(fieldCount, {});
    for (auto& field : sync.Fields)
    {
        field.Name = reader.GetU64();
        field.TypeName = reader.GetU64();
        if (!ReadValueBytes(reader, field.Bytes) || field.Name == 0 || field.TypeName == 0)
        {
            return false;
        }
    }
    const uint8_t valueCount = reader.GetU8();
    if (!reader.Ok() || valueCount > kMostDevicePropValues)
    {
        return false;
    }
    sync.PropValues.assign(valueCount, {});
    for (auto& value : sync.PropValues)
    {
        value.Index = reader.GetU64();
        value.TypeName = reader.GetU64();
        if (!ReadValueBytes(reader, value.Bytes) || value.TypeName == 0 || value.Index > 2)
        {
            return false;
        }
    }
    const bool headerOk = reader.Ok() && reader.Remaining() == 0 && sync.Kind >= kDeviceRequest && sync.Kind <= kDeviceAnswer &&
                          sync.EntityHash != 0 && sync.PsClass != 0 && sync.ActionKind < kDeviceNotShared && sync.Skill <= 3 &&
                          (sync.ActionKind == kDeviceSkillCheck) == (sync.Skill != 0) && StateFitsTheWire(sync.StateBefore) &&
                          StateFitsTheWire(sync.StateAfter) && (sync.Flags & 0xF8) == 0 && hasProp < 2 && std::isfinite(sync.Duration) &&
                          sync.Duration >= 0.0f && sync.Duration <= LongestDuration;
    if (!headerOk)
    {
        return false;
    }
    if ((sync.ActionKind == kDeviceSameAction || sync.ActionKind == kDeviceSameActionHostOnly) && sync.ActionClass == 0)
    {
        return false;
    }
    if (sync.Kind == kDeviceReplay && sync.ActionKind == kDeviceSameActionHostOnly)
    {
        return false;
    }
    return sync.Kind == kDeviceRequest ? sync.RequestNumber != 0 : sync.Version != 0;
}

std::vector<uint8_t> WriteTurretWord(const TurretWord& word)
{
    nc::proto::Writer writer(nc::proto::MsgType::TurretWord);
    writer.PutU8(word.Kind);
    writer.PutU64(word.TurretKey);
    writer.PutU8(word.State);
    return writer.Data();
}

bool ReadTurretWord(const std::vector<uint8_t>& message, TurretWord& word)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::TurretWord)
    {
        return false;
    }
    word.Kind = reader.GetU8();
    word.TurretKey = reader.GetU64();
    word.State = reader.GetU8();
    return reader.Ok() && (word.Kind == kTurretAsk || word.Kind == kTurretAnswer) && word.TurretKey != 0 && word.State <= kTurretInCombat &&
           !(word.Kind == kTurretAsk && word.State != kTurretNotThere);
}

std::vector<uint8_t> WriteDeviceBlast(const DeviceBlast& blast)
{
    nc::proto::Writer writer(nc::proto::MsgType::DeviceBlast);
    writer.PutU8(blast.Kind);
    const size_t count = std::min(blast.Devices.size(), kMostBlastsPerMessage);
    writer.PutU8(static_cast<uint8_t>(count));
    for (size_t index = 0; index < count; ++index)
    {
        const auto& device = blast.Devices[index];
        writer.PutU64(device.DeviceKey);
        for (float coordinate : device.Position)
        {
            writer.PutF32(coordinate);
        }
        writer.PutU8(device.ByOurPlayer);
    }
    return writer.Data();
}

bool ReadDeviceBlast(const std::vector<uint8_t>& message, DeviceBlast& blast)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::DeviceBlast)
    {
        return false;
    }
    blast.Kind = reader.GetU8();
    const uint8_t count = reader.GetU8();
    if (!reader.Ok() || count > kMostBlastsPerMessage || (blast.Kind & 0xFE) != 0)
    {
        return false;
    }
    blast.Devices.assign(count, {});
    for (auto& device : blast.Devices)
    {
        device.DeviceKey = reader.GetU64();
        for (float& coordinate : device.Position)
        {
            coordinate = reader.GetF32();
        }
        device.ByOurPlayer = reader.GetU8();
        if (!reader.Ok() || !BlastKeyIsShared(device.DeviceKey) || (device.ByOurPlayer & 0xFE) != 0 ||
            !std::all_of(device.Position, device.Position + 3, [](float value) { return std::isfinite(value); }))
        {
            return false;
        }
    }
    return reader.Ok() && reader.Remaining() == 0;
}

bool BlastKeyIsShared(uint64_t deviceKey)
{
    return (deviceKey & SharedBlastKeyBits) != 0;
}
}
