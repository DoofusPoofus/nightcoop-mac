#include "plugin/PhotoTogether.hpp"

#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Hud.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

namespace nightcoop_mac::photo_together
{
namespace
{
constexpr uint64_t LookEveryMs = 500;

uint64_t NextLookMs = 0;
bool PhotoModeWasOpen = false;
std::map<uint32_t, bool> PartnersInPhotoMode;

bool PhotoModeOpen()
{
    static auto* isActive = game::FindFunction("NightCoopPhoto", "IsActive");
    auto service = game::ScriptService("NightCoopPhoto");
    bool open = false;
    RED4ext::StackArgs_t noArgs;
    if (isActive && service)
    {
        game::CallMethod(service.instance, isActive, &open, noArgs);
    }
    return open;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::PhotoTogether);
}

void Tick(uint64_t nowMs, bool inSave, bool sameWorld, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || nowMs < NextLookMs)
    {
        return;
    }
    NextLookMs = nowMs + LookEveryMs;
    const bool open = PhotoModeOpen();
    if (open == PhotoModeWasOpen)
    {
        return;
    }
    PhotoModeWasOpen = open;
    if (!sameWorld || !session.GetStatus().InSession)
    {
        return;
    }
    nc::proto::Writer writer(nc::proto::MsgType::PhotoTogether);
    writer.PutU8(open ? 1 : 0);
    session.SendGameMessage(writer.Data());
    log(open ? "photo: we opened photo mode, the partners hear of it" : "photo: we closed photo mode");
}

void OnMessage(uint32_t fromPeer, const std::string& nickname, const std::vector<uint8_t>& message, bool sameWorld, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::PhotoTogether)
    {
        return;
    }
    const uint8_t open = reader.GetU8();
    if (!reader.Ok() || reader.Remaining() != 0 || open > 1 || !sameWorld)
    {
        return;
    }
    bool& known = PartnersInPhotoMode[fromPeer];
    if (known == (open == 1))
    {
        return;
    }
    known = open == 1;
    const std::string name = nickname.empty() ? "peer " + std::to_string(fromPeer) : nickname;
    log("photo: " + name + (known ? " opened photo mode" : " closed photo mode"));
    if (known)
    {
        hud::Show(name + " opened photo mode, open yours for a group shot", 4.0f);
    }
}
}
