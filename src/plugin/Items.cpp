#include "plugin/Items.hpp"

#include "net/LookMessages.hpp"
#include "plugin/GameCalls.hpp"

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/TweakDB.hpp>

namespace nightcoop_mac::items
{
namespace
{
constexpr uint32_t RenderPlaneScene = 0;

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetTransactionSystem = nullptr;
    RED4ext::CBaseFunction* FromTdbid = nullptr;
    RED4ext::CBaseFunction* HasItem = nullptr;
    RED4ext::CBaseFunction* GiveItem = nullptr;
    RED4ext::CBaseFunction* AddItemToSlot = nullptr;
    RED4ext::CBaseFunction* RemoveItemFromSlot = nullptr;
    RED4ext::CBaseFunction* GetActiveItemInSlot = nullptr;
    RED4ext::CBaseFunction* GetItemInSlot = nullptr;
    RED4ext::CBaseFunction* GetItemObjectId = nullptr;
};

Calls Game;

const RED4ext::CStackType* FlatValue(uint64_t record, const char* flatName, const char* typeName, RED4ext::CStackType& stack)
{
    auto* tweakDb = RED4ext::TweakDB::Get();
    if (!tweakDb || !record)
    {
        return nullptr;
    }
    auto* value = tweakDb->GetFlatValue(RED4ext::TweakDBID(RED4ext::TweakDBID(net::RecordWithoutOffset(record)), flatName));
    if (!value)
    {
        return nullptr;
    }
    stack = value->GetValue();
    if (!stack.type || !stack.value || std::string(stack.type->GetName().ToString()) != typeName)
    {
        return nullptr;
    }
    return &stack;
}
}

bool Ready(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    Game.GetTransactionSystem = game::FindFunction("ScriptGameInstance", "GetTransactionSystem");
    Game.FromTdbid = game::FindFunction("gameItemID", "FromTDBID");
    Game.HasItem = game::FindFunction("gameTransactionSystem", "HasItem");
    Game.GiveItem = game::FindFunction("gameTransactionSystem", "GiveItem");
    Game.AddItemToSlot = game::FindFunction("gameTransactionSystem", "AddItemToSlot");
    Game.RemoveItemFromSlot = game::FindFunction("gameTransactionSystem", "RemoveItemFromSlot");
    Game.GetActiveItemInSlot = game::FindFunction("gameTransactionSystem", "GetActiveItemInSlot");
    Game.GetItemInSlot = game::FindFunction("gameTransactionSystem", "GetItemInSlot");
    Game.GetItemObjectId = game::FindFunction("gameItemObject", "GetItemID");
    Game.Usable =
        game::ParamsAre(Game.GetTransactionSystem, {"ScriptGameInstance"}) && game::ParamsAre(Game.FromTdbid, {"TweakDBID"}) &&
        game::ParamsAre(Game.HasItem, {"handle:gameObject", "gameItemID"}) &&
        game::ParamsAre(Game.GiveItem, {"handle:gameObject", "gameItemID", "Int32", "array:CName"}) &&
        game::ParamsAre(Game.AddItemToSlot, {"handle:gameObject", "TweakDBID", "gameItemID", "Bool", "whandle:gameItemObject", "ERenderingPlane", "Bool",
                                             "Bool", "CName", "gameItemID", "Bool"}) &&
        game::ParamsAre(Game.RemoveItemFromSlot, {"handle:gameObject", "TweakDBID", "Bool", "Bool", "Bool"}) &&
        game::ParamsAre(Game.GetActiveItemInSlot, {"handle:gameObject", "TweakDBID"}) &&
        game::ParamsAre(Game.GetItemInSlot, {"handle:gameObject", "TweakDBID"}) && game::ParamsAre(Game.GetItemObjectId, {});
    if (!Game.Usable)
    {
        log("items: the transaction system calls look different than expected, nothing gets put on partner bodies");
    }
    return Game.Usable;
}

RED4ext::Handle<RED4ext::IScriptable> TransactionSystem()
{
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> system;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    game::CallStatic(Game.GetTransactionSystem, &system, args);
    return system;
}

ItemId ItemFromRecord(uint64_t record)
{
    ItemId item;
    RED4ext::TweakDBID tdbid(net::RecordWithoutOffset(record));
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &tdbid);
    game::CallStatic(Game.FromTdbid, &item, args);
    return item;
}

ItemId ItemOf(RED4ext::Handle<RED4ext::IScriptable>& itemObject)
{
    ItemId item;
    RED4ext::StackArgs_t noArgs;
    if (itemObject)
    {
        game::CallMethod(itemObject.instance, Game.GetItemObjectId, &item, noArgs);
    }
    return item;
}

