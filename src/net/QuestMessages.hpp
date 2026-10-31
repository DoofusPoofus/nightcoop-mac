/*
 quest sharing, the mirror part of nightcoops quest sync rebuilt from the 0.1.2 dll. all three ride in Game envelopes, reliable
 QuestFacts (70) host -> guests: u8 batch (1 delta, 2 snapshot begin, 3 snapshot part, 4 snapshot end) | u32 number (0 for a
   delta, the snapshot number) | u32 total (entries of the whole batch) | u16 n (up to 1024) | n x (u8 table + 1 with bit 7 =
   a writer follows | u32 fact id (fnv1a32 of the fact name) | i32 value | writer: u32 graph, u16 node)
 QuestJournal (71) host -> guests: u8 batch | u32 number | u32 total | u8 1 = the tracked entry is valid | u32 tracked entry
   hash | u16 n (up to 1024) | n x (u32 entry hash | u8 state 0..4 | u8 1 quiet or 2 with notification, bit 7 = a writer
   follows | writer: u32 graph, u16 node)
 QuestWant (73) guest -> host: u32 our world number, after evry load in the hosts world
 a delta goes as batch 1 messages of 1024 entries at most, a snapshot as a begin and an end without entries and the parts
 between. readers want the exact length and drop broken limits like the dll does
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kQuestDelta = 1;
inline constexpr uint8_t kQuestSnapshotBegin = 2;
inline constexpr uint8_t kQuestSnapshotPart = 3;
inline constexpr uint8_t kQuestSnapshotEnd = 4;
inline constexpr size_t kQuestMostEntries = 1024;
inline constexpr uint8_t kQuestFactsTable = 1;
inline constexpr uint8_t kQuestMostTables = 10;
inline constexpr uint8_t kJournalQuiet = 1;
inline constexpr uint8_t kJournalNotify = 2;
inline constexpr uint8_t kJournalMostState = 4;

struct QuestWriter
{
    bool Known = false;
    uint32_t Graph = 0;
    uint16_t Node = 0;
};

struct QuestFact
{
    uint8_t Table = kQuestFactsTable;
    uint32_t FactId = 0;
    int32_t Value = 0;
    QuestWriter Writer;
};

struct QuestFacts
{
    uint8_t Batch = kQuestDelta;
    uint32_t Number = 0;
    uint32_t Total = 0;
    std::vector<QuestFact> Facts;
};

struct QuestJournalEntry
{
    uint32_t EntryHash = 0;
    uint8_t State = 0;
    uint8_t Notify = kJournalNotify;
    QuestWriter Writer;
};

struct QuestJournal
{
    uint8_t Batch = kQuestDelta;
    uint32_t Number = 0;
    uint32_t Total = 0;
    bool TrackedKnown = false;
    uint32_t Tracked = 0;
    std::vector<QuestJournalEntry> Entries;
};

uint32_t FactIdOf(std::string_view factName);

std::vector<uint8_t> WriteQuestFacts(const QuestFacts& facts);
bool ReadQuestFacts(const std::vector<uint8_t>& message, QuestFacts& facts);
std::vector<uint8_t> WriteQuestJournal(const QuestJournal& journal);
bool ReadQuestJournal(const std::vector<uint8_t>& message, QuestJournal& journal);
std::vector<uint8_t> WriteQuestWant(uint32_t worldNumber);
bool ReadQuestWant(const std::vector<uint8_t>& message, uint32_t& worldNumber);

std::vector<std::vector<uint8_t>> SplitQuestFacts(bool snapshot, uint32_t number, const std::vector<QuestFact>& facts);
std::vector<std::vector<uint8_t>> SplitQuestJournal(bool snapshot, uint32_t number, bool trackedKnown, uint32_t tracked,
                                                    const std::vector<QuestJournalEntry>& entries);
}
