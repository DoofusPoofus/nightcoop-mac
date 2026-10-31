#include "plugin/Wardrobe.hpp"

#include "plugin/GameCalls.hpp"
#include "plugin/Items.hpp"

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/TweakDB.hpp>

#include <cstdio>
#include <cstring>
#include <set>

namespace nightcoop_mac::wardrobe
{
namespace
{
const char* ClothingAreas[] = {"Outfit", "OuterChest", "InnerChest", "Head", "Face", "Legs", "Feet"};
const char* ThirdPersonSlots[] = {"AttachmentSlots.Head", "AttachmentSlots.Eyes", "AttachmentSlots.Torso", "AttachmentSlots.Chest",
                                  "AttachmentSlots.Outfit", "AttachmentSlots.RightArm"};
using items::ItemId;

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetPlayer = nullptr;
    RED4ext::CBaseFunction* GetSystemsContainer = nullptr;
    RED4ext::CBaseFunction* GetSystem = nullptr;
    RED4ext::CBaseFunction* GetPlayerData = nullptr;
    RED4ext::CBaseFunction* IsBuildCensored = nullptr;
    RED4ext::CBaseFunction* ShouldShowGenitals = nullptr;
    RED4ext::CBaseFunction* IsSlotHidden = nullptr;
    RED4ext::CBaseFunction* GetVisualItemInSlot = nullptr;
    RED4ext::CBaseFunction* GetActiveItem = nullptr;
    RED4ext::CBaseFunction* GetGenderName = nullptr;
    RED4ext::CBaseFunction* GivePreviewItem = nullptr;
    RED4ext::CBaseFunction* CreatePreviewItemId = nullptr;
    RED4ext::CBaseFunction* GetItemAppearance = nullptr;
    RED4ext::CBaseFunction* ChangeItemAppearance = nullptr;
    RED4ext::CEnum* EquipmentAreaEnum = nullptr;
};

Calls Game;

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    Game.GetPlayer = rtti->GetFunction("GetPlayer;GameInstance");
    Game.GetSystemsContainer = game::FindFunction("ScriptGameInstance", "GetScriptableSystemsContainer");
    Game.GetSystem = game::FindFunction("gameScriptableSystemsContainer", "Get");
    Game.GetPlayerData = game::FindFunction("EquipmentSystem", "GetPlayerData");
    Game.IsBuildCensored = game::FindFunction("EquipmentSystemPlayerData", "IsBuildCensored");
    Game.ShouldShowGenitals = game::FindFunction("EquipmentSystemPlayerData", "ShouldShowGenitals");
    Game.IsSlotHidden = game::FindFunction("EquipmentSystemPlayerData", "IsSlotHidden");
    Game.GetVisualItemInSlot = game::FindFunction("EquipmentSystemPlayerData", "GetVisualItemInSlot");
    Game.GetActiveItem = game::FindFunction("EquipmentSystemPlayerData", "GetActiveItem");
    Game.GetGenderName = game::FindFunction("gamePuppet", "GetResolvedGenderName");
    Game.GivePreviewItem = game::FindFunction("gameTransactionSystem", "GivePreviewItemByItemID");
    Game.CreatePreviewItemId = game::FindFunction("gameTransactionSystem", "CreatePreviewItemID");
    Game.GetItemAppearance = game::FindFunction("gameTransactionSystem", "GetItemAppearance");
    Game.ChangeItemAppearance = game::FindFunction("gameTransactionSystem", "ChangeItemAppearanceByName");
    Game.EquipmentAreaEnum = rtti->GetEnum("gamedataEquipmentArea");
    Game.Usable =
        items::Ready(log) && Game.GetPlayer && game::ParamsAre(Game.GetSystemsContainer, {"ScriptGameInstance"}) &&
        game::ParamsAre(Game.GetSystem, {"CName"}) && game::ParamsAre(Game.GetPlayerData, {"handle:gameObject"}) &&
        game::ParamsAre(Game.IsBuildCensored, {}) && game::ParamsAre(Game.ShouldShowGenitals, {}) &&
        game::ParamsAre(Game.IsSlotHidden, {"gamedataEquipmentArea"}) &&
        game::ParamsAre(Game.GetVisualItemInSlot, {"gamedataEquipmentArea", "TweakDBID", "Bool"}) &&
        game::ParamsAre(Game.GetActiveItem, {"gamedataEquipmentArea", "Bool"}) && game::ParamsAre(Game.GetGenderName, {}) &&
        game::ParamsAre(Game.GivePreviewItem, {"handle:gameObject", "gameItemID"}) && game::ParamsAre(Game.CreatePreviewItemId, {"gameItemID"}) &&
        game::ParamsAre(Game.GetItemAppearance, {"handle:gameObject", "gameItemID"}) &&
        game::ParamsAre(Game.ChangeItemAppearance, {"handle:gameObject", "gameItemID", "CName"}) && Game.EquipmentAreaEnum;
    log(Game.Usable ? "wardrobe: item and equipment calls found, clothes can be read and put on bodies"
                    : "wardrobe: the item or equipment calls look different than expected, partners keep the bodys own clothes");
    return Game.Usable;
}

