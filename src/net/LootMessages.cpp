#include "net/LootMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr float FarthestCoordinate = 100000.0f;

void PutItemId(nc::proto::Writer& writer, const LootItemId& item)
{
    writer.PutU64(item.Record);
    writer.PutU32(item.RngSeed);
    writer.PutU16(item.UniqueCounter);
    writer.PutU8(item.Structure);
    writer.PutU8(item.Flags);
}

LootItemId GetItemId(nc::proto::Reader& reader)
{
    LootItemId item;
    item.Record = reader.GetU64();
    item.RngSeed = reader.GetU32();
    item.UniqueCounter = reader.GetU16();
    item.Structure = reader.GetU8();
    item.Flags = reader.GetU8();
    return item;
}

void PutEntry(nc::proto::Writer& writer, const LootEntry& entry)
{
    PutItemId(writer, entry.Item);
    writer.PutU32(entry.Quantity);
    writer.PutU8(entry.Quality);
}

LootEntry GetEntry(nc::proto::Reader& reader)
{
    LootEntry entry;
    entry.Item = GetItemId(reader);
    entry.Quantity = reader.GetU32();
    entry.Quality = reader.GetU8();
    return entry;
}

bool EntryOk(const LootEntry& entry)
{
    return entry.Item.Record != 0 && entry.Quantity >= 1 && entry.Quantity <= kMostLootQuantity &&
           (entry.Quality <= 10 || entry.Quality == kLootQualityUnknown);
}

bool KindOk(uint8_t kind)
{
    return kind >= kLootCorpse && kind <= kLootWorldCorpse;
}

bool TransferTypeOk(uint8_t type)
{
    return type == kItemTransferOffer || type == kItemTransferDebit || type == kItemTransferGrant;
}

bool FloatsOk(const float* values, size_t count, float limit)
{
    return std::all_of(values, values + count, [limit](float value) { return std::isfinite(value) && std::fabs(value) <= limit; });
}
}

std::vector<uint8_t> WriteItemTransfer(uint8_t type, const ItemTransfer& transfer)
{
    nc::proto::Writer writer(static_cast<nc::proto::MsgType>(type));
    writer.PutU64(transfer.TransferId);
    writer.PutU32(transfer.Recipient);
    PutItemId(writer, transfer.Item);
    writer.PutU32(transfer.Quantity);
    return writer.Data();
}

bool ReadItemTransfer(const std::vector<uint8_t>& message, uint8_t& type, ItemTransfer& transfer)
{
    nc::proto::Reader reader(message.data(), message.size());
    type = static_cast<uint8_t>(reader.ReadHeader());
    if (!TransferTypeOk(type))
    {
        return false;
    }
    transfer.TransferId = reader.GetU64();
    transfer.Recipient = reader.GetU32();
    transfer.Item = GetItemId(reader);
    transfer.Quantity = reader.GetU32();
    return reader.Ok() && reader.Remaining() == 0 && transfer.TransferId != 0 && transfer.Recipient != 0xFFFFFFFF && transfer.Item.Record != 0 &&
           transfer.Quantity >= 1 && transfer.Quantity <= kMostTransferQuantity;
}

std::vector<uint8_t> WriteItemTransferAck(const ItemTransferAck& ack)
{
    nc::proto::Writer writer(nc::proto::MsgType::ItemTransferAck);
    writer.PutU64(ack.TransferId);
    writer.PutU8(ack.Phase);
    writer.PutU8(ack.Ok ? 1 : 0);
    return writer.Data();
}

bool ReadItemTransferAck(const std::vector<uint8_t>& message, ItemTransferAck& ack)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::ItemTransferAck)
    {
        return false;
    }
    ack.TransferId = reader.GetU64();
    ack.Phase = reader.GetU8();
    const uint8_t ok = reader.GetU8();
    ack.Ok = ok == 1;
    return reader.Ok() && reader.Remaining() == 0 && ack.TransferId != 0 && (ack.Phase == kTransferPhaseDebit || ack.Phase == kTransferPhaseGrant) &&
           ok <= 1;
}

