#include "net/DoorMessages.hpp"

#include <ProtocolCore.hpp>

#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr size_t DoorSyncSize = 5 + 41;
constexpr float FarthestCoordinate = 100000.0f;
constexpr uint16_t QuestOpsNobodyUses = 0xF801;
}

std::vector<uint8_t> WriteDoorSync(const DoorSync& door)
{
    nc::proto::Writer writer(nc::proto::MsgType::DoorSync);
    writer.PutU8(door.Kind);
    writer.PutU32(door.Peer);
    writer.PutU64(door.DoorKey);
    writer.PutU8(door.State);
    writer.PutU8(door.Changed);
    writer.PutU8(door.Cause);
    writer.PutU16(door.QuestOps);
    writer.PutU8(door.Side);
    writer.PutU64(door.Version);
    writer.PutU16(door.RequestNumber);
    for (float coordinate : door.Position)
    {
        writer.PutF32(coordinate);
    }
    return writer.Data();
}

bool ReadDoorSync(const std::vector<uint8_t>& message, DoorSync& door)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (message.size() != DoorSyncSize || reader.ReadHeader() != nc::proto::MsgType::DoorSync)
    {
        return false;
    }
    door.Kind = reader.GetU8();
    door.Peer = reader.GetU32();
    door.DoorKey = reader.GetU64();
    door.State = reader.GetU8();
    door.Changed = reader.GetU8();
    door.Cause = reader.GetU8();
    door.QuestOps = reader.GetU16();
    door.Side = reader.GetU8();
    door.Version = reader.GetU64();
    door.RequestNumber = reader.GetU16();
    for (float& coordinate : door.Position)
    {
        coordinate = reader.GetF32();
    }
    if (!reader.Ok() || (door.Kind != kDoorTell && door.Kind != kDoorShared) || door.DoorKey == 0)
    {
        return false;
    }
    if ((door.State & ~kDoorStateBits) != 0 || (door.Changed & ~kDoorStateBits) != 0 || door.Cause > kDoorCauseKeptOpen || door.Side > 2)
    {
        return false;
    }
    if ((door.QuestOps & QuestOpsNobodyUses) != 0 || (door.Cause == kDoorCauseQuest) != (door.QuestOps != 0))
    {
        return false;
    }
    if ((door.Kind == kDoorShared && door.Version == 0) || (door.Kind == kDoorTell && door.RequestNumber == 0))
    {
        return false;
    }
    for (float coordinate : door.Position)
    {
        if (!std::isfinite(coordinate) || std::fabs(coordinate) > FarthestCoordinate)
        {
            return false;
        }
    }
    return true;
}
}
