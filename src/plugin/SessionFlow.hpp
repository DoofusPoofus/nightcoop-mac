/*
 session flow, the plugin half of the mac take on nightcoops SessionFlow
 nightcoop follows the world its player is in and keeps partner bodies out of the main menu (IsPreGame from the ui
 requests handler, which only a menu controller hands out). on mac the script half (scripts/NightCoopMac/NightCoopSessionFlow.reds)
 listens to codewares session events and this reads its fields thru rtti (codewares ScriptableServiceContainer hands out
 the service, the fields sit in its script value holder):
   bodies may exist only while a game session runs that is not the menu
   evry change gets logged (session #n started in the menu or in a save, the session is ending)
 without the script half (no redscript for mac) the menu and a save look the same from here, so then there are no bodies.
 the scripts land in rtti a bit after the Running state starts, so the lookup tries again evry 2 s
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace RED4ext
{
struct CBaseFunction;
struct CProperty;
struct CClass;
}

namespace nightcoop_mac
{
class SessionFlow
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    explicit SessionFlow(LogFunction log)
        : m_log(std::move(log))
    {
    }

    bool BodiesAllowed(uint64_t nowMs);

private:
    LogFunction m_log;
    bool Resolve();
    int32_t ReadState();

    RED4ext::CBaseFunction* m_getContainer = nullptr;
    RED4ext::CBaseFunction* m_getService = nullptr;
    RED4ext::CClass* m_flowClass = nullptr;
    RED4ext::CProperty* m_sessionRunning = nullptr;
    RED4ext::CProperty* m_preGame = nullptr;
    RED4ext::CProperty* m_sessionsStarted = nullptr;
    bool m_containerTakesGameInstance = false;
    bool m_resolved = false;
    uint64_t m_nextLookupMs = 0;
    bool m_missingLogged = false;
    int32_t m_lastState = -2;
};
}
