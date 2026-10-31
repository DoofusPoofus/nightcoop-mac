/*
 key watch: which keys are held down in the game window, from an NSEvent local monitor (in process, so no input monitoring
 permission and no prompt). the downed module uses it for nightcoops F (help a partner up) and T (pass out) holds.
 kept apart from RED4ext headers, objc and RED4ext disagree on BOOL. WatchKeys has to run on the main thread
*/
#pragma once

namespace nightcoop_mac
{
inline constexpr unsigned short KeyF = 3;
inline constexpr unsigned short KeyT = 17;

void WatchKeys();
bool KeyHeld(unsigned short keyCode);
}
