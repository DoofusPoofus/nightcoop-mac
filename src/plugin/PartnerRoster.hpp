/*
 partner roster, the names part of nightcoops lobby: who is in the session and wat they are called
 a guest sends PlayerPresence (111) to the host when it changed and evry 2 s (our name, in a world or in the menu, the in the
 hosts world flag). the host keeps the latest presence per guest and sends SessionRoster (112) to evryone evry 2 s and when
 it changed, its own entry first (role host), then evry guest with the name from their presence or their Hello. a guest keeps
 the roster it got from peer 0
 NameOf follows nightcoops order: the roster, then the presence (host), then the Hello nickname, then "Player N"
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::partner_roster
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inWorld, const std::string& ourName, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
std::string NameOf(uint32_t peerNumber, net::RelaySession& session);
bool InOurWorld(uint32_t peerNumber);
void Forget();
}
