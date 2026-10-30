#include "net/SessionMessages.hpp"

#include <ProtocolCore.hpp>

namespace nightcoop_mac::net
{
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
    if (reader.ReadHeader() != nc::proto::MsgType::Hello || !reader.Ok())
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
}
