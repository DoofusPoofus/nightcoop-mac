/*
 player effects the taking half of nightcoops playerfx (PlayerFx 138): effect pictures a partners game drew for its player
 (rocket and launcher blasts explosive rounds ground slams landings ricochets beams) get played here at the same place with
 gameFxSystem.SpawnEffect (picture and sound only no damage) when that partner is in our world (blasts any_world in
 session.txt for tests). effect numbers seen before are skipped
 not done yet: sending ours (nightcoop catches them with native detours in the effect executor nodes that have no mac address)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::player_effects
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, const LogFunction& log);
}
