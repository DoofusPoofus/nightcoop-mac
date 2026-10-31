/*
 facts db the native fact store of the games quests system. nightcoop reads and writes facts by id (the fnv1a32 of the fact
 name) which no script call takes so this goes to the db itself. found in the mac 2.5.0 binary by following the script
 natives questQuestsSystem.GetFact / SetFact (game+0x2a1195c / +0x2a11bb4): they get the db from the quests system (vtable
 +0x278 a getter of +0xf8) and call it at vtable +0x18 GetFact(table id) and +0x20 SetFact(table id value) with table 1.
 the db (vtable game+0x79e2540 built at game+0x27f22c4) holds 10 tables at +0x08 (table t at +0x08 + (t - 1) * 8 their
 count at +0x58) and +0x40 ListIds(table out ids) copies the ids of one table sorted into a DynArray under that tables lock.
 windows 2.31 has the same layout with the slots one lower (msvc has one destructor slot clang two)
 nothing gets touched unless the quests system and the db carry exactly the vtables of mac 2.5.0 those slots point at the
 functions seen there and their first code bytes match else the facts stay off with a log line. the first db also gets a
 few facts compared with the scripts own GetFactStr before anything is written. ListIds gets a buffer with room for twice the
 ids its table has so the game never grows it with its own allocator
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace nightcoop_mac::facts_db
{
using LogFunction = std::function<void(const std::string&)>;

bool Ready(const LogFunction& log);
bool TurnedOff();
bool ReadFact(uint8_t table, uint32_t factId, int32_t& value);
bool WriteFact(uint8_t table, uint32_t factId, int32_t value);
bool ListTable(uint8_t table, std::vector<std::pair<uint32_t, int32_t>>& facts);
}
