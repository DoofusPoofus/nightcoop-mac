/*
 session.txt next to the plugin says what to do once the game is running
   host HUB              host a session on that hub
   join CODE HUB         join a session by its code
   name NICK             the name others see (optional)
   sampler off           dont read our own player (testing also means no partner bodies)
   bodies off            dont spawn partner bodies
   body player|npc       player: nightcoops real body a second PlayerPuppet built by PartnerBodyBuilder (default needs
                         its hook and the script side else the cutscene V body is used)
                         npc: the cutscene V body (nightcoops fallback body walks with npc move policies where it can)
                         player_without_scripts: player body even when its scripts are not compiled (testing only)
   bodies_in_menu        let bodies spawn in the main menu too (testing only the session flow normally keeps them out)
   auto_continue         the main menu loads the newest save by itself (testing only)
   test_moves            in a save the plugin presses c (crouch) 8 times 3 s apart (testing only)
   share_npcs            share our npcs with evry partner even when they are not in our world (testing only they see doubles)
   share_doors           share doors with evry partner even when the world handoff did not put us in one world (testing only)
   share_devices         share devices turrets and barrels with evry partner even outside the hosts world (testing only)
   handoff               as a guest ask for the hosts world once it is up and load the save it sends (nightcoops world
                         handoff for now the hosts save as it is without our own character spliced in)
   quests off|any_world  quest sharing (nightcoops mirror mode on by default): off turns it off any_world makes a guest
                         take the hosts facts and journal even when it did not come in thru the handoff (testing only they
                         land in our own save)
   handoff_character off play the hosts V in its world instead of putting our own V into its save
   handoff_restart off   never restart the game to get into the hosts world when no in game load works
   test_help             in a save f gets held down for 2 s evry 8 s to help a downed probe up (testing only)
   test_downed           in a save our health drops to 1 % once a partner is there so the fall and a revive can be checked (testing only)
   phone any_world       take the hosts calls (guest) or send ours (host) in any world not only the hosts (testing only)
   rewards any_world     take the hosts quest rewards (guest) or send ours (host) in any world not only the hosts (testing only)
   deaths any_world      take the hosts npc deaths (guest) or send ours (host) in any world not only the hosts (testing only)
   blasts any_world      send our grenade blasts and take the partners blasts and effects in any world not only the same one (testing only)
   test_character        in a save our level attributes and perks get captured and applied back after 30 s (testing only)
   test_autosave         40 s after a guest arrived in the hosts world the game gets asked for an autosave (testing only)
   test_devices          in a save the next device within 50 m gets a ToggleON from our player evry 15 s (testing only)
   test_doors            in a save the nearest door gets opened and closed evry 12 s from script (testing only)
   test_claim            in a save our script claims 40 % of the first partner npc copy evry 5 s as if our player hit it (testing only)
   test_vehicle          in a save a car spawns next to our player and we get mounted at its wheel (testing only)
   test_loot [REC [N]]   loot sharing on even outside the hosts world and 10 s after a partner is there N x REC (default
                         1 x Items.FirstAidWhiffV0) goes to them thru NightCoopGiveItem 10 s later one gets dropped at our
                         feet as a shared drop (testing only it tops our own stock up first)
   test_weapon           in a save our player gets a Lexington drawn and once its in hand its magazine drops by one evry 2 s 10 times
                         so the weapon fields and shot counts can be checked with nobody at the keyboard (testing only)
   body_gender male|female  force the V body gender instead of taking it from the partners look
   body_record REC       spawn partner bodies from an npc record instead of the cutscene V
   body_template PATH    spawn partner bodies from another .ent template
   dialogs off|everyone  off: no choice votes (each player picks alone like the game). everyone: evry partner counts as in the
                         hosts world and near for votes and subtitles wherever they are (testing with a probe)
 HUB is host:port#FINGERPRINT. lines starting with # are skipped. no file means the plugin just idles
*/
#pragma once

#include "plugin/DialogScenes.hpp"
#include "plugin/RemoteBodies.hpp"

#include <optional>
#include <string>

namespace nightcoop_mac
{
struct SessionPlan
{
    bool IsHost = false;
    std::string JoinCode;
    std::string Hub;
    std::string Nickname = "guest";
    bool SamplePlayer = true;
    bool AutoContinue = false;
    bool TestMoves = false;
    bool TestWeapon = false;
    bool TestVehicle = false;
    bool TestDowned = false;
    bool TestClaim = false;
    bool TestDoors = false;
    bool TestDevices = false;
    bool TestCharacter = false;
    bool TestAutosave = false;
    bool RewardsAnyWorld = false;
    bool PhoneAnyWorld = false;
    bool DeathsAnyWorld = false;
    bool BlastsAnyWorld = false;
    bool TestHelp = false;
    bool WorldHandoff = false;
    bool HandoffRestart = true;
    bool HandoffCharacter = true;
    bool ShareNpcsAlways = false;
    bool ShareDoors = false;
    bool QuestSync = true;
    bool QuestsInAnyWorld = false;
    bool TestLoot = false;
    std::string TestLootRecord = "Items.FirstAidWhiffV0";
    int32_t TestLootQuantity = 1;
    bool ShareDevicesAlways = false;
    BodySettings Bodies;
    dialog_scenes::DialogSettings Dialogs;
};

std::optional<SessionPlan> ReadSessionPlan(const std::string& path, std::string& problem);
}
