/*
 PlayerCustomization (33), the whole look state of a players V, rebuilt from nightcoops PlayerCustomization::Write / Read
 every name is a raw CName hash, there are no strings. little endian, after the usual message header:
   u8 version (1, anything else is refused) | u32 data version (state +0xC0) | u8 body gender male (+0x44) | u8 brain gender male (+0x45)
   head, body and arms group lists (state +0x70 / +0x80 / +0x90), each:
     u16 group count (max 128), per group: u64 name
       u8 part count (max 255), per part: u64 resource (raRef path hash) | u64 definition | u64 name | u32 censor flag | u8 censor action
       u8 morph count (max 255), per morph: u64 region name | u64 target name | u32 censor flag | u8 censor action
   u8 perspective count (max 255) (+0xA0), each: u64 name | u64 fpp | u64 tpp
   u8 tag count (max 255) (+0xB0), each: u64 tag
 no clothes in here, those ride in PlayerLook (31). skin tone is part of a definition name
 PlayerLook (31), the clothes, rebuilt from nightcoops wardrobe module (writer / reader of the 0.1.2 dll):
   u8 female | u16 item count (max 64) | per item a 16 byte ItemID: u64 record (tdbid as sent, top 3 bytes are the senders tdb
   offset) | u32 rng seed | u16 unique counter | u8 structure | u8 flags | u64 arms record | u16 slot count | per item its u64
   placement slot (the tail is only read when its there, and dropped when the count is not the item count)
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
struct LookPart
{
    uint64_t Resource = 0;
    uint64_t Definition = 0;
    uint64_t Name = 0;
    uint32_t CensorFlag = 0;
    uint8_t CensorAction = 0;
};

struct LookMorph
{
    uint64_t RegionName = 0;
    uint64_t TargetName = 0;
    uint32_t CensorFlag = 0;
    uint8_t CensorAction = 0;
};

struct LookGroup
{
    uint64_t Name = 0;
    std::vector<LookPart> Parts;
    std::vector<LookMorph> Morphs;
};

struct LookPerspective
{
    uint64_t Name = 0;
    uint64_t Fpp = 0;
    uint64_t Tpp = 0;
};

struct PlayerCustomization
{
    uint32_t DataVersion = 0;
    bool BodyMale = false;
    bool BrainMale = false;
    std::vector<LookGroup> Head;
    std::vector<LookGroup> Body;
    std::vector<LookGroup> Arms;
    std::vector<LookPerspective> Perspectives;
    std::vector<uint64_t> Tags;
};

struct LookItem
{
    uint64_t Record = 0;
    uint32_t RngSeed = 0;
    uint16_t UniqueCounter = 0;
    uint8_t Structure = 0;
    uint8_t Flags = 0;
};

struct PlayerClothes
{
    bool Female = false;
    std::vector<LookItem> Items;
    uint64_t Arms = 0;
    std::vector<uint64_t> Slots;
};

std::vector<uint8_t> WritePlayerCustomization(const PlayerCustomization& look);
bool ReadPlayerCustomization(const std::vector<uint8_t>& message, PlayerCustomization& look);
uint64_t LookFingerprint(const std::vector<uint8_t>& message);
std::vector<uint8_t> WritePlayerClothes(const PlayerClothes& clothes);
bool ReadPlayerClothes(const std::vector<uint8_t>& message, PlayerClothes& clothes);
uint64_t RecordWithoutOffset(uint64_t record);
}
