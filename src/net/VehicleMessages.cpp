#include "net/VehicleMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr size_t MostOccupants = 8;
constexpr float MostSeatOffsetSquared = 100.0f;

bool OpenMessage(nc::proto::Reader& reader, nc::proto::MsgType expectedType)
{
    return reader.ReadHeader() == expectedType && reader.Ok();
}

void PutFloats(nc::proto::Writer& writer, const float* values, size_t count)
{
    for (size_t index = 0; index < count; ++index)
    {
        writer.PutF32(values[index]);
    }
}

bool GetFloats(nc::proto::Reader& reader, float* values, size_t count)
{
    for (size_t index = 0; index < count; ++index)
    {
        values[index] = reader.GetF32();
        if (!std::isfinite(values[index]))
        {
            return false;
        }
    }
    return reader.Ok();
}
}

std::vector<uint8_t> WriteVehicleSpawn(const VehicleSpawn& spawn)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleSpawn);
    writer.PutU32(spawn.NetId);
    writer.PutU32(spawn.HomePeer);
    writer.PutU64(spawn.Record);
    writer.PutU64(spawn.Appearance);
    writer.PutU64(spawn.OwnerEntity);
    writer.PutU8(std::clamp<uint8_t>(spawn.Kind, 1, 5));
    writer.PutU8(spawn.Flags);
    for (uint8_t door : spawn.Doors)
    {
        writer.PutU8(std::min<uint8_t>(door, 4));
    }
    PutFloats(writer, spawn.Position, 3);
    PutFloats(writer, spawn.Orientation, 4);
    return writer.Data();
}

bool ReadVehicleSpawn(const std::vector<uint8_t>& message, VehicleSpawn& spawn)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleSpawn))
    {
        return false;
    }
    spawn.NetId = reader.GetU32();
    spawn.HomePeer = reader.GetU32();
    spawn.Record = reader.GetU64();
    spawn.Appearance = reader.GetU64();
    spawn.OwnerEntity = reader.GetU64();
    spawn.Kind = reader.GetU8();
    spawn.Flags = reader.GetU8();
    for (uint8_t& door : spawn.Doors)
    {
        door = reader.GetU8();
        if (door >= 5)
        {
            return false;
        }
    }
    return reader.Ok() && spawn.NetId != 0 && spawn.Record != 0 && spawn.Kind >= 1 && spawn.Kind <= 5 &&
           GetFloats(reader, spawn.Position, 3) && GetFloats(reader, spawn.Orientation, 4) && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteVehicleState(const VehicleState& state)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleState);
    writer.PutU32(state.NetId);
    writer.PutU64(state.StampMs);
    PutFloats(writer, state.Position, 3);
    PutFloats(writer, state.Orientation, 4);
    PutFloats(writer, state.Velocity, 3);
    writer.PutU8(state.Flags);
    writer.PutU8(std::min<uint8_t>(state.Headlights, 2));
    writer.PutU8(state.OpenDoors);
    writer.PutU8(std::min<uint8_t>(state.Health, 100));
    writer.PutU8(static_cast<uint8_t>(state.Steering));
    const auto count = static_cast<uint8_t>(std::min(state.Occupants.size(), MostOccupants));
    writer.PutU8(count);
    for (uint8_t index = 0; index < count; ++index)
    {
        writer.PutU64(state.Occupants[index].Slot);
        writer.PutU8(state.Occupants[index].Kind);
        writer.PutU32(state.Occupants[index].Id);
    }
    return writer.Data();
}

bool ReadVehicleState(const std::vector<uint8_t>& message, VehicleState& state)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleState))
    {
        return false;
    }
    state.NetId = reader.GetU32();
    state.StampMs = reader.GetU64();
    if (!GetFloats(reader, state.Position, 3) || !GetFloats(reader, state.Orientation, 4) || !GetFloats(reader, state.Velocity, 3))
    {
        return false;
    }
    state.Flags = reader.GetU8();
    state.Headlights = reader.GetU8();
    state.OpenDoors = reader.GetU8();
    state.Health = reader.GetU8();
    state.Steering = static_cast<int8_t>(reader.GetU8());
    const uint8_t count = reader.GetU8();
    if (!reader.Ok() || state.Health > 100 || count > MostOccupants)
    {
        return false;
    }
    state.Occupants.clear();
    for (uint8_t index = 0; index < count; ++index)
    {
        VehicleOccupant occupant;
        occupant.Slot = reader.GetU64();
        occupant.Kind = reader.GetU8();
        occupant.Id = reader.GetU32();
        if (!reader.Ok() || occupant.Slot == 0 || occupant.Kind < 1 || occupant.Kind > 2)
        {
            return false;
        }
        state.Occupants.push_back(occupant);
    }
    return reader.Remaining() == 0;
}

std::vector<uint8_t> WriteVehicleDespawn(const VehicleDespawn& despawn)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleDespawn);
    writer.PutU32(despawn.NetId);
    writer.PutU8(std::clamp<uint8_t>(despawn.Reason, 1, 3));
    return writer.Data();
}

bool ReadVehicleDespawn(const std::vector<uint8_t>& message, VehicleDespawn& despawn)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleDespawn))
    {
        return false;
    }
    despawn.NetId = reader.GetU32();
    despawn.Reason = reader.GetU8();
    return reader.Ok() && despawn.NetId != 0 && despawn.Reason >= 1 && despawn.Reason <= 3 && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteVehicleSeat(const VehicleSeat& seat)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleSeat);
    writer.PutU32(seat.NetId);
    writer.PutU64(seat.Slot);
    PutFloats(writer, seat.Position, 3);
    PutFloats(writer, seat.Orientation, 4);
    return writer.Data();
}

bool ReadVehicleSeat(const std::vector<uint8_t>& message, VehicleSeat& seat)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleSeat))
    {
        return false;
    }
    seat.NetId = reader.GetU32();
    seat.Slot = reader.GetU64();
    if (!GetFloats(reader, seat.Position, 3) || !GetFloats(reader, seat.Orientation, 4) || reader.Remaining() != 0)
    {
        return false;
    }
    const float offsetSquared = seat.Position[0] * seat.Position[0] + seat.Position[1] * seat.Position[1] + seat.Position[2] * seat.Position[2];
    return offsetSquared <= MostSeatOffsetSquared && (seat.NetId == 0 || seat.Slot != 0);
}
}
