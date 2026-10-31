/*
 door sync a first cut of nightcoops doorsync (DoorSync 88 kinds 1 and 2 / the script half is NightCoopDoorSync in
 scripts/NightCoopMac/NightCoopDoors.reds). it only runs while the world is shared: a guest in the hosts world with the host in
 it or a host with a guest in its world (share_doors in session.txt forces it for tests)
 evry 100 ms the script queues get read: doors that moved here / quest actions on a door / auto closes we let thru. each
 door gets tracked by its entity id hash and polled (open locked sealed) evry 500 ms while it was touched in the last 5 min
 else evry 1000 ms (48 polls per tick at most). a change here is ours unless it only moves the door towards the shared state
 host: a change here gets version = max(version + 1 / session clock) and goes to evry guest in our world as kind 2.
 a guests kind 1 gets decided like nightcoop (taken / already so / the hosts quest did it already / refused sealed / refused
 locked / kept open for someone at it) then taken ones go to evryone and the asker gets its request number back
 guest: a change here goes to the host as kind 1 and our door stays as it is until the answer (5 s at most). a kind 2 with a
 newer version (or the answer to our ask) becomes the shared state and our door gets moved to it with the quest force
 actions and OpenDoor / CloseDoor (waits up to 3 s while the door is busy / 6 tries)
 auto close guard: partner spots go to the script evry tick so a door does not close by itself in a partners face
 lift doors are skipped. not done yet: the quest catch up holds (doors_open_behind / doors_shut_behind / doors_ahead_undone /
 doors_own_shut) / doors opening for partner bodies (doors_bodies) / acts from other modules (doors_act_other)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac
{
struct RemotePeerView;
}

namespace nightcoop_mac::net
{
class RelaySession;
struct PlayerState;
}

namespace nightcoop_mac::door_sync
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool shareAlways, const net::PlayerState* ourState, const std::vector<RemotePeerView>& partners,
          net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, net::RelaySession& session, const LogFunction& log);
}
