/*
 world handoff a first cut of nightcoops way for a guest to play in the hosts world (WorldWant 80 WorldFile 81
 WorldStatus 82; the script half is NightCoopWorldHandoff in scripts/NightCoopMac/NightCoopWorldHandoff.reds)
 host: a WorldWant from a guest starts a job. it asks the game for a manual save (ManualSave-) waits for the newest save
 folder whose sav.dat got written after that and stopped growing (60 s at most WorldStatus 4 when it never shows up)
 tells the guest its folder name (WorldStatus 3) and streams metadata.*.json then sav.dat as WorldFile chunks of 32 KiB
 8 per tick each file with its fnv1a64. WorldStatus 6 / 7 / 8 from the guest get logged and shown
 guest (only with handoff in session.txt): once the roster says the host is in its world it sends WorldWant 1 collects the
 chunks checks sizes and hashes writes them as a new ManualSave-N folder (never over an existing one listed in the plugins
 handoff/mod-saves.txt its metadata name set to the new folder and its timestamp to now) and loads it thru the save list
 (LoadSavedGame by its index) or as the newest save (LoadLastCheckpoint(false)) when the list does not answer in 10 s.
 WorldStatus 6 when the load starts
 7 once our world is up again 8 with the reason when it fails
 when no in game load works (the save list callback the menus quick load LoadSavedGame(0) and LoadLastCheckpoint all stay
 quiet on mac) the game restarts straight into the written save: handoff/arriving.txt gets the folder a detached relauncher
 waits for the game to close and starts launch_red4ext.sh -save=<folder> and the game closes itself. on the next start the
 marker plus that -save argument mean we arrived (WorldStatus 7). handoff_restart off in session.txt turns this off
 not done yet: splicing our own character into the hosts save (nightcoops savepkg) saving locks reloads of the host
 (WorldReload 95) sync saves and the return to our own world
*/
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::world_handoff
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool joinHostWorld, net::RelaySession& session, const std::filesystem::path& pluginFolder,
          const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
void AllowRestart(bool allowed);
bool InHostWorld();
}
