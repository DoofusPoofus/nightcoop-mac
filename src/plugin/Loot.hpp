/*
 loot, the one door Main uses for nightcoops lootsync and worldloot: messages 38..41 go to ItemGive, 42..44 to WorldLoot and
 one tick runs both
 like nightcoop loot is only shared while the guest is in the hosts world (the host shares with guests whose presence says so),
 test_loot in session.txt forces it on. LootStates of an owner that is not in our world are skipped
 evry 2 s both status lines go to the script (NightCoopInventoryStatus / NightCoopLootStatus), changed ones get logged evry 20 s
 test_loot RECORD QUANTITY (testing only): 10 s after a save is up with a partner around we top our player up to QUANTITY + 1 of
 RECORD and give QUANTITY to the first partner thru the redscript call, 10 s later one of it gets dropped at our feet as a
 shared drop
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class RemoteBodies;
}

namespace nightcoop_mac::net
{
class RelaySession;
struct PlayerState;
}

namespace nightcoop_mac::loot
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, const net::PlayerState* ourPlayer, bool testLoot, const std::string& testRecord, int32_t testQuantity,
          net::RelaySession& session, RemoteBodies& bodies, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
}
