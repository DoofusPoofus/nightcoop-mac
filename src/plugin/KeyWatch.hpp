/*
 key watch: which keys are held down in the game window from an NSEvent local monitor (in process so no input monitoring
 permission and no prompt). the downed module uses it for nightcoops F (help a partner up) and T (pass out) holds.
 HoldKeyPresses makes the monitor swallow the real key downs of a key (the dialog votes hold F and enter while a vote is open)
 and count them for TakeHeldPresses. key ups always go thru so the game never keeps a key down and our own posted keys
 (TestKeys timestamp 0) are never swallowed.
 kept apart from RED4ext headers objc and RED4ext disagree on BOOL. WatchKeys has to run on the main thread
*/
#pragma once

namespace nightcoop_mac
{
inline constexpr unsigned short KeyF = 3;
inline constexpr unsigned short KeyT = 17;
inline constexpr unsigned short KeyReturn = 36;
inline constexpr unsigned short KeyDownArrow = 125;
inline constexpr unsigned short KeyUpArrow = 126;

void WatchKeys();
bool KeyHeld(unsigned short keyCode);
void HoldKeyPresses(unsigned short keyCode, bool hold);
unsigned TakeHeldPresses(unsigned short keyCode);
}
