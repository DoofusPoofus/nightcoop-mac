/*
 grenades the first cut of nightcoops physicspush (PlayerBlast 131; the script half is NightCoopGrenades in
 scripts/NightCoopMac/NightCoopGrenades.reds)
 evry 100 ms the blasts of our players grenades (place radius id) go to evryone as PlayerBlast. a partners blast in our
 world (a guest in the hosts world or a host with that partner in its world. blasts any_world in session.txt for tests)
 pushes loose things around here with the games physical impulse sphere. no damage. ids seen before are skipped
 not done yet: the visual grenade copies (PlayerGrenade 99 / NpcGrenade 24)
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

namespace nightcoop_mac::grenades
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool partnerAround, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, const LogFunction& log);
}
