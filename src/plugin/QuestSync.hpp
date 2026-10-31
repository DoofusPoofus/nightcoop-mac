/*
 quest sync the mirror mode of nightcoops quest sharing (QuestFacts 70 QuestJournal 71 QuestWant 73). the script half is
 NightCoopQuestSync in scripts/NightCoopMac/NightCoopQuests.reds facts go by id thru FactsDb. the host owns the story
 host: evry 250 ms facts table 1 gets listed and compared with the last listing the changed facts and the journal entry
   states the script caught go out as deltas to evry partner in its world (presence flag or a QuestWant from them) the
   tracked entry gets checked evry 1 s. a QuestWant gets the whole table 1 and the whole journal as snapshot begin / parts /
   end with a new snapshot number. the same world number asked again within 10 s gets nothing like in nightcoop
 guest: once in the hosts world (the handoff or any world with quests any_world) it sends QuestWant with its own world number
   3 s after evry load again after a link blink and after 20 s without a snapshot (3 tries). facts and journal entries from
   peer 0 get queued and written on the game thread 400 facts and 100 journal entries a frame: SetFact by id thru the facts
   db ChangeEntryStateByHash thru the script. the hosts tracked entry only gets logged
 facts each player keeps for themself (tutorials wanted level metro holo calls vendor item owned and character retrofix
 facts the lists of nightcoop) are never sent and never taken. the writer fields of nightcoops led story are read and dropped
 not done yet: led story following the hosts tracked objective (TrackModeWord 222) guest fact words (175 / 187 / 246)
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

namespace nightcoop_mac::quest_sync
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool enabled, bool inAnyWorld, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
}
