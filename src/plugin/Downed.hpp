/*
 downed, the mac take on nightcoops downed module (PlayerRevive 47, DownedNotice 48, PlayerState flag 0x100)
 falls are in force while a save is loaded and a partner was in our world in the last 45 s. the script half
 (scripts/NightCoopMac/NightCoopDowned.reds) then makes our player immortal and lets it fall instead of dying, evry 100 ms
 PluginTick hands it the in force flag and gives back the state: 0 up, 1 down, 2 rising. down and rising go out as flag 0x100
 down: holding T for 2 s passes us out (we rise where we lie, nightcoops walk back along our path is not done yet).
 a PlayerRevive from a partner within 8 m makes us rise like nightcoop
 up: a partner with flag 0x100 in reach (2.5 m, 1.6 m up or down, looked at within 60 deg or closer than 0.9 m, a bit more
 to keep it) gets the hud line "Hold F to help up ..." and F held 1 s sends them PlayerRevive. if they are still down 5 s
 later the prompt comes back. a downed partner within 8 m but out of reach gets a one time hint
 evryone down for 4 s: the host sends DownedNotice 2 and evryone passes out (the host loading its last save is not done)
 testing only: RunTest drops our health to 1 % once a partner is there so the fall and a probes revive can be checked
*/
#pragma once

#include "net/SessionMessages.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::downed
{
using LogFunction = std::function<void(const std::string&)>;

struct Partner
{
    uint32_t PeerNumber = 0;
    net::PlayerState State;
    std::string Name;
};

inline constexpr uint16_t DownFlag = 0x100;

void Tick(uint64_t nowMs, bool inSave, const net::PlayerState* ourState, const std::vector<Partner>& partners, net::RelaySession& session,
          const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, const net::PlayerState* ourState, net::RelaySession& session,
               const LogFunction& log);
bool WeAreDown();
void RunTest(uint64_t nowMs, bool partnerHere, const LogFunction& log);
}
