/*
 world load the reporting half of nightcoops worldload (WorldLoad 120) plus the lobby word (LobbyStart 113)
 a windows host holds loading screens and fast travel until evryone in its world is settled. we tell it our loading state
 (kind 1) when it changes and evry 3 s: in a transition while no save runs here (our transition number goes up evry time a
 save comes up) settled once it does. a hold the host lets go (kind 2) only gets logged since our game never waits on it
 StoryHold: a windows host holds its story until a partner coming into its world says it is ready. a guest says its state
 (kind 1: 1 coming while no save runs 3 ready once the hosts world is up here 0 idle in its own world) when it changes and evry
 5 s. the hosts hold words (kind 2) get logged
 LobbyStart: the host started its save (or a new game) from its co-op lobby. we show it. the join itself starts once the hosts
 world is up like always (a new game for evryone is not there on mac)
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

namespace nightcoop_mac::world_load
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, bool inHostWorld, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, const LogFunction& log);
}
