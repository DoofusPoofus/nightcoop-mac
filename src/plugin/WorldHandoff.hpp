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
 marker plus that -save argument mean we arrived (WorldStatus 7). handoff_restart off in session.txt turns this off. once a restart
 was needed handoff/loads-never-work.txt makes later handoffs restart right away
 the written save keeps our own V: the character nodes and the player and stash inventories of our newest own save (not a
 handoff one not PointOfNoReturn) go into the hosts save (save package). level attributes perks unspent points and the equipped item
 ids go around it (NightCoopCharacter Capture into handoff/character.txt before and Apply + EquipNow 15 s after we arrived). handoff_character off in session.txt plays the hosts V instead
 going home: 30 s after the session is gone a guest in the hosts world saves there takes that V (character nodes items stash)
 into the campaign save it came from (handoff/state.txt) writes that as a new save captures level perks and gear and restarts
 into it (marker kind home)
 the games ManualSave call ignores the folder name and writes into ManualSave-0 when nobody asked for the save list first (the
 list callback never comes on mac) so before evry save ManualSave-0 is copied aside and when the game wrote into it the new
 save is moved to the folder we wanted and the copy goes back
 host reloads: when the hosts world comes up again in a session (another save got loaded) evry partner that was in its old world gets
 WorldReload 1 and a new save job. the guest saves its V there first (that save is the character source) then takes the new
 world like a join
 autosaves: the game keeps autosaving while a guest plays in the hosts world. on arrival our AutoSave folders get cloned into
 handoff/autosaves. evry autosave the game writes there is listed in handoff/host-autosaves.txt and the first start in our own
 world moves those into handoff/host-autosaves and puts our own back (only when nothing touched them since)
 not done yet: item stats and the rest of the player systems in the scriptable systems blob (crafting recipes and co) saving locks
 and sync saves
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
void AskForAutosave(bool wanted);
void KeepOurCharacter(bool keep);
void TestCharacter(uint64_t nowMs, bool inSave, const LogFunction& log);
bool InHostWorld();
}
