#include "plugin/NpcHandoffs.hpp"

#include "net/GameMessages.hpp"
#include "net/NpcMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/NpcCopies.hpp"
#include "plugin/NpcShare.hpp"

#include <ProtocolCore.hpp>

#include <cstdio>

namespace nightcoop_mac::npc_handoffs
{
bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::NpcHandoff) || messageType == static_cast<uint8_t>(nc::proto::MsgType::NpcHandoffAck) ||
           messageType == static_cast<uint8_t>(nc::proto::MsgType::NpcClaim);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, NpcCopies& copies, NpcShare& ourNpcs, net::RelaySession& session,
               const LogFunction& log)
{
    const uint8_t type = net::MessageTypeOf(message);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::NpcHandoff))
    {
        net::NpcHandoff handoff;
        if (!net::ReadNpcHandoff(message, handoff))
        {
            return;
        }
        net::NpcHandoffAck ack;
        ack.NetId = handoff.Spawn.NetId;
        uint64_t entityHash = 0;
        net::NpcSpawn spawn;
        if (copies.ReleaseForHandoff(handoff.Spawn.NetId, fromPeer, entityHash, spawn))
        {
            spawn.Record = handoff.Spawn.Record;
            ourNpcs.TakeHanded(handoff.Spawn.NetId, entityHash, spawn, nowMs);
            ack.Accepted = true;
            ack.Entity = entityHash;
            session.SendGameMessage(net::WriteNpcHandoffAck(ack), static_cast<int32_t>(fromPeer));
            session.SendGameMessage(net::WriteNpcClaim({handoff.Spawn.NetId, net::kClaimAfterHandoff, fromPeer}));
            return;
        }
        ack.Accepted = false;
        ack.Reason = net::kHandoffNoBody;
        session.SendGameMessage(net::WriteNpcHandoffAck(ack), static_cast<int32_t>(fromPeer));
        char line[160];
        std::snprintf(line, sizeof(line), "npcs: peer %u offered net id %#010x but we have no body for it", fromPeer, handoff.Spawn.NetId);
        log(line);
        return;
    }
    if (type == static_cast<uint8_t>(nc::proto::MsgType::NpcHandoffAck))
    {
        net::NpcHandoffAck ack;
        if (net::ReadNpcHandoffAck(message, ack))
        {
            ourNpcs.OnHandoffAck(fromPeer, ack, nowMs, [&](uint32_t netId, uint32_t newOwner, uint64_t entityHash, const net::NpcSpawn& spawn) {
                copies.AdoptAsPuppet(netId, newOwner, entityHash, spawn, nowMs);
            });
        }
        return;
    }
    net::NpcClaim claim;
    if (type == static_cast<uint8_t>(nc::proto::MsgType::NpcClaim) && net::ReadNpcClaim(message, claim))
    {
        copies.FollowClaim(fromPeer, claim, nowMs);
    }
}
}
