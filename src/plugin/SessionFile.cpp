#include "plugin/SessionFile.hpp"

#include <fstream>
#include <sstream>

namespace nightcoop_mac
{
std::optional<SessionPlan> ReadSessionPlan(const std::string& path, std::string& problem)
{
    std::ifstream file(path);
    if (!file)
    {
        return std::nullopt;
    }
    SessionPlan plan;
    bool sawAction = false;
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream words(line);
        std::string keyword;
        if (!(words >> keyword) || keyword[0] == '#')
        {
            continue;
        }
        if (keyword == "host")
        {
            plan.IsHost = true;
            sawAction = static_cast<bool>(words >> plan.Hub);
        }
        else if (keyword == "join")
        {
            plan.IsHost = false;
            sawAction = static_cast<bool>(words >> plan.JoinCode >> plan.Hub);
        }
        else if (keyword == "name")
        {
            std::getline(words >> std::ws, plan.Nickname);
        }
        else if (keyword == "sampler")
        {
            std::string setting;
            words >> setting;
            plan.SamplePlayer = setting != "off";
        }
        else
        {
            problem = "unknown line in session.txt: " + line;
            return std::nullopt;
        }
    }
    if (!sawAction)
    {
        problem = "session.txt needs a 'host HUB' or 'join CODE HUB' line";
        return std::nullopt;
    }
    return plan;
}
}
