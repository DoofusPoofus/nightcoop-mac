#include "net/SessionMessages.hpp"

#include <ProtocolCore.hpp>

#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
bool OpenMessage(nc::proto::Reader& reader, nc::proto::MsgType expectedType)
{
    return reader.ReadHeader() == expectedType && reader.Ok();
}

void ReplaceIfNotFinite(float& value, float replacement, bool& fixedSomething)
{
    if (!std::isfinite(value))
    {
        value = replacement;
        fixedSomething = true;
    }
}
}

std::vector<uint8_t> WriteHello(uint64_t clientId, const std::string& nickname, uint64_t gameVersion, uint64_t installId)
{
    nc::proto::Writer writer(nc::proto::MsgType::Hello);
    writer.PutU32(kProtocolVersion);
    writer.PutU64(clientId);
    writer.PutString(nickname.empty() ? std::string("guest") : nickname);
    writer.PutU64(gameVersion);
    if (installId != 0)
    {
        writer.PutU64(installId);
    }
    return writer.Data();
}

bool ReadHello(const std::vector<uint8_t>& message, Hello& hello)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::Hello))
    {
        return false;
    }
    hello.Protocol = reader.GetU32();
    hello.ClientId = reader.GetU64();
    hello.Nickname = reader.GetString();
    if (!reader.Ok())
    {
        return false;
    }
    hello.GameVersion = reader.Remaining() >= 8 ? reader.GetU64() : 0;
    hello.InstallId = reader.Remaining() >= 8 ? reader.GetU64() : 0;
    return reader.Ok();
}

std::vector<uint8_t> WriteWelcome(const Welcome& welcome)
{
    nc::proto::Writer writer(nc::proto::MsgType::Welcome);
    writer.PutU32(welcome.PeerNumber);
    writer.PutU64(static_cast<uint64_t>(welcome.HostClockMs));
    return writer.Data();
}

bool ReadWelcome(const std::vector<uint8_t>& message, Welcome& welcome)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::Welcome))
    {
        return false;
    }
    welcome.PeerNumber = reader.GetU32();
    welcome.HostClockMs = static_cast<int64_t>(reader.GetU64());
    return reader.Ok();
}

std::vector<uint8_t> WritePlayerState(const PlayerState& state)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerState);
    writer.PutU32(state.PeerNumber);
    writer.PutF32(state.PositionX);
    writer.PutF32(state.PositionY);
    writer.PutF32(state.PositionZ);
    writer.PutF32(state.Yaw);
    writer.PutF32(state.CameraPitch);
    writer.PutF32(state.Speed);
    writer.PutU64(state.SessionTimeMs);
    writer.PutU16(state.Flags);
    writer.PutU8(state.LocomotionState);
    writer.PutU8(state.UpperBodyState);
    writer.PutU8(state.WeaponState);
    writer.PutU32(state.Unknown30);
    writer.PutU64(state.Unknown38);
    writer.PutF32(state.Unknown40);
    writer.PutF32(state.Unknown44);
    writer.PutF32(state.Unknown48);
    writer.PutF32(state.Unknown4C);
    writer.PutF32(state.Unknown50);
    for (uint8_t value : state.Bytes54)
    {
        writer.PutU8(value);
    }
    writer.PutF32(state.Unknown64);
    writer.PutU8(state.Unknown68);
    writer.PutU8(state.Unknown69);
    writer.PutU64(state.Unknown70);
    writer.PutU32(state.Unknown78);
    writer.PutU8(state.Unknown7C);
    return writer.Data();
}

bool ReadPlayerState(const std::vector<uint8_t>& message, PlayerState& state)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::PlayerState))
    {
        return false;
    }
    state.PeerNumber = reader.GetU32();
    state.PositionX = reader.GetF32();
    state.PositionY = reader.GetF32();
    state.PositionZ = reader.GetF32();
    state.Yaw = reader.GetF32();
    state.CameraPitch = reader.GetF32();
    state.Speed = reader.GetF32();
    state.SessionTimeMs = reader.GetU64();
    state.Flags = reader.GetU16();
    state.LocomotionState = reader.GetU8();
    state.UpperBodyState = reader.GetU8();
    state.WeaponState = reader.GetU8();
    state.Unknown30 = reader.GetU32();
    state.Unknown38 = reader.GetU64();
    state.Unknown40 = reader.GetF32();
    state.Unknown44 = reader.GetF32();
    state.Unknown48 = reader.GetF32();
    state.Unknown4C = reader.GetF32();
    state.Unknown50 = reader.GetF32();
    for (uint8_t& value : state.Bytes54)
    {
        value = reader.GetU8();
    }
    state.Unknown64 = reader.GetF32();
    state.Unknown68 = reader.GetU8();
    state.Unknown69 = reader.GetU8();
    if (!reader.Ok())
    {
        return false;
    }
    state.Unknown70 = 0;
    state.Unknown78 = 0;
    state.Unknown7C = 0;
    if (reader.Remaining() < 13)
    {
        return true;
    }
    state.Unknown70 = reader.GetU64();
    state.Unknown78 = reader.GetU32();
    state.Unknown7C = reader.GetU8();
    return reader.Ok();
}

bool PlayerStateLooksSane(PlayerState& state)
{
    for (float coordinate : {state.PositionX, state.PositionY, state.PositionZ})
    {
        if (!std::isfinite(coordinate) || std::fabs(coordinate) > 100000.0f)
        {
            return false;
        }
    }
    bool fixedSomething = false;
    ReplaceIfNotFinite(state.Yaw, 0.0f, fixedSomething);
    ReplaceIfNotFinite(state.CameraPitch, 0.0f, fixedSomething);
    ReplaceIfNotFinite(state.Speed, 0.0f, fixedSomething);
    ReplaceIfNotFinite(state.Unknown40, 1.0f, fixedSomething);
    ReplaceIfNotFinite(state.Unknown44, 1.0f, fixedSomething);
    ReplaceIfNotFinite(state.Unknown48, 0.0f, fixedSomething);
    ReplaceIfNotFinite(state.Unknown4C, 0.0f, fixedSomething);
    ReplaceIfNotFinite(state.Unknown50, 0.0f, fixedSomething);
    ReplaceIfNotFinite(state.Unknown64, 0.0f, fixedSomething);
    return true;
}

bool ReadCustomizationGender(const std::vector<uint8_t>& message, bool& isMale)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::PlayerCustomization) || reader.GetU8() != 1)
    {
        return false;
    }
    reader.GetU32();
    isMale = reader.GetU8() != 0;
    return reader.Ok();
}

std::vector<uint8_t> WriteReject(const std::string& reason)
{
    nc::proto::Writer writer(nc::proto::MsgType::Reject);
    writer.PutString(reason);
    return writer.Data();
}

bool ReadReject(const std::vector<uint8_t>& message, std::string& reason)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::Reject))
    {
        return false;
    }
    reason = reader.GetString();
    return reader.Ok();
}
}
