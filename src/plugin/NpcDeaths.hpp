/*
 npc deaths the dead table of nightcoop (NpcDead 36 host -> evryone in its world / NpcDefeat 98 guest -> host)
 npc share tells us evry npc of ours it sees dead (the host: any npc of its world near a partner. a guest: npcs handed to it)
 the host keeps them in a table (1024 at most) sends new ones at once and the whole table evry 30 s to the partners in its world
 a guest in the hosts world sends its dead to the host as NpcDefeat. the host puts those in the table too
 whoever gets a death looks for the same person in its own world evry 3 s (same static entity id or same record and match keys
 within 3 m the nearest first) and kills it when its still alive. deaths that dont find anyone wait (the npc may stream in later)
 a new world drops the table. deaths any_world in session.txt takes the hosts deaths in any world (tests only)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
struct DeadPerson;
}

namespace nightcoop_mac::npc_deaths
{
using LogFunction = std::function<void(const std::string&)>;

bool Handles(uint8_t messageType);
void NoteDeath(const net::DeadPerson& person);
void Tick(uint64_t nowMs, bool inSave, bool weAreHost, const std::vector<uint32_t>& peersInOurWorld, bool takeHostDeaths, net::RelaySession& session,
          const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, bool takeHostDeaths, const LogFunction& log);
}
