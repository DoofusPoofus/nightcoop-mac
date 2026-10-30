/*
 test keys: one space key press posted into the games own event queue (NSApp postEvent, in process so no accessibility
 permission). auto_continue uses it on the press any key screen. kept apart from RED4ext headers, objc and RED4ext disagree on BOOL
*/
#pragma once

namespace nightcoop_mac
{
void PressSpaceKey();
}
