/*
 test keys: one key press (down and up) posted into the games own event queue (NSApp postEvent, in process so no accessibility
 permission). auto_continue presses space on the press any key screen, test_moves presses c (crouch) in a save. kept apart from RED4ext headers, objc and RED4ext disagree on BOOL
*/
#pragma once

namespace nightcoop_mac
{
void PressKey(unsigned short keyCode, const char* characters);
void PressSpaceKey();
void PressCrouchKey();
}
