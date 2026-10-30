/*
 session.txt next to the plugin says what to do once the game is running
   host HUB              host a session on that hub
   join CODE HUB         join a session by its code
   name NICK             the name others see (optional)
   sampler off           dont read our own player (testing, also means no partner bodies)
   bodies off            dont spawn partner bodies
   body player|npc       player: nightcoops real body, a second PlayerPuppet built by PartnerBodyBuilder (needs its hook)
                         npc: the cutscene V body (default)
                         player_without_scripts: player body even when its scripts are not compiled (testing only)
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
    BodySettings Bodies;
};

std::optional<SessionPlan> ReadSessionPlan(const std::string& path, std::string& problem);
}
