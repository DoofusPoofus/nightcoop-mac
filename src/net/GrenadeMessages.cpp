#include "net/GrenadeMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
bool PlaceOk(const float* position)
{
    return std::all_of(position, position + 3, [](float value) { return std::isfinite(value) && std::fabs(value) <= 100000.0f; });
}
}

std::vector<uint8_t> WriteGrenade(bool playerGrenade, const GrenadeState& grenade)
{
    nc::proto::Writer writer(playerGrenade ? nc::proto::MsgType::PlayerGrenade : nc::proto::MsgType::NpcGrenade);
    writer.PutU32(grenade.Owner);
    writer.PutU32(grenade.Number);
    writer.PutU8(grenade.Stage);
    writer.PutU64(grenade.ItemRecord);
    writer.PutU64(grenade.SecondRecord);
    for (float coordinate : grenade.Position)
    {
        writer.PutF32(coordinate);
    }
    for (float part : grenade.Orientation)
    {
        writer.PutF32(part);
    }
    writer.PutU64(grenade.SenderClockMs);
    return writer.Data();
}

bool ReadGrenade(const std::vector<uint8_t>& message, bool& playerGrenade, GrenadeState& grenade)
{
    nc::proto::Reader reader(message.data(), message.size());
    const auto type = reader.ReadHeader();
    if (type != nc::proto::MsgType::PlayerGrenade && type != nc::proto::MsgType::NpcGrenade)
    {
        return false;
    }
    playerGrenade = type == nc::proto::MsgType::PlayerGrenade;
    grenade.Owner = reader.GetU32();
    grenade.Number = reader.GetU32();
    grenade.Stage = reader.GetU8();
    grenade.ItemRecord = reader.GetU64();
    grenade.SecondRecord = reader.GetU64();
    for (float& coordinate : grenade.Position)
    {
        coordinate = reader.GetF32();
    }
    for (float& part : grenade.Orientation)
    {
        part = reader.GetF32();
    }
    grenade.SenderClockMs = reader.GetU64();
    const bool orientationOk = std::all_of(grenade.Orientation, grenade.Orientation + 4, [](float value) { return std::isfinite(value); });
    return reader.Ok() && reader.Remaining() == 0 && grenade.Stage >= kGrenadeHeld && grenade.Stage <= kGrenadeGone && PlaceOk(grenade.Position) &&
           orientationOk;
}

std::vector<uint8_t> WritePlayerBlast(const std::vector<Blast>& blasts)
{
    const size_t count = std::min(blasts.size(), kMostBlastsAtOnce);
    nc::proto::Writer writer(nc::proto::MsgType::PlayerBlast);
    writer.PutU8(static_cast<uint8_t>(count));
    for (size_t index = 0; index < count; ++index)
    {
        for (float coordinate : blasts[index].Position)
        {
            writer.PutF32(coordinate);
        }
        writer.PutF32(blasts[index].Radius);
        writer.PutU64(blasts[index].Id);
    }
    return writer.Data();
}

bool ReadPlayerBlast(const std::vector<uint8_t>& message, std::vector<Blast>& blasts)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::PlayerBlast)
    {
        return false;
    }
    const uint8_t count = reader.GetU8();
    if (!reader.Ok() || count < 1 || count > kMostBlastsAtOnce || reader.Remaining() != count * 24u)
    {
        return false;
    }
    blasts.assign(count, {});
    for (auto& blast : blasts)
    {
        for (float& coordinate : blast.Position)
        {
            coordinate = reader.GetF32();
        }
        blast.Radius = reader.GetF32();
        blast.Id = reader.GetU64();
        if (!PlaceOk(blast.Position) || !std::isfinite(blast.Radius) || blast.Radius < 0 || blast.Radius > 100)
        {
            return false;
        }
    }
    return reader.Ok() && reader.Remaining() == 0;
}
}