bool Has(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, ItemId item)
{
    bool has = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &item);
    game::CallMethod(transactions.instance, Game.HasItem, &has, args);
    return has;
}

bool Give(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, ItemId item)
{
    int32_t amount = 1;
    RED4ext::DynArray<RED4ext::CName> noTags;
    bool given = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &item);
    args.emplace_back(nullptr, &amount);
    args.emplace_back(nullptr, &noTags);
    game::CallMethod(transactions.instance, Game.GiveItem, &given, args);
    return given;
}

bool PutInSlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot, ItemId item,
               bool highPriority)
{
    RED4ext::TweakDBID slotId(slot);
    RED4ext::WeakHandle<RED4ext::IScriptable> noItemObject;
    uint32_t plane = RenderPlaneScene;
    bool keepWorldTransform = false;
    bool ignoreRestrictions = false;
    RED4ext::CName noGarmentAppearance;
    ItemId noAppearanceItem;
    bool dontCache = false;
    bool placed = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &slotId);
    args.emplace_back(nullptr, &item);
    args.emplace_back(nullptr, &highPriority);
    args.emplace_back(nullptr, &noItemObject);
    args.emplace_back(nullptr, &plane);
    args.emplace_back(nullptr, &keepWorldTransform);
    args.emplace_back(nullptr, &ignoreRestrictions);
    args.emplace_back(nullptr, &noGarmentAppearance);
    args.emplace_back(nullptr, &noAppearanceItem);
    args.emplace_back(nullptr, &dontCache);
    game::CallMethod(transactions.instance, Game.AddItemToSlot, &placed, args);
    return placed;
}

void EmptySlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot)
{
    RED4ext::TweakDBID slotId(slot);
    bool destroyEntity = true;
    bool keepWorldTransform = false;
    bool skipAnimEvents = true;
    bool removed = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &slotId);
    args.emplace_back(nullptr, &destroyEntity);
    args.emplace_back(nullptr, &keepWorldTransform);
    args.emplace_back(nullptr, &skipAnimEvents);
    game::CallMethod(transactions.instance, Game.RemoveItemFromSlot, &removed, args);
}

RED4ext::Handle<RED4ext::IScriptable> ObjectInSlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body,
                                                   uint64_t slot)
{
    RED4ext::TweakDBID slotId(slot);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &slotId);
    RED4ext::Handle<RED4ext::IScriptable> itemObject;
    game::CallMethod(transactions.instance, Game.GetItemInSlot, &itemObject, args);
    return itemObject;
}

ItemId ItemInSlot(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot)
{
    auto itemObject = ObjectInSlot(transactions, body, slot);
    ItemId item = ItemOf(itemObject);
    if (RED4ext::TweakDBID(item.Record).IsValid())
    {
        return item;
    }
    item = ItemId {};
    RED4ext::TweakDBID slotId(slot);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &slotId);
    game::CallMethod(transactions.instance, Game.GetActiveItemInSlot, &item, args);
    return item;
}

std::vector<uint64_t> ForeignKeys(uint64_t record, const char* flatName)
{
    RED4ext::CStackType stack;
    if (!FlatValue(record, flatName, "array:TweakDBID", stack))
    {
        return {};
    }
    const auto* keys = static_cast<RED4ext::DynArray<RED4ext::TweakDBID>*>(stack.value);
    std::vector<uint64_t> found;
    for (uint32_t index = 0; index < keys->Size(); ++index)
    {
        found.push_back((*keys)[index].value);
    }
    return found;
}

uint64_t ForeignKey(uint64_t record, const char* flatName)
{
    RED4ext::CStackType stack;
    if (!FlatValue(record, flatName, "TweakDBID", stack))
    {
        return 0;
    }
    return net::RecordWithoutOffset(static_cast<RED4ext::TweakDBID*>(stack.value)->value);
}

std::vector<RED4ext::CName> Names(uint64_t record, const char* flatName)
{
    RED4ext::CStackType stack;
    if (!FlatValue(record, flatName, "array:CName", stack))
    {
        return {};
    }
    const auto* names = static_cast<RED4ext::DynArray<RED4ext::CName>*>(stack.value);
    return std::vector<RED4ext::CName>(names->begin(), names->end());
}

uint64_t PlacementSlot(uint64_t record)
{
    const auto slots = ForeignKeys(record, ".placementSlots");
    return slots.empty() ? 0 : net::RecordWithoutOffset(slots.front());
}

bool RecordExists(uint64_t record)
{
    auto* tweakDb = RED4ext::TweakDB::Get();
    return tweakDb && tweakDb->GetRecord(RED4ext::TweakDBID(net::RecordWithoutOffset(record)));
}
}
