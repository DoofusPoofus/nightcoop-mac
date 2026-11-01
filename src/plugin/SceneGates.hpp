/*
 scene gates the answering half of nightcoops scenegate (SceneGate 83): a windows hosts quest scene waits at a gate (the zone
 at the npc that starts a talk) until evry partner in its world stands there. the mac can not read the scene graph (that needs
 native pause node hooks) so it answers the hosts own gate words: for evry scene the host says it waits in we say we are at
 the gate (state 2) while our player is within 12 m of where the host stands and that the scene is here (state 1) when we are
 farther. when the host passed (3) or left (0) we say the same. sent to the host on change and again evry 5 s
 not done yet: holding or passing our own pause nodes (SceneGateGo 115) zone passes (151) and walking away from talks (192)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
struct PlayerState;
}

namespace nightcoop_mac::scene_gates
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool inHostWorld, const net::PlayerState* ourPlayer, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, const LogFunction& log);
}
