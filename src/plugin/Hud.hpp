/*
 hud lines for the plugin, like nightcoops hud Show: the text goes to the NightCoopHud script service
 (scripts/NightCoopMac/NightCoopDowned.reds), which posts a SimpleScreenMessage on UI_Notifications. main thread only
*/
#pragma once

#include <string>

namespace nightcoop_mac::hud
{
void Show(const std::string& text, float seconds);
}
