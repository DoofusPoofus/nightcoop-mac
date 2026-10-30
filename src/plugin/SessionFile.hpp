/*
 session.txt next to the plugin says what to do once the game is running
   host HUB              host a session on that hub
   join CODE HUB         join a session by its code
   name NICK             the name others see (optional)
   sampler off           dont read our own player (testing)
 HUB is host:port#FINGERPRINT. lines starting with # are skipped. no file means the plugin just idles
*/
#pragma once

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
};

std::optional<SessionPlan> ReadSessionPlan(const std::string& path, std::string& problem);
}
