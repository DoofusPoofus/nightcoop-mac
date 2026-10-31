/*
 session.txt next to the plugin says what to do once the game is running
   host HUB              host a session on that hub
   join CODE HUB         join a session by its code
   name NICK             the name others see (optional)
   sampler off           dont read our own player (testing, also means no partner bodies)
   bodies off            dont spawn partner bodies
   body player|npc       player: nightcoops real body, a second PlayerPuppet built by PartnerBodyBuilder (default, needs
                         its hook and the script side, else the cutscene V body is used)
                         npc: the cutscene V body (nightcoops fallback body, walks with npc move policies where it can)
                         player_without_scripts: player body even when its scripts are not compiled (testing only)
   bodies_in_menu        let bodies spawn in the main menu too (testing only, the session flow normally keeps them out)
   auto_continue         the main menu loads the newest save by itself (testing only)
   test_moves            in a save the plugin presses c (crouch) 8 times, 3 s apart (testing only)
   test_help             in a save f gets held down for 2 s evry 8 s, to help a downed probe up (testing only)
   test_downed           in a save our health drops to 1 % once a partner is there, so the fall and a revive can be checked (testing only)
   test_vehicle          in a save a car spawns next to our player and we get mounted at its wheel (testing only)
   test_weapon           in a save our player gets a Lexington drawn and once its in hand its magazine drops by one evry 2 s, 10 times,
                         so the weapon fields and shot counts can be checked with nobody at the keyboard (testing only)
   body_gender male|female  force the V body gender instead of taking it from the partners look
   body_record REC       spawn partner bodies from an npc record instead of the cutscene V
   body_template PATH    spawn partner bodies from another .ent template
 HUB is host:port#FINGERPRINT. lines starting with # are skipped. no file means the plugin just idles
*/
#pragma once

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
    bool TestHelp = false;
    BodySettings Bodies;
};

std::optional<SessionPlan> ReadSessionPlan(const std::string& path, std::string& problem);
}
