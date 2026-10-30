/*
 crash notes: when the game dies on a SIGSEGV / SIGBUS this names the rtti class of evry register that points at a game
 object (its vtable sits in the game image and GetType answers) and writes that to crash-notes.txt next to the plugin,
 then hands the signal on to whoever had it before (RED4exts crash handler). debugging aid for crashes in game code
 where the stack alone says nothing (the mac binary has almost no symbols)
 others put their handlers in later (the game, its crash reporter), so KeepCrashNotesOnTop checks evry few seconds that ours is
 still the first one and chains to whoever took over
*/
#pragma once

#include <filesystem>

namespace nightcoop_mac
{
void InstallCrashNotes(const std::filesystem::path& pluginFolder);
void KeepCrashNotesOnTop();
}
