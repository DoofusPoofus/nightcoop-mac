#include "net/WorldMessages.hpp"

#include <ProtocolCore.hpp>

#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr size_t LiftSyncSize = 5 + 53;
}

std::vector<uint8_t> WriteLiftSync(const LiftSync& lift)
{
    nc::proto::Writer writer(nc::proto::MsgType::LiftSync);
    writer.PutU8(lift.Kind);
    writer.PutU32(lift.Peer);
    writer.PutU64(lift.LiftKey);
    writer.PutU8(lift.Flags);
    writer.PutU8(static_cast<uint8_t>(lift.FloorFrom));
    writer.PutU8(static_cast<uint8_t>(lift.FloorTo));
    writer.PutU64(lift.DepartureMs);
    for (float coordinate : lift.Position)
    {
        writer.PutF32(coordinate);
    }
    writer.PutF32(lift.CabZ);
    writer.PutU8(lift.MoveMode);
    writer.PutF32(lift.MoveValue);
    writer.PutU32(lift.DepartureNumber);
    writer.PutU16(lift.RequestNumber);
    writer.PutU8(lift.RequestKind);
    writer.PutU8(lift.Result);
    return writer.Data();
}

bool ReadLiftSync(const std::vector<uint8_t>& message, LiftSync& lift)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (message.size() != LiftSyncSize || reader.ReadHeader() != nc::proto::MsgType::LiftSync)
    {
        return false;
    }
    lift.Kind = reader.GetU8();
    lift.Peer = reader.GetU32();
    lift.LiftKey = reader.GetU64();
    lift.Flags = reader.GetU8();
    lift.FloorFrom = static_cast<int8_t>(reader.GetU8());
    lift.FloorTo = static_cast<int8_t>(reader.GetU8());
    lift.DepartureMs = reader.GetU64();
    for (float& coordinate : lift.Position)
    {
        coordinate = reader.GetF32();
    }
    lift.CabZ = reader.GetF32();
    lift.MoveMode = reader.GetU8();
    lift.MoveValue = reader.GetF32();
    lift.DepartureNumber = reader.GetU32();
    lift.RequestNumber = reader.GetU16();
    lift.RequestKind = reader.GetU8();
    lift.Result = reader.GetU8();
    if (!reader.Ok() || lift.Kind < kLiftPresence || lift.Kind > kLiftReply || (lift.Flags & 0xA0) != 0)
    {
        return false;
    }
    if (((lift.Flags & 0x08) && !(lift.Flags & 0x01)) || ((lift.Flags & 0x40) && lift.DepartureNumber == 0))
    {
        return false;
    }
    for (float coordinate : lift.Position)
    {
        if (!std::isfinite(coordinate) || std::fabs(coordinate) > 100000.0f)
        {
            return false;
        }
    }
    if (!std::isfinite(lift.CabZ) || lift.MoveMode > 2 || !std::isfinite(lift.MoveValue) || lift.MoveValue < 0 || lift.MoveValue > 10000.0f ||
        lift.Result > 7)
    {
        return false;
    }
    if (lift.Kind == kLiftDeparture && (lift.FloorTo < 0 || lift.DepartureMs == 0))
    {
        return false;
    }
    return lift.Kind != kLiftRequest || (lift.RequestNumber != 0 && lift.RequestKind >= 1 && lift.RequestKind <= 3);
}

uint64_t WorldFileHash(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (uint8_t byte : bytes)
    {
        hash = (hash ^ byte) * 0x100000001b3ULL;
    }
    return hash;
}

std::vector<uint8_t> WriteWorldWant(const WorldWant& want)
{
    nc::proto::Writer writer(nc::proto::MsgType::WorldWant);
    writer.PutU8(want.Reason);
    writer.PutU32(want.WorldNumber);
    return writer.Data();
}

bool ReadWorldWant(const std::vector<uint8_t>& message, WorldWant& want)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::WorldWant)
    {
        return false;
    }
    want.Reason = reader.GetU8();
    want.WorldNumber = reader.GetU32();
    return reader.Ok() && reader.Remaining() == 0 && want.Reason >= 1 && want.Reason <= 5;
}

std::vector<uint8_t> WriteWorldFile(const WorldFile& file)
{
    nc::proto::Writer writer(nc::proto::MsgType::WorldFile);
    writer.PutU32(file.TransferId);
    writer.PutU8(file.Kind);
    writer.PutU8(file.Purpose);
    writer.PutU32(file.TotalSize);
    writer.PutU64(file.Hash);
    writer.PutU32(file.Offset);
    writer.PutU32(static_cast<uint32_t>(file.Bytes.size()));
    writer.PutBytes(file.Bytes);
    return writer.Data();
}

bool ReadWorldFile(const std::vector<uint8_t>& message, WorldFile& file)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::WorldFile)
    {
        return false;
    }
    file.TransferId = reader.GetU32();
    file.Kind = reader.GetU8();
    file.Purpose = reader.GetU8();
    file.TotalSize = reader.GetU32();
    file.Hash = reader.GetU64();
    file.Offset = reader.GetU32();
    const uint32_t length = reader.GetU32();
    const uint32_t largest = file.Kind == kWorldFileSave ? 0x8000000u : 0x400000u;
    if (!reader.Ok() || file.TransferId == 0 || (file.Kind != kWorldFileSave && file.Kind != kWorldFileMetadata) || file.Purpose < 1 ||
        file.Purpose > 5 || file.TotalSize == 0 || file.TotalSize > largest || length > kWorldChunkBytes ||
        static_cast<uint64_t>(file.Offset) + length > file.TotalSize || reader.Remaining() != length)
    {
        return false;
    }
    file.Bytes = reader.GetRest();
    return reader.Ok();
}

namespace
{
std::vector<uint8_t> WriteCodeValueText(nc::proto::MsgType type, const WorldStatus& status, size_t longestText)
{
    nc::proto::Writer writer(type);
    writer.PutU8(status.Code);
    writer.PutU32(status.Value);
    writer.PutString(status.Text.size() > longestText ? status.Text.substr(0, longestText) : status.Text);
    return writer.Data();
}

bool ReadCodeValueText(const std::vector<uint8_t>& message, nc::proto::MsgType type, uint8_t highestCode, size_t longestText, WorldStatus& status)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != type)
    {
        return false;
    }
    status.Code = reader.GetU8();
    status.Value = reader.GetU32();
    status.Text = reader.GetString();
    return reader.Ok() && reader.Remaining() == 0 && status.Code >= 1 && status.Code <= highestCode && status.Text.size() <= longestText;
}
}

std::vector<uint8_t> WriteWorldStatus(const WorldStatus& status)
{
    return WriteCodeValueText(nc::proto::MsgType::WorldStatus, status, 512);
}

bool ReadWorldStatus(const std::vector<uint8_t>& message, WorldStatus& status)
{
    return ReadCodeValueText(message, nc::proto::MsgType::WorldStatus, 9, 512, status);
}

std::vector<uint8_t> WriteWorldReload(const WorldStatus& reload)
{
    return WriteCodeValueText(nc::proto::MsgType::WorldReload, reload, 128);
}

bool ReadWorldReload(const std::vector<uint8_t>& message, WorldStatus& reload)
{
    return ReadCodeValueText(message, nc::proto::MsgType::WorldReload, 5, 128, reload);
}
}
