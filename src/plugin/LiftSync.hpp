/*
 lift sync, a first cut of nightcoops one cab lifts (LiftSync 86 kinds 2, 3 and 4; the script half is NightCoopLiftSync in
 scripts/NightCoopMac/NightCoopLifts.reds). one cab mode is on while a save is loaded and a partner is around
 evry 100 ms the script queues get read: on the host each lift start goes out as a departure (kind 2, a departure number, the
 session clock + 150 ms), on a guest each swallowed button press goes to the host as a request (kind 3). the host starts its
 own cab for a request with StartLift and answers with kind 4 (1 it goes, 2 already there, 3 on its way, 6 not a lift here),
 a guest starts its copy of the cab for evry departure from the host, and starts its own cab when the host said 6
 in step start like nightcoop: a departure carries the session time evry copy leaves at (now + 150 ms + the lifts start delay)
 and the hosts speed. the host holds its own LiftStartDelayEvent until then, a guest holds its copy and runs it at the hosts
 speed, the plugin releases them when the session clock gets there
 not done yet: presence (kind 1), quest lift holds, arrival holds, travel time overrides and the body in cab rides (LiftRide 97)
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

namespace nightcoop_mac::lift_sync
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool partnersHere, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
}
