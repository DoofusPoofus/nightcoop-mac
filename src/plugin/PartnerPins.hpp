/*
 partner pins, the map half of nightcoops playermarkers: evry partner gets a pin on the minimap and the world map
 nightcoop attaches a CPO_RemotePlayerVariant mappin (Mappins.CPO_RemotePlayerMappinDefinition, active, seen thru walls,
 the partners name as caption) to the partner body. mac bodies only exist within 150 m, so the pin goes at the partners
 PlayerState spot instead (RegisterMappin) and follows them with SetMappinPosition when they moved over 1 m, checked evry
 250 ms like nightcoop. name changes go thru SetMappinDebugCaption, a partner that left gets UnregisterMappin, a new mappin
 system (new world) forgets the old ids. outside a loaded save there are no pins. the look of the pin comes from
 scripts/NightCoopMac/NightCoopPartnerMarkers.reds
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::partner_pins
{
using LogFunction = std::function<void(const std::string&)>;

struct PartnerSpot
{
    uint32_t PeerNumber = 0;
    float X = 0;
    float Y = 0;
    float Z = 0;
    std::string Name;
};

void Tick(uint64_t nowMs, bool inSave, const std::vector<PartnerSpot>& partners, const LogFunction& log);
uint32_t PinCount();
}
