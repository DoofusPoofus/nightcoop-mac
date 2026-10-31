/*
 items, the transaction system calls the wardrobe and the weapons both need (all rtti, main thread only)
 give an item to a body, put it in an attachment slot (all 11 AddItemToSlot args, scene plane), take a slot empty, ask what
 sits in a slot (GetItemInSlot -> the item objects GetItemID, GetActiveItemInSlot as the fallback since it only knows weapon
 slots) and read tweakdb flats like a records placementSlots. EquipmentData is a players EquipmentSystemPlayerData (thru the
 scriptable systems container). Ready says once if the calls look different than expected
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::items
{
using LogFunction = std::function<void(const std::string&)>;

struct ItemId
{
    uint64_t Record = 0;
    uint32_t RngSeed = 0;
    uint16_t UniqueCounter = 0;
    uint8_t Structure = 0;
    uint8_t Flags = 0;
};
static_assert(sizeof(ItemId) == 16);

bool Ready(const LogFunction& log);
RED4ext::Handle<RED4ext::IScriptable> TransactionSystem();
RED4ext::Handle<RED4ext::IScriptable> EquipmentData(RED4ext::Handle<RED4ext::IScriptable>& player);
ItemId ItemFromRecord(uint64_t record);
ItemId ItemOf(RED4ext::Handle<RED4ext::IScriptable>& itemObject);
bool Has(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, ItemId item);
bool Give(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, ItemId item);
bool PutInSlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot, ItemId item,
               bool highPriority);
void EmptySlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot);
ItemId ItemInSlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot);
RED4ext::Handle<RED4ext::IScriptable> ObjectInSlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body,
                                                   uint64_t slot);
std::vector<uint64_t> ForeignKeys(uint64_t record, const char* flatName);
uint64_t ForeignKey(uint64_t record, const char* flatName);
std::vector<RED4ext::CName> Names(uint64_t record, const char* flatName);
uint64_t PlacementSlot(uint64_t record);
bool RecordExists(uint64_t record);
}