std::vector<uint8_t> WriteLootState(const LootState& state)
{
    nc::proto::Writer writer(nc::proto::MsgType::LootState);
    writer.PutU8(state.Kind);
    writer.PutU64(state.ContainerId);
    writer.PutU32(state.SourceNpc);
    writer.PutU32(state.Revision);
    writer.PutU8(state.State);
    writer.PutU8(state.Bag ? 1 : 0);
    writer.PutU64(state.NpcRecord);
    writer.PutU64(state.CommunityKeyLow);
    writer.PutU64(state.CommunityKeyHigh);
    for (float coordinate : state.Position)
    {
        writer.PutF32(coordinate);
    }
    for (float component : state.Orientation)
    {
        writer.PutF32(component);
    }
    const size_t entryCount = state.State == kLootOpen ? std::min(state.Entries.size(), kMostLootEntries) : 0;
    writer.PutU16(static_cast<uint16_t>(entryCount));
    for (size_t index = 0; index < entryCount; ++index)
    {
        PutEntry(writer, state.Entries[index]);
    }
    return writer.Data();
}

bool ReadLootState(const std::vector<uint8_t>& message, LootState& state)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::LootState)
    {
        return false;
    }
    state.Kind = reader.GetU8();
    state.ContainerId = reader.GetU64();
    state.SourceNpc = reader.GetU32();
    state.Revision = reader.GetU32();
    state.State = reader.GetU8();
    const uint8_t bag = reader.GetU8();
    state.Bag = bag == 1;
    state.NpcRecord = reader.GetU64();
    state.CommunityKeyLow = reader.GetU64();
    state.CommunityKeyHigh = reader.GetU64();
    for (float& coordinate : state.Position)
    {
        coordinate = reader.GetF32();
    }
    for (float& component : state.Orientation)
    {
        component = reader.GetF32();
    }
    const uint16_t entryCount = reader.GetU16();
    if (!reader.Ok() || !KindOk(state.Kind) || state.ContainerId == 0 || state.State > kLootLetGo || bag > 1 || entryCount > kMostLootEntries)
    {
        return false;
    }
    state.Entries.clear();
    bool entriesOk = true;
    for (uint16_t index = 0; index < entryCount && reader.Ok(); ++index)
    {
        state.Entries.push_back(GetEntry(reader));
        entriesOk = entriesOk && EntryOk(state.Entries.back());
    }
    return reader.Ok() && reader.Remaining() == 0 && entriesOk && FloatsOk(state.Position, 3, FarthestCoordinate) &&
           FloatsOk(state.Orientation, 4, 2.0f);
}

std::vector<uint8_t> WriteLootTake(const LootTake& take)
{
    nc::proto::Writer writer(nc::proto::MsgType::LootTake);
    writer.PutU8(take.Kind);
    writer.PutU64(take.ContainerId);
    writer.PutU32(take.TakeNumber);
    PutEntry(writer, take.Item);
    return writer.Data();
}

bool ReadLootTake(const std::vector<uint8_t>& message, LootTake& take)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::LootTake)
    {
        return false;
    }
    take.Kind = reader.GetU8();
    take.ContainerId = reader.GetU64();
    take.TakeNumber = reader.GetU32();
    take.Item = GetEntry(reader);
    return reader.Ok() && reader.Remaining() == 0 && KindOk(take.Kind) && take.ContainerId != 0 && take.TakeNumber != 0 && EntryOk(take.Item);
}

std::vector<uint8_t> WriteLootTakeResult(const LootTakeResult& result)
{
    nc::proto::Writer writer(nc::proto::MsgType::LootTakeResult);
    writer.PutU8(result.Kind);
    writer.PutU64(result.ContainerId);
    writer.PutU32(result.TakeNumber);
    writer.PutU8(result.Granted ? 1 : 0);
    return writer.Data();
}

bool ReadLootTakeResult(const std::vector<uint8_t>& message, LootTakeResult& result)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::LootTakeResult)
    {
        return false;
    }
    result.Kind = reader.GetU8();
    result.ContainerId = reader.GetU64();
    result.TakeNumber = reader.GetU32();
    const uint8_t granted = reader.GetU8();
    result.Granted = granted == 1;
    return reader.Ok() && reader.Remaining() == 0 && KindOk(result.Kind) && result.ContainerId != 0 && result.TakeNumber != 0 && granted <= 1;
}

uint64_t ItemRecordFromName(std::string_view recordName)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (char letter : recordName)
    {
        crc ^= static_cast<uint8_t>(letter);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return static_cast<uint64_t>(~crc) | (static_cast<uint64_t>(recordName.size() & 0xFF) << 32);
}
}
