#include "net/QuestMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>

namespace nightcoop_mac::net
{
namespace
{
constexpr uint8_t WriterFollowsBit = 0x80;
constexpr size_t SmallestFactBytes = 9;
constexpr size_t SmallestJournalEntryBytes = 6;

bool BatchKindOk(uint8_t batch)
{
    return batch >= kQuestDelta && batch <= kQuestSnapshotEnd;
}

void PutWriter(nc::proto::Writer& writer, const QuestWriter& questWriter)
{
    if (questWriter.Known)
    {
        writer.PutU32(questWriter.Graph);
        writer.PutU16(questWriter.Node);
    }
}

void GetWriter(nc::proto::Reader& reader, QuestWriter& questWriter)
{
    if (questWriter.Known)
    {
        questWriter.Graph = reader.GetU32();
        questWriter.Node = reader.GetU16();
    }
}

std::vector<size_t> ChunkStarts(size_t entryCount)
{
    std::vector<size_t> starts;
    for (size_t start = 0; start < entryCount; start += kQuestMostEntries)
    {
        starts.push_back(start);
    }
    return starts;
}
}

uint32_t FactIdOf(std::string_view factName)
{
    uint32_t hash = 0x811c9dc5u;
    for (const char character : factName)
    {
        hash = (hash ^ static_cast<uint8_t>(character)) * 0x01000193u;
    }
    return hash;
}

std::vector<uint8_t> WriteQuestFacts(const QuestFacts& facts)
{
    nc::proto::Writer writer(nc::proto::MsgType::QuestFacts);
    writer.PutU8(facts.Batch);
    writer.PutU32(facts.Number);
    writer.PutU32(facts.Total);
    const auto count = static_cast<uint16_t>(std::min(facts.Facts.size(), kQuestMostEntries));
    writer.PutU16(count);
    for (uint16_t index = 0; index < count; ++index)
    {
        const auto& fact = facts.Facts[index];
        writer.PutU8(static_cast<uint8_t>((fact.Table & 0x7F) | (fact.Writer.Known ? WriterFollowsBit : 0)));
        writer.PutU32(fact.FactId);
        writer.PutU32(static_cast<uint32_t>(fact.Value));
        PutWriter(writer, fact.Writer);
    }
    return writer.Data();
}

bool ReadQuestFacts(const std::vector<uint8_t>& message, QuestFacts& facts)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::QuestFacts)
    {
        return false;
    }
    facts.Batch = reader.GetU8();
    facts.Number = reader.GetU32();
    facts.Total = reader.GetU32();
    const uint16_t count = reader.GetU16();
    if (!reader.Ok() || !BatchKindOk(facts.Batch) || count > kQuestMostEntries || count * SmallestFactBytes > reader.Remaining())
    {
        return false;
    }
    facts.Facts.assign(count, QuestFact {});
    for (auto& fact : facts.Facts)
    {
        const uint8_t tableByte = reader.GetU8();
        fact.Table = tableByte & 0x7F;
        fact.Writer.Known = (tableByte & WriterFollowsBit) != 0;
        fact.FactId = reader.GetU32();
        fact.Value = static_cast<int32_t>(reader.GetU32());
        GetWriter(reader, fact.Writer);
        if (fact.Table < 1 || fact.Table > kQuestMostTables)
        {
            return false;
        }
    }
    return reader.Ok() && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteQuestJournal(const QuestJournal& journal)
{
    nc::proto::Writer writer(nc::proto::MsgType::QuestJournal);
    writer.PutU8(journal.Batch);
    writer.PutU32(journal.Number);
    writer.PutU32(journal.Total);
    writer.PutU8(journal.TrackedKnown ? 1 : 0);
    writer.PutU32(journal.Tracked);
    const auto count = static_cast<uint16_t>(std::min(journal.Entries.size(), kQuestMostEntries));
    writer.PutU16(count);
    for (uint16_t index = 0; index < count; ++index)
    {
        const auto& entry = journal.Entries[index];
        writer.PutU32(entry.EntryHash);
        writer.PutU8(entry.State);
        writer.PutU8(static_cast<uint8_t>((entry.Notify & 0x7F) | (entry.Writer.Known ? WriterFollowsBit : 0)));
        PutWriter(writer, entry.Writer);
    }
    return writer.Data();
}

