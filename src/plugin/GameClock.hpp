/*
 game clock, nightcoops time of day sync (part of its sessionflow)
 the host sends its game seconds (gameTimeSystem.GetGameTime) to evryone evry 5 s as GameClock (46) while a save is loaded.
 a guest in a loaded save compares them with its own and when they are more than 120 s apart it takes the hosts time
 (SetGameTimeBySeconds). smaller gaps are left alone so the clock does not jump back and forth
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

namespace nightcoop_mac::game_clock
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool inSave, bool weAreHost, const LogFunction& log);
}
