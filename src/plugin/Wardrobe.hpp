/*
 wardrobe, the mac take on nightcoops wardrobe module (clothes and arms of a partner, PlayerLook 31). all rtti calls, no hooks
 our clothes: EquipmentSystem (thru the scriptable systems container) -> GetPlayerData(our player), then per area in nightcoops
 order (Outfit, OuterChest, InnerChest, Head, Face, Legs, Feet, plus UnderwearTop when the build is censored and UnderwearBottom
 when censored or no genitals): skip a hidden slot, else GetVisualItemInSlot, so transmog and wardrobe sets come along. arms
 are the active ArmsCW item or Items.HolsteredFists. each item gets the first entry of its records placementSlots flat
 dressing a body (main thread): arms with GiveItem + AddItemToSlot, each piece with GivePreviewItemByItemID +
 CreatePreviewItemID + AddItemToSlot (high priority) on the slot from our own record (the senders slot when we have none),
 then evry first person appearance on the tpp slots gets its &FPP turned into &TPP (whats in a slot comes from GetItemInSlot ->
 the item objects GetItemID, GetActiveItemInSlot only knows weapon slots). a changed look takes off wat is gone
 first (RemoveItemFromSlot). PlayerPuppet bodies wait until their third person head is in TppHead (8 s at most), and when
 TppHead is still empty or holds Items.PlayerFppHead (the faceless first person head, our tpp view event does not swap it) the
 body gets Items.PlayerWaTppHead / PlayerMaTppHead put on like nightcoop does for a body without a tpp view
*/
#pragma once

#include "net/LookMessages.hpp"

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace nightcoop_mac::wardrobe
{
using LogFunction = std::function<void(const std::string&)>;

struct Worn
{
    std::map<uint64_t, uint64_t> RecordBySlot;
    uint64_t Arms = 0;
    uint64_t ArmsSlot = 0;
    uint64_t Head = 0;
};

bool ReadOwnClothes(net::PlayerClothes& clothes, const LogFunction& log);
bool HeadIsThirdPerson(RED4ext::Handle<RED4ext::IScriptable>& body);
std::string DescribeHead(RED4ext::Handle<RED4ext::IScriptable>& body);
uint32_t Dress(RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerClothes& clothes, Worn& worn, const LogFunction& log);
uint32_t TurnToThirdPerson(RED4ext::Handle<RED4ext::IScriptable>& body);
}
