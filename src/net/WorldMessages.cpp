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
}
