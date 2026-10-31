#include "net/NpcMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr float FarthestCoordinate = 100000.0f;
constexpr size_t MostPartnerAttitudes = 7;

bool PositionOk(const float* position)
{
    return std::all_of(position, position + 3, [](float value) { return std::isfinite(value) && std::fabs(value) <= FarthestCoordinate; });
}
}

std::vector<uint8_t> WriteNpcSpawn(const NpcSpawn& spawn)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcSpawn);
    writer.PutU32(spawn.NetId);
    writer.PutU64(spawn.Record);
    writer.PutU64(spawn.Appearance);
    writer.PutU64(spawn.OwnerEntity);
    writer.PutU64(spawn.MatchKeyA);
    writer.PutU64(spawn.MatchKeyB);
    for (float coordinate : spawn.Position)
    {
        writer.PutF32(coordinate);
    }
    writer.PutF32(spawn.Yaw);
    writer.PutU8(spawn.HighLevelState);
    writer.PutU8(spawn.Crowd);
    return writer.Data();
}

bool ReadNpcSpawn(const std::vector<uint8_t>& message, NpcSpawn& spawn)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcSpawn)
    {
        return false;
    }
    spawn.NetId = reader.GetU32();
    spawn.Record = reader.GetU64();
    spawn.Appearance = reader.GetU64();
    spawn.OwnerEntity = reader.GetU64();
    spawn.MatchKeyA = reader.GetU64();
    spawn.MatchKeyB = reader.GetU64();
    for (float& coordinate : spawn.Position)
    {
        coordinate = reader.GetF32();
    }
    spawn.Yaw = reader.GetF32();
    spawn.HighLevelState = reader.GetU8();
    spawn.Crowd = reader.GetU8();
    return reader.Ok() && reader.Remaining() == 0 && PositionOk(spawn.Position) && std::isfinite(spawn.Yaw);
}

std::vector<uint8_t> WriteNpcSnapshot(const NpcSnapshot& snapshot)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcSnapshot);
    writer.PutU32(snapshot.NetId);
    for (float coordinate : snapshot.Position)
    {
        writer.PutF32(coordinate);
    }
    writer.PutF32(snapshot.Yaw);
    writer.PutF32(snapshot.Speed);
    writer.PutF32(snapshot.Health);
    for (uint8_t state : snapshot.States)
    {
        writer.PutU8(state);
    }
    writer.PutU8(snapshot.Attitude);
    const auto count = static_cast<uint8_t>(std::min(snapshot.PartnerAttitudes.size(), MostPartnerAttitudes));
    writer.PutU8(count);
    for (uint8_t index = 0; index < count; ++index)
    {
        writer.PutU8(snapshot.PartnerAttitudes[index].Peer);
        writer.PutU8(snapshot.PartnerAttitudes[index].Attitude);
    }
    writer.PutU64(snapshot.RightHandItem);
    writer.PutU64(snapshot.OwnerClockMs);
    writer.PutF32(snapshot.TimeDilation);
    writer.PutU8(snapshot.DilationFlags);
    writer.PutU8(snapshot.Flags);
    writer.PutU64(snapshot.LeftHandItem);
    return writer.Data();
}

bool ReadNpcSnapshot(const std::vector<uint8_t>& message, NpcSnapshot& snapshot)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcSnapshot)
    {
        return false;
    }
    snapshot.NetId = reader.GetU32();
    for (float& coordinate : snapshot.Position)
    {
        coordinate = reader.GetF32();
    }
    snapshot.Yaw = reader.GetF32();
    snapshot.Speed = reader.GetF32();
    snapshot.Health = reader.GetF32();
    for (uint8_t& state : snapshot.States)
    {
        state = reader.GetU8();
    }
    snapshot.Attitude = reader.GetU8();
    const uint8_t count = reader.GetU8();
    snapshot.PartnerAttitudes.clear();
    for (uint8_t index = 0; index < count && reader.Ok(); ++index)
    {
        NpcAttitude attitude;
        attitude.Peer = reader.GetU8();
        attitude.Attitude = reader.GetU8();
        if (snapshot.PartnerAttitudes.size() < MostPartnerAttitudes)
        {
            snapshot.PartnerAttitudes.push_back(attitude);
        }
    }
    snapshot.RightHandItem = reader.GetU64();
    snapshot.OwnerClockMs = reader.GetU64();
    snapshot.TimeDilation = reader.GetF32();
    snapshot.DilationFlags = reader.GetU8();
    snapshot.Flags = reader.GetU8();
    snapshot.LeftHandItem = reader.GetU64();
    return reader.Ok() && reader.Remaining() == 0 && PositionOk(snapshot.Position) && std::isfinite(snapshot.Yaw) && std::isfinite(snapshot.Speed) &&
           std::isfinite(snapshot.Health) && std::isfinite(snapshot.TimeDilation);
}

std::vector<uint8_t> WriteNpcDespawn(const NpcDespawn& despawn)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcDespawn);
    writer.PutU32(despawn.NetId);
    writer.PutU8(despawn.Reason);
    writer.PutU32(despawn.Killer);
    return writer.Data();
}

bool ReadNpcDespawn(const std::vector<uint8_t>& message, NpcDespawn& despawn)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcDespawn)
    {
        return false;
    }
    despawn.NetId = reader.GetU32();
    despawn.Reason = reader.GetU8();
    despawn.Killer = reader.GetU32();
    return reader.Ok() && reader.Remaining() == 0 && despawn.Reason <= 3;
}
}