bool ReadQuestJournal(const std::vector<uint8_t>& message, QuestJournal& journal)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::QuestJournal)
    {
        return false;
    }
    journal.Batch = reader.GetU8();
    journal.Number = reader.GetU32();
    journal.Total = reader.GetU32();
    const uint8_t trackedByte = reader.GetU8();
    journal.Tracked = reader.GetU32();
    const uint16_t count = reader.GetU16();
    if (!reader.Ok() || !BatchKindOk(journal.Batch) || trackedByte > 1 || count > kQuestMostEntries ||
        count * SmallestJournalEntryBytes > reader.Remaining())
    {
        return false;
    }
    journal.TrackedKnown = trackedByte == 1;
    journal.Entries.assign(count, QuestJournalEntry {});
    for (auto& entry : journal.Entries)
    {
        entry.EntryHash = reader.GetU32();
        entry.State = reader.GetU8();
        const uint8_t notifyByte = reader.GetU8();
        entry.Notify = notifyByte & 0x7F;
        entry.Writer.Known = (notifyByte & WriterFollowsBit) != 0;
        GetWriter(reader, entry.Writer);
        if (entry.State > kJournalMostState || (entry.Notify != kJournalQuiet && entry.Notify != kJournalNotify))
        {
            return false;
        }
    }
    return reader.Ok() && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteQuestWant(uint32_t worldNumber)
{
    nc::proto::Writer writer(nc::proto::MsgType::QuestWant);
    writer.PutU32(worldNumber);
    return writer.Data();
}

bool ReadQuestWant(const std::vector<uint8_t>& message, uint32_t& worldNumber)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::QuestWant)
    {
        return false;
    }
    worldNumber = reader.GetU32();
    return reader.Ok() && reader.Remaining() == 0;
}

std::vector<std::vector<uint8_t>> SplitQuestFacts(bool snapshot, uint32_t number, const std::vector<QuestFact>& facts)
{
    std::vector<std::vector<uint8_t>> messages;
    QuestFacts batch;
    batch.Number = snapshot ? number : 0;
    batch.Total = static_cast<uint32_t>(facts.size());
    if (snapshot)
    {
        batch.Batch = kQuestSnapshotBegin;
        messages.push_back(WriteQuestFacts(batch));
    }
    batch.Batch = snapshot ? kQuestSnapshotPart : kQuestDelta;
    for (const size_t start : ChunkStarts(facts.size()))
    {
        const size_t end = std::min(start + kQuestMostEntries, facts.size());
        batch.Facts.assign(facts.begin() + static_cast<std::ptrdiff_t>(start), facts.begin() + static_cast<std::ptrdiff_t>(end));
        messages.push_back(WriteQuestFacts(batch));
    }
    batch.Facts.clear();
    if (snapshot)
    {
        batch.Batch = kQuestSnapshotEnd;
        messages.push_back(WriteQuestFacts(batch));
    }
    else if (facts.empty())
    {
        messages.push_back(WriteQuestFacts(batch));
    }
    return messages;
}

std::vector<std::vector<uint8_t>> SplitQuestJournal(bool snapshot, uint32_t number, bool trackedKnown, uint32_t tracked,
                                                    const std::vector<QuestJournalEntry>& entries)
{
    std::vector<std::vector<uint8_t>> messages;
    QuestJournal batch;
    batch.Number = snapshot ? number : 0;
    batch.Total = static_cast<uint32_t>(entries.size());
    batch.TrackedKnown = trackedKnown;
    batch.Tracked = tracked;
    if (snapshot)
    {
        batch.Batch = kQuestSnapshotBegin;
        messages.push_back(WriteQuestJournal(batch));
    }
    batch.Batch = snapshot ? kQuestSnapshotPart : kQuestDelta;
    for (const size_t start : ChunkStarts(entries.size()))
    {
        const size_t end = std::min(start + kQuestMostEntries, entries.size());
        batch.Entries.assign(entries.begin() + static_cast<std::ptrdiff_t>(start), entries.begin() + static_cast<std::ptrdiff_t>(end));
        messages.push_back(WriteQuestJournal(batch));
    }
    batch.Entries.clear();
    if (snapshot)
    {
        batch.Batch = kQuestSnapshotEnd;
        messages.push_back(WriteQuestJournal(batch));
    }
    else if (entries.empty())
    {
        messages.push_back(WriteQuestJournal(batch));
    }
    return messages;
}
}