bool AreaValue(const char* name, uint32_t& value)
{
    const RED4ext::CName wanted(name);
    for (uint32_t index = 0; index < Game.EquipmentAreaEnum->hashList.Size() && index < Game.EquipmentAreaEnum->valueList.Size(); ++index)
    {
        if (Game.EquipmentAreaEnum->hashList[index] == wanted)
        {
            value = static_cast<uint32_t>(Game.EquipmentAreaEnum->valueList[index]);
            return true;
        }
    }
    return false;
}

std::string AppearanceOf(RED4ext::Handle<RED4ext::IScriptable>& transactions, RED4ext::Handle<RED4ext::IScriptable>& body, ItemId item)
{
    RED4ext::CName appearance;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &item);
    game::CallMethod(transactions.instance, Game.GetItemAppearance, &appearance, args);
    const char* text = appearance ? appearance.ToString() : nullptr;
    return text ? std::string(text) : std::string();
}
}

bool ReadOwnClothes(net::PlayerClothes& clothes, const LogFunction& log)
{
    if (!Resolve(log))
    {
        return false;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t playerArgs;
    playerArgs.emplace_back(nullptr, &gameInstance);
    RED4ext::Handle<RED4ext::IScriptable> container;
    RED4ext::StackArgs_t containerArgs;
    containerArgs.emplace_back(nullptr, &gameInstance);
    if (!game::CallStatic(Game.GetPlayer, &player, playerArgs) || !player || !game::CallStatic(Game.GetSystemsContainer, &container, containerArgs) ||
        !container)
    {
        return false;
    }
    RED4ext::CName equipmentName("EquipmentSystem");
    RED4ext::Handle<RED4ext::IScriptable> equipment;
    RED4ext::StackArgs_t systemArgs;
    systemArgs.emplace_back(nullptr, &equipmentName);
    RED4ext::Handle<RED4ext::IScriptable> playerData;
    RED4ext::StackArgs_t dataArgs;
    dataArgs.emplace_back(nullptr, &player);
    if (!game::CallMethod(container.instance, Game.GetSystem, &equipment, systemArgs) || !equipment ||
        !game::CallMethod(equipment.instance, Game.GetPlayerData, &playerData, dataArgs) || !playerData)
    {
        return false;
    }
    bool censored = false;
    bool genitals = false;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(playerData.instance, Game.IsBuildCensored, &censored, noArgs);
    game::CallMethod(playerData.instance, Game.ShouldShowGenitals, &genitals, noArgs);

    std::vector<const char*> areas(std::begin(ClothingAreas), std::end(ClothingAreas));
    if (censored)
    {
        areas.push_back("UnderwearTop");
    }
    if (censored || !genitals)
    {
        areas.push_back("UnderwearBottom");
    }
    clothes = net::PlayerClothes {};
    for (const char* areaName : areas)
    {
        uint32_t area = 0;
        if (!AreaValue(areaName, area))
        {
            continue;
        }
        bool hidden = false;
        RED4ext::StackArgs_t hiddenArgs;
        hiddenArgs.emplace_back(nullptr, &area);
        game::CallMethod(playerData.instance, Game.IsSlotHidden, &hidden, hiddenArgs);
        if (hidden)
        {
            continue;
        }
        ItemId item;
        RED4ext::TweakDBID anySlot;
        bool itemValid = false;
        RED4ext::StackArgs_t itemArgs;
        itemArgs.emplace_back(nullptr, &area);
        itemArgs.emplace_back(nullptr, &anySlot);
        itemArgs.emplace_back(nullptr, &itemValid);
        game::CallMethod(playerData.instance, Game.GetVisualItemInSlot, &item, itemArgs);
        if (!RED4ext::TweakDBID(item.Record).IsValid())
        {
            continue;
        }
        clothes.Items.push_back({item.Record, item.RngSeed, item.UniqueCounter, item.Structure, item.Flags});
        clothes.Slots.push_back(items::PlacementSlot(item.Record));
    }
    uint32_t armsArea = 0;
    ItemId arms;
    if (AreaValue("ArmsCW", armsArea))
    {
        bool leftHand = false;
        RED4ext::StackArgs_t armsArgs;
        armsArgs.emplace_back(nullptr, &armsArea);
        armsArgs.emplace_back(nullptr, &leftHand);
        game::CallMethod(playerData.instance, Game.GetActiveItem, &arms, armsArgs);
    }
    clothes.Arms = RED4ext::TweakDBID(arms.Record).IsValid() ? arms.Record : RED4ext::TweakDBID("Items.HolsteredFists").value;
    RED4ext::CName gender;
    game::CallMethod(player.instance, Game.GetGenderName, &gender, noArgs);
    clothes.Female = gender == RED4ext::CName("Female");
    return true;
}

std::string DescribeHead(RED4ext::Handle<RED4ext::IScriptable>& body)
{
    if (!Game.Usable)
    {
        return "no wardrobe calls";
    }
    auto transactions = items::TransactionSystem();
    if (!transactions)
    {
        return "no transaction system";
    }
    const ItemId head = items::ItemInSlot(transactions, body, RED4ext::TweakDBID("AttachmentSlots.TppHead").value);
    if (!RED4ext::TweakDBID(head.Record).IsValid())
    {
        return "TppHead is empty";
    }
    char record[32];
    std::snprintf(record, sizeof(record), "%#llx", static_cast<unsigned long long>(net::RecordWithoutOffset(head.Record)));
    const std::string appearance = AppearanceOf(transactions, body, head);
    return std::string("TppHead holds ") + record + " with appearance '" + (appearance.empty() ? "none" : appearance) + "'";
}

bool HeadIsThirdPerson(RED4ext::Handle<RED4ext::IScriptable>& body)
{
    if (!Game.Usable)
    {
        return false;
    }
    auto transactions = items::TransactionSystem();
    if (!transactions)
    {
        return false;
    }
    const ItemId head = items::ItemInSlot(transactions, body, RED4ext::TweakDBID("AttachmentSlots.TppHead").value);
    if (!RED4ext::TweakDBID(head.Record).IsValid() ||
        net::RecordWithoutOffset(head.Record) == RED4ext::TweakDBID("Items.PlayerFppHead").value)
    {
        return false;
    }
    return AppearanceOf(transactions, body, head).find("FPP") == std::string::npos;
}

uint32_t Dress(RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerClothes& clothes, Worn& worn, const LogFunction& log)
{
    if (!Resolve(log))
    {
        return 0;
    }
    auto transactions = items::TransactionSystem();
    if (!transactions)
    {
        return 0;
    }
    std::map<uint64_t, std::pair<uint64_t, net::LookItem>> wanted;
    uint32_t skipped = 0;
    for (size_t index = 0; index < clothes.Items.size(); ++index)
    {
        const auto& item = clothes.Items[index];
        const uint64_t record = net::RecordWithoutOffset(item.Record);
        if (!items::RecordExists(record))
        {
            ++skipped;
            continue;
        }
        uint64_t slot = items::PlacementSlot(record);
        if (!slot && index < clothes.Slots.size())
        {
            slot = net::RecordWithoutOffset(clothes.Slots[index]);
        }
        if (slot)
        {
            wanted[slot] = {record, item};
        }
    }

    for (auto wornEntry = worn.RecordBySlot.begin(); wornEntry != worn.RecordBySlot.end();)
    {
        const auto still = wanted.find(wornEntry->first);
        if (still == wanted.end() || still->second.first != wornEntry->second)
        {
            items::EmptySlot(transactions, body, wornEntry->first);
            wornEntry = worn.RecordBySlot.erase(wornEntry);
        }
        else
        {
            ++wornEntry;
        }
    }

    uint32_t placed = 0;
    const uint64_t headSlot = RED4ext::TweakDBID("AttachmentSlots.TppHead").value;
    const uint64_t headNow = net::RecordWithoutOffset(items::ItemInSlot(transactions, body, headSlot).Record);
    const bool firstPersonHead = headNow == RED4ext::TweakDBID("Items.PlayerFppHead").value;
    if (!worn.Head && (!RED4ext::TweakDBID(headNow).IsValid() || firstPersonHead))
    {
        if (firstPersonHead)
        {
            items::EmptySlot(transactions, body, headSlot);
        }
        const uint64_t head = RED4ext::TweakDBID(clothes.Female ? "Items.PlayerWaTppHead" : "Items.PlayerMaTppHead").value;
        if (items::RecordExists(head))
        {
            ItemId headItem = items::ItemFromRecord(head);
            items::Give(transactions, body, headItem);
            const uint64_t slot = items::PlacementSlot(head);
            if (items::PutInSlot(transactions, body, slot ? slot : headSlot, headItem, true))
            {
                worn.Head = head;
                ++placed;
            }
        }
    }
    const uint64_t arms = net::RecordWithoutOffset(clothes.Arms);
    if (arms != worn.Arms)
    {
        if (worn.ArmsSlot)
        {
            items::EmptySlot(transactions, body, worn.ArmsSlot);
        }
        worn.Arms = 0;
        worn.ArmsSlot = 0;
        if (arms && items::RecordExists(arms))
        {
            ItemId armsItem = items::ItemFromRecord(arms);
            items::Give(transactions, body, armsItem);
            uint64_t armsSlot = items::PlacementSlot(arms);
            if (!armsSlot)
            {
                armsSlot = RED4ext::TweakDBID("AttachmentSlots.RightArm").value;
            }
            if (items::PutInSlot(transactions, body, armsSlot, armsItem, false))
            {
                worn.Arms = arms;
                worn.ArmsSlot = armsSlot;
                ++placed;
            }
        }
    }

    for (const auto& [slot, entry] : wanted)
    {
        if (worn.RecordBySlot.count(slot))
        {
            continue;
        }
        ItemId item {net::RecordWithoutOffset(entry.second.Record), entry.second.RngSeed, entry.second.UniqueCounter, entry.second.Structure,
                           entry.second.Flags};
        bool given = false;
        RED4ext::StackArgs_t giveArgs;
        giveArgs.emplace_back(nullptr, &body);
        giveArgs.emplace_back(nullptr, &item);
        game::CallMethod(transactions.instance, Game.GivePreviewItem, &given, giveArgs);
        ItemId preview;
        RED4ext::StackArgs_t previewArgs;
        previewArgs.emplace_back(nullptr, &item);
        game::CallMethod(transactions.instance, Game.CreatePreviewItemId, &preview, previewArgs);
        if (!RED4ext::TweakDBID(preview.Record).IsValid())
        {
            preview = item;
        }
        if (items::PutInSlot(transactions, body, slot, preview, true))
        {
            worn.RecordBySlot[slot] = entry.first;
            ++placed;
        }
    }
    if (skipped)
    {
        log("wardrobe: " + std::to_string(skipped) + " piece(s) of the partners clothes have no record here and were left out");
    }
    return placed;
}

uint32_t TurnToThirdPerson(RED4ext::Handle<RED4ext::IScriptable>& body)
{
    if (!Game.Usable)
    {
        return 0;
    }
    auto transactions = items::TransactionSystem();
    if (!transactions)
    {
        return 0;
    }
    uint32_t turned = 0;
    for (const char* slotName : ThirdPersonSlots)
    {
        const ItemId item = items::ItemInSlot(transactions, body, RED4ext::TweakDBID(slotName).value);
        if (!RED4ext::TweakDBID(item.Record).IsValid())
        {
            continue;
        }
        std::string appearance = AppearanceOf(transactions, body, item);
        const auto fpp = appearance.find("&FPP");
        if (fpp == std::string::npos)
        {
            continue;
        }
        appearance.replace(fpp, 4, "&TPP");
        RED4ext::CName thirdPerson(appearance.c_str());
        ItemId target = item;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &body);
        args.emplace_back(nullptr, &target);
        args.emplace_back(nullptr, &thirdPerson);
        game::CallMethod(transactions.instance, Game.ChangeItemAppearance, nullptr, args);
        ++turned;
    }
    return turned;
}
}
