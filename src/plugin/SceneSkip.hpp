/*
 scene skip the first cut of nightcoops sceneskip (SceneSkipHere 121 SceneLineSkip 118; the script half is NightCoopSceneSkip in
 scripts/NightCoopMac/NightCoopSceneSkip.reds)
 we tell the others evry 5 s and on change whether our player may skip (0 may 1 combat 5 in a car) without a scene place so a
 windows partner never waits on us. a partners line skip (the host relays it) fast forwards the scene that holds our player
 here for about as long as they skipped (1 to 10 s) the game itself stops a fast forward at a choice
 not done yet: sending our own skips (the scene ids and line places need native line hooks) and cut skips (119)
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

namespace nightcoop_mac::scene_skip
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool sameWorld, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, uint64_t nowMs, const LogFunction& log);
}
