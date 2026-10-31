/*
 team build, nightcoops teambuild (BuildStats 141). the script half is NightCoopTeamBuild in scripts/NightCoopMac/NightCoopTeamBuild.reds
 evry second our players 11 stats from nightcoops table (attributes, their skill check bonuses, level, street cred, hacking) get
 read thru the stats system, they go to evryone as BuildStats when they changed and at least evry 5 s
 partners stats are kept per peer (forgotten after 15 s without one). on the host only guests in its world count, like nightcoops
 build_checks_any. for each build the game checks (cool, netrunner, reflexes, solo, techie) the best partner score is worked out
 like nightcoop does (cool and reflexes as they are, the other three plus their skill check bonus, 20 at most) and handed to the
 script, whose RPGManager.GetBuildScore wrap lets the higher of ours and theirs count. lifted checks get logged
 the native quest stat checks (build_checks_native) are not done
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

namespace nightcoop_mac::team_build
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, const LogFunction& log);
}
