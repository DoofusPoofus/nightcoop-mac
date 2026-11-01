/*
 background run: the mac game stops its world while the game window is not up front (a co-op host would freeze evryone).
 GameWindowDelegate windowDidBecomeKey: / windowDidResignKey: tell a list of c++ engine listeners (vfunc 0x38 true / false)
 and a window that never got focus never told them true at all (they even wait in the lists add queue: the games own
 flush runs first). KeepRunning(true) tells evry listener true once from the main thread while the window is not key
 (checked again evry 5 s for new ones) and again after evry real focus loss. thats all it touches: the cursor clip and
 warp hang on GameWindow m_isActive and the mute on GameWindowController so those still go like the game wants.
 Install swaps in our own windowDidResignKey: / windowDidBecomeKey: / windowDidChangeOcclusionState: (they log the listener
 types and call the games own ones) TakeNotes hands those lines to the main log. KeepRunning(false) gives the pause back.
 kept apart from RED4ext headers cuz objc and RED4ext disagree on BOOL
*/
#pragma once

#include <cstdint>
#include <string>

namespace nightcoop_mac::background_run
{
void Install();
void KeepRunning(bool keep, uint64_t nowMs);
std::string TakeNotes();
}
