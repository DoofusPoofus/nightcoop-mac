/*
 session.txt next to the plugin says what to do once the game is running
   host HUB              host a session on that hub
   join CODE HUB         join a session by its code
   name NICK             the name others see (optional)
   sampler off           dont read our own player (testing, also means no partner bodies)
   bodies off            dont spawn partner bodies
   body_record REC       the npc record partner bodies are spawned from (default Character.Panam)
   body_template PATH    spawn partner bodies from an .ent template instead of a record
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
