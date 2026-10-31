#include "plugin/PlayerExtras.hpp"

#include "net/GameMessages.hpp"
#include "plugin/BodyCarry.hpp"
#include "plugin/MessageDrops.hpp"
#include "plugin/TeamBuild.hpp"

#include <ProtocolCore.hpp>

namespace nightcoop_mac::player_extras
{
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, const LogFunction& log)
{
    switch (static_cast<nc::proto::MsgType>(net::MessageTypeOf(message)))
    {
    case nc::proto::MsgType::PlayerCarry: body_carry::OnMessage(fromPeer, message, nowMs, log); break;
    case nc::proto::MsgType::BuildStats: team_build::OnMessage(fromPeer, message, nowMs, log); break;
    default: message_drops::OnMessage(fromPeer, message, nowMs); break;
    }
}

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, RemoteBodies& bodies, NpcCopies& copies, NpcShare& ourNpcs, const LogFunction& log)
{
    body_carry::Tick(nowMs, inSave, session, bodies, copies, ourNpcs, log);
    team_build::Tick(nowMs, inSave, session, log);
    message_drops::Tick(nowMs, log);
}
}
