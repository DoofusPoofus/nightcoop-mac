/*
 loot messages nightcoops lootsync (item give) and worldloot (shared containers) rebuilt from the 0.1.2 dll. all reliable in
 Game envelopes. an item id is 16 bytes like the games ItemID: u64 record | u32 rng seed | u16 unique counter | u8 structure | u8 flags
 ItemTransferOffer (38) / Debit (39) / Grant (40) share one 32 byte body: u64 transfer id (sender peer << 32 | counter never 0) |
   u32 recipient peer (not 0xFFFFFFFF) | item id (record not 0) | u32 quantity 1..9999
   offer: sender -> host debit: host -> sender grant: host -> recipient
 ItemTransferAck (41) 10 bytes: u64 transfer id (not 0) | u8 phase (1 debit done from the sender / 2 grant done from the
   recipient) | u8 ok 0/1 always to the host
 LootState (42) owner -> evryone 73 + 21 per entry bytes: u8 kind (1 corpse of a shared npc 2 drop 3 world corpse) |
   u64 container id (not 0 drops are owner peer << 24 | a 24 bit counter) | u32 source npc net id (0 none) | u32 revision |
   u8 state (0 open 1 closed 2 let go entries only when open) | u8 bag (the owner has no item object for it) | u64 npc
   record (kind 3) | u64 community key low | u64 community key high | f32 x3 position | f32 x4 orientation (i j k r) |
   u16 n (max 64) | n x entry
 an entry is 21 bytes: item id | u32 quantity 1..9999999 | u8 quality (0 Common .. 10 LegendaryPlusPlus 0xFF unknown)
 LootTake (43) taker -> owner 34 bytes: u8 kind | u64 container id | u32 take number (not 0) | entry
 LootTakeResult (44) owner -> taker 14 bytes: u8 kind | u64 container id | u32 take number | u8 granted 0/1
 readers drop bytes left over out of range fields and positions that are not finite or past 100 km
 ItemRecordFromName makes a TweakDBID from a record name (crc32 | length << 32) for the probe and the tests
*/
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kItemTransferOffer = 38;
inline constexpr uint8_t kItemTransferDebit = 39;
inline constexpr uint8_t kItemTransferGrant = 40;
inline constexpr uint8_t kTransferPhaseDebit = 1;
inline constexpr uint8_t kTransferPhaseGrant = 2;
inline constexpr uint32_t kMostTransferQuantity = 9999;

inline constexpr uint8_t kLootCorpse = 1;
inline constexpr uint8_t kLootDrop = 2;
inline constexpr uint8_t kLootWorldCorpse = 3;
inline constexpr uint8_t kLootOpen = 0;
inline constexpr uint8_t kLootClosed = 1;
inline constexpr uint8_t kLootLetGo = 2;
inline constexpr size_t kMostLootEntries = 64;
inline constexpr uint32_t kMostLootQuantity = 9999999;
inline constexpr uint8_t kLootQualityUnknown = 0xFF;

struct LootItemId
{
    uint64_t Record = 0;
    uint32_t RngSeed = 0;
    uint16_t UniqueCounter = 0;
    uint8_t Structure = 0;
    uint8_t Flags = 0;

    bool operator==(const LootItemId&) const = default;
};

struct ItemTransfer
{
    uint64_t TransferId = 0;
    uint32_t Recipient = 0;
    LootItemId Item;
    uint32_t Quantity = 1;

    bool operator==(const ItemTransfer&) const = default;
};

struct ItemTransferAck
{
    uint64_t TransferId = 0;
    uint8_t Phase = kTransferPhaseDebit;
    bool Ok = true;
};

struct LootEntry
{
    LootItemId Item;
    uint32_t Quantity = 1;
    uint8_t Quality = kLootQualityUnknown;

    bool operator==(const LootEntry&) const = default;
};

struct LootState
{
    uint8_t Kind = kLootDrop;
    uint64_t ContainerId = 0;
    uint32_t SourceNpc = 0;
    uint32_t Revision = 0;
    uint8_t State = kLootOpen;
    bool Bag = false;
    uint64_t NpcRecord = 0;
    uint64_t CommunityKeyLow = 0;
    uint64_t CommunityKeyHigh = 0;
    float Position[3] = {0, 0, 0};
    float Orientation[4] = {0, 0, 0, 1};
    std::vector<LootEntry> Entries;
};

struct LootTake
{
    uint8_t Kind = kLootDrop;
    uint64_t ContainerId = 0;
    uint32_t TakeNumber = 0;
    LootEntry Item;
};

struct LootTakeResult
{
    uint8_t Kind = kLootDrop;
    uint64_t ContainerId = 0;
    uint32_t TakeNumber = 0;
    bool Granted = false;
};

std::vector<uint8_t> WriteItemTransfer(uint8_t type, const ItemTransfer& transfer);
bool ReadItemTransfer(const std::vector<uint8_t>& message, uint8_t& type, ItemTransfer& transfer);
std::vector<uint8_t> WriteItemTransferAck(const ItemTransferAck& ack);
bool ReadItemTransferAck(const std::vector<uint8_t>& message, ItemTransferAck& ack);
std::vector<uint8_t> WriteLootState(const LootState& state);
bool ReadLootState(const std::vector<uint8_t>& message, LootState& state);
std::vector<uint8_t> WriteLootTake(const LootTake& take);
bool ReadLootTake(const std::vector<uint8_t>& message, LootTake& take);
std::vector<uint8_t> WriteLootTakeResult(const LootTakeResult& result);
bool ReadLootTakeResult(const std::vector<uint8_t>& message, LootTakeResult& result);
uint64_t ItemRecordFromName(std::string_view recordName);
}
