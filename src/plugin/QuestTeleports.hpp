/*
 quest teleports the taking half of nightcoops quest teleport offers (QuestTeleport 77): when the game or a scene moves the host
 far (kind 1 game teleport 2 scene move 3 a story scene talks) a guest in its world 40..600 m away gets an offer for 60 s.
 holding PageUp (fn + up on a laptop) for a second moves our player 2 m beside that point
 not done yet: sending ours as host and the storypull calls (StoryPull 87)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
struct PlayerState;
}

namespace nightcoop_mac::quest_teleports
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool testHold, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool takeOffers, const net::PlayerState* ourPlayer, uint64_t nowMs, const LogFunction& log);
}
