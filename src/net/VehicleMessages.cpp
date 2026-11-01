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

namespace
{
void PutStateBody(nc::proto::Writer& writer, const VehicleState& state)
{
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
}

bool GetStateBody(nc::proto::Reader& reader, VehicleState& state)
{
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
    return true;
}
}

std::vector<uint8_t> WriteVehicleState(const VehicleState& state)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleState);
    PutStateBody(writer, state);
    return writer.Data();
}

bool ReadVehicleState(const std::vector<uint8_t>& message, VehicleState& state)
{
    nc::proto::Reader reader(message.data(), message.size());
    return OpenMessage(reader, nc::proto::MsgType::VehicleState) && GetStateBody(reader, state) && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteVehicleHandover(const VehicleHandover& handover)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleHandover);
    writer.PutU32(handover.NetId);
    writer.PutU32(handover.NewOwner);
    writer.PutU8(handover.Reason);
    PutStateBody(writer, handover.State);
    return writer.Data();
}

bool ReadVehicleHandover(const std::vector<uint8_t>& message, VehicleHandover& handover)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleHandover))
    {
        return false;
    }
    handover.NetId = reader.GetU32();
    handover.NewOwner = reader.GetU32();
    handover.Reason = reader.GetU8();
    return reader.Ok() && handover.Reason >= 1 && handover.Reason <= 3 && GetStateBody(reader, handover.State) && reader.Remaining() == 0 &&
           handover.State.NetId == handover.NetId;
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

std::vector<uint8_t> WriteVehicleHit(const VehicleHit& hit)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleHit);
    writer.PutU32(hit.NetId);
    writer.PutU32(hit.HitPeer);
    writer.PutU64(hit.ClockMs);
    for (float part : hit.Direction)
    {
        writer.PutF32(part);
    }
    for (float part : hit.Impulse)
    {
        writer.PutF32(part);
    }
    writer.PutF32(hit.Speed);
    return writer.Data();
}

bool ReadVehicleHit(const std::vector<uint8_t>& message, VehicleHit& hit)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleHit))
    {
        return false;
    }
    hit.NetId = reader.GetU32();
    hit.HitPeer = reader.GetU32();
    hit.ClockMs = reader.GetU64();
    if (!GetFloats(reader, hit.Direction, 3) || !GetFloats(reader, hit.Impulse, 3) || !GetFloats(reader, &hit.Speed, 1) || reader.Remaining() != 0)
    {
        return false;
    }
    const float directionSquared = hit.Direction[0] * hit.Direction[0] + hit.Direction[1] * hit.Direction[1] + hit.Direction[2] * hit.Direction[2];
    const float impulseSquared = hit.Impulse[0] * hit.Impulse[0] + hit.Impulse[1] * hit.Impulse[1] + hit.Impulse[2] * hit.Impulse[2];
    return directionSquared <= 1e4f && impulseSquared <= 1e6f && hit.Speed >= 0 && hit.Speed <= 200;
}

std::vector<uint8_t> WriteVehicleDamage(const VehicleDamage& damage)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleDamage);
    writer.PutU32(damage.NetId);
    writer.PutU8(damage.Flags & 1);
    writer.PutU8(damage.TireIndex);
    PutFloats(writer, damage.Grid, 30);
    return writer.Data();
}

bool ReadVehicleDamage(const std::vector<uint8_t>& message, VehicleDamage& damage)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleDamage))
    {
        return false;
    }
    damage.NetId = reader.GetU32();
    damage.Flags = reader.GetU8();
    damage.TireIndex = reader.GetU8();
    if (!GetFloats(reader, damage.Grid, 30) || reader.Remaining() != 0 || (damage.Flags & ~1u) != 0 || ((damage.Flags & 1) && damage.TireIndex > 7))
    {
        return false;
    }
    return std::all_of(damage.Grid, damage.Grid + 30, [](float value) { return value >= -0.01f && value <= 16.0f; });
}

std::vector<uint8_t> WriteCarRadio(const CarRadio& radio)
{
    nc::proto::Writer writer(nc::proto::MsgType::CarsWord);
    writer.PutU8(radio.Kind);
    writer.PutU32(radio.NetId);
    writer.PutU8(radio.On ? 1 : 0);
    writer.PutU32(radio.Station & 0xFFFF);
    writer.PutU32(radio.Spare);
    writer.PutU64(radio.StationName);
    writer.PutU64(radio.TrackName);
    return writer.Data();
}

bool ReadCarRadio(const std::vector<uint8_t>& message, CarRadio& radio)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::CarsWord))
    {
        return false;
    }
    radio.Kind = reader.GetU8();
    if (!reader.Ok() || radio.Kind > 1)
    {
        return false;
    }
    radio.NetId = reader.GetU32();
    radio.On = reader.GetU8();
    radio.Station = reader.GetU32();
    radio.Spare = reader.GetU32();
    radio.StationName = reader.GetU64();
    radio.TrackName = reader.GetU64();
    return reader.Ok() && reader.Remaining() == 0 && radio.NetId != 0 && radio.On < 2 && radio.Station < 0x10000;
}

std::vector<uint8_t> WriteVehicleSceneSeat(const VehicleSceneSeat& seat)
{
    nc::proto::Writer writer(nc::proto::MsgType::VehicleSceneSeat);
    writer.PutU64(seat.Entity);
    writer.PutU64(seat.Record);
    writer.PutU64(seat.Slot);
    PutFloats(writer, seat.Offset, 3);
    PutFloats(writer, seat.Orientation, 4);
    PutFloats(writer, seat.CarPosition, 3);
    writer.PutU8(seat.Flags & 7);
    return writer.Data();
}

bool ReadVehicleSceneSeat(const std::vector<uint8_t>& message, VehicleSceneSeat& seat)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::VehicleSceneSeat))
    {
        return false;
    }
    seat.Entity = reader.GetU64();
    seat.Record = reader.GetU64();
    seat.Slot = reader.GetU64();
    if (!GetFloats(reader, seat.Offset, 3) || !GetFloats(reader, seat.Orientation, 4) || !GetFloats(reader, seat.CarPosition, 3))
    {
        return false;
    }
    seat.Flags = reader.GetU8();
    const float offsetSquared = seat.Offset[0] * seat.Offset[0] + seat.Offset[1] * seat.Offset[1] + seat.Offset[2] * seat.Offset[2];
    const bool placeOk = std::all_of(seat.CarPosition, seat.CarPosition + 3, [](float value) { return std::fabs(value) <= 100000.0f; });
    return reader.Ok() && reader.Remaining() == 0 && offsetSquared <= 100.0f && placeOk && (seat.Flags & ~7u) == 0;
}
}
