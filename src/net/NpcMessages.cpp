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

namespace
{
void PutSpawnBody(nc::proto::Writer& writer, const NpcSpawn& spawn)
{
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
}

bool GetSpawnBody(nc::proto::Reader& reader, NpcSpawn& spawn)
{
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
    return reader.Ok() && PositionOk(spawn.Position) && std::isfinite(spawn.Yaw);
}

void PutSnapshotBody(nc::proto::Writer& writer, const NpcSnapshot& snapshot)
{
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
}

bool GetSnapshotBody(nc::proto::Reader& reader, NpcSnapshot& snapshot)
{
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
    return reader.Ok() && PositionOk(snapshot.Position) && std::isfinite(snapshot.Yaw) && std::isfinite(snapshot.Speed) &&
           std::isfinite(snapshot.Health) && std::isfinite(snapshot.TimeDilation);
}
}

std::vector<uint8_t> WriteNpcSpawn(const NpcSpawn& spawn)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcSpawn);
    PutSpawnBody(writer, spawn);
    return writer.Data();
}

bool ReadNpcSpawn(const std::vector<uint8_t>& message, NpcSpawn& spawn)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcSpawn)
    {
        return false;
    }
    return GetSpawnBody(reader, spawn) && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteNpcSnapshot(const NpcSnapshot& snapshot)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcSnapshot);
    PutSnapshotBody(writer, snapshot);
    return writer.Data();
}

bool ReadNpcSnapshot(const std::vector<uint8_t>& message, NpcSnapshot& snapshot)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcSnapshot)
    {
        return false;
    }
    return GetSnapshotBody(reader, snapshot) && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteNpcHandoff(const NpcHandoff& handoff)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcHandoff);
    PutSpawnBody(writer, handoff.Spawn);
    PutSnapshotBody(writer, handoff.Snapshot);
    return writer.Data();
}

bool ReadNpcHandoff(const std::vector<uint8_t>& message, NpcHandoff& handoff)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcHandoff)
    {
        return false;
    }
    return GetSpawnBody(reader, handoff.Spawn) && GetSnapshotBody(reader, handoff.Snapshot) && reader.Remaining() == 0 && handoff.Spawn.NetId != 0 &&
           handoff.Spawn.NetId == handoff.Snapshot.NetId;
}

std::vector<uint8_t> WriteNpcHandoffAck(const NpcHandoffAck& ack)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcHandoffAck);
    writer.PutU32(ack.NetId);
    writer.PutU8(ack.Accepted ? 1 : 0);
    writer.PutU8(ack.Reason);
    writer.PutU64(ack.Entity);
    return writer.Data();
}

bool ReadNpcHandoffAck(const std::vector<uint8_t>& message, NpcHandoffAck& ack)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcHandoffAck)
    {
        return false;
    }
    ack.NetId = reader.GetU32();
    ack.Accepted = reader.GetU8() != 0;
    ack.Reason = reader.GetU8();
    ack.Entity = reader.GetU64();
    return reader.Ok() && reader.Remaining() == 0 && ack.NetId != 0;
}

std::vector<uint8_t> WriteNpcClaim(const NpcClaim& claim)
{
    nc::proto::Writer writer(nc::proto::MsgType::NpcClaim);
    writer.PutU32(claim.NetId);
    writer.PutU8(claim.Kind);
    writer.PutU32(claim.FromPeer);
    return writer.Data();
}

bool ReadNpcClaim(const std::vector<uint8_t>& message, NpcClaim& claim)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcClaim)
    {
        return false;
    }
    claim.NetId = reader.GetU32();
    claim.Kind = reader.GetU8();
    claim.FromPeer = reader.GetU32();
    return reader.Ok() && reader.Remaining() == 0 && claim.NetId != 0 && claim.Kind >= 1 && claim.Kind <= 3;
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
namespace
{
void PutDeadPerson(nc::proto::Writer& writer, const DeadPerson& person)
{
    writer.PutU64(person.Entity);
    writer.PutU64(person.Record);
    writer.PutU64(person.MatchKeyA);
    writer.PutU64(person.MatchKeyB);
    for (float coordinate : person.Position)
    {
        writer.PutF32(coordinate);
    }
}

bool GetDeadPerson(nc::proto::Reader& reader, DeadPerson& person)
{
    person.Entity = reader.GetU64();
    person.Record = reader.GetU64();
    person.MatchKeyA = reader.GetU64();
    person.MatchKeyB = reader.GetU64();
    for (float& coordinate : person.Position)
    {
        coordinate = reader.GetF32();
    }
    return reader.Ok() && PositionOk(person.Position);
}
}

std::vector<uint8_t> WriteNpcDead(bool fullTable, const std::vector<DeadPerson>& people)
{
    const size_t count = std::min(people.size(), kMostDeadInTable);
    nc::proto::Writer writer(nc::proto::MsgType::NpcDead);
    writer.PutU8(fullTable ? 1 : 0);
    writer.PutU16(static_cast<uint16_t>(count));
    for (size_t index = 0; index < count; ++index)
    {
        PutDeadPerson(writer, people[index]);
    }
    return writer.Data();
}

bool ReadNpcDead(const std::vector<uint8_t>& message, bool& fullTable, std::vector<DeadPerson>& people)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcDead)
    {
        return false;
    }
    const uint8_t full = reader.GetU8();
    const uint16_t count = reader.GetU16();
    if (!reader.Ok() || full > 1 || count > kMostDeadInTable || reader.Remaining() != count * 44u)
    {
        return false;
    }
    fullTable = full == 1;
    people.assign(count, {});
    for (auto& person : people)
    {
        if (!GetDeadPerson(reader, person))
        {
            return false;
        }
    }
    return reader.Remaining() == 0;
}

std::vector<uint8_t> WriteNpcDefeat(uint64_t senderClockMs, const std::vector<DeadPerson>& people)
{
    const size_t count = std::min(people.size(), kMostDefeatsAtOnce);
    nc::proto::Writer writer(nc::proto::MsgType::NpcDefeat);
    writer.PutU64(senderClockMs);
    writer.PutU8(static_cast<uint8_t>(count));
    for (size_t index = 0; index < count; ++index)
    {
        PutDeadPerson(writer, people[index]);
        writer.PutU8(people[index].Flag);
    }
    return writer.Data();
}

bool ReadNpcDefeat(const std::vector<uint8_t>& message, uint64_t& senderClockMs, std::vector<DeadPerson>& people)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::NpcDefeat)
    {
        return false;
    }
    senderClockMs = reader.GetU64();
    const uint8_t count = reader.GetU8();
    if (!reader.Ok() || count > kMostDefeatsAtOnce || reader.Remaining() != count * 45u)
    {
        return false;
    }
    people.assign(count, {});
    for (auto& person : people)
    {
        if (!GetDeadPerson(reader, person))
        {
            return false;
        }
        person.Flag = reader.GetU8();
    }
    return reader.Ok() && reader.Remaining() == 0;
}
}
