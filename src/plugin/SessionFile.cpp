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
        else if (keyword == "bodies")
        {
            std::string setting;
            words >> setting;
            plan.Bodies.Enabled = setting != "off";
        }
        else if (keyword == "body")
        {
            std::string kind;
            words >> kind;
            if (kind != "player" && kind != "npc" && kind != "player_without_scripts")
            {
                problem = "session.txt: body takes player or npc, not " + kind;
                return std::nullopt;
            }
            plan.Bodies.PlayerBody = kind != "npc";
            plan.Bodies.PlayerBodyWithoutScripts = kind == "player_without_scripts";
        }
        else if (keyword == "auto_continue")
        {
            plan.AutoContinue = true;
        }
        else if (keyword == "bodies_in_menu")
        {
            plan.Bodies.InMenuForTesting = true;
        }
        else if (keyword == "body_record")
        {
            words >> plan.Bodies.Record;
            plan.Bodies.Template.clear();
        }
        else if (keyword == "body_template")
        {
            std::getline(words >> std::ws, plan.Bodies.Template);
        }
        else if (keyword == "body_gender")
        {
            words >> plan.Bodies.Gender;
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
