#include "plugin/SessionFlow.hpp"

#include "plugin/GameCalls.hpp"
#include "plugin/TestKeys.hpp"

#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

namespace nightcoop_mac
{
namespace
{
constexpr int32_t SessionRunningBit = 1;
constexpr int32_t PreGameBit = 2;
constexpr uint64_t LookAgainEveryMs = 2000;

bool PropertyIs(RED4ext::CProperty* property, const char* typeName)
{
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName;
}
}

bool SessionFlow::Resolve()
{
    m_getContainer = game::FindFunction("ScriptGameInstance", "GetScriptableServiceContainer");
    m_getService = game::FindFunction("ScriptableServiceContainer", "GetService");
    m_flowClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopSessionFlow");
    m_sessionRunning = m_flowClass ? m_flowClass->GetProperty("sessionRunning") : nullptr;
    m_preGame = m_flowClass ? m_flowClass->GetProperty("preGame") : nullptr;
    m_sessionsStarted = m_flowClass ? m_flowClass->GetProperty("sessionsStarted") : nullptr;
    m_autoContinue = m_flowClass ? m_flowClass->GetProperty("autoContinue") : nullptr;
    m_menuSteps = m_flowClass ? m_flowClass->GetProperty("menuSteps") : nullptr;
    m_containerTakesGameInstance = game::ParamsAre(m_getContainer, {"ScriptGameInstance"});
    return (game::ParamsAre(m_getContainer, {}) || m_containerTakesGameInstance) &&
           game::ReturnIs(m_getContainer, "handle:ScriptableServiceContainer") &&
           game::ParamsAre(m_getService, {"CName"}) && game::ReturnIs(m_getService, "handle:ScriptableService") &&
           PropertyIs(m_sessionRunning, "Bool") && PropertyIs(m_preGame, "Bool") && PropertyIs(m_sessionsStarted, "Int32");
}

RED4ext::Handle<RED4ext::IScriptable> SessionFlow::Service()
{
    RED4ext::Handle<RED4ext::IScriptable> container;
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::StackArgs_t containerArgs;
    if (m_containerTakesGameInstance)
    {
        containerArgs.emplace_back(nullptr, &gameInstance);
    }
    if (!game::CallStatic(m_getContainer, &container, containerArgs) || !container)
    {
        return {};
    }
    RED4ext::Handle<RED4ext::IScriptable> service;
    RED4ext::CName serviceName("NightCoopSessionFlow");
    RED4ext::StackArgs_t serviceArgs;
    serviceArgs.emplace_back(nullptr, &serviceName);
    if (!game::CallMethod(container.instance, m_getService, &service, serviceArgs) || !service || !service.instance->GetType() ||
        !service.instance->GetType()->IsA(m_flowClass))
    {
        return {};
    }
    return service;
}

int32_t SessionFlow::ReadState()
{
    const auto service = Service();
    if (!service)
    {
        return -1;
    }
    int32_t state = *m_sessionsStarted->GetValuePtr<int32_t>(service.instance) * 4;
    state += *m_sessionRunning->GetValuePtr<bool>(service.instance) ? SessionRunningBit : 0;
    state += *m_preGame->GetValuePtr<bool>(service.instance) ? PreGameBit : 0;
    return state;
}

void SessionFlow::AskForAutoContinue()
{
    if (m_autoContinueAsked || !m_resolved || !m_autoContinue || !m_autoContinue->type ||
        std::string(m_autoContinue->type->GetName().ToString()) != "Bool")
    {
        return;
    }
    const auto service = Service();
    if (!service)
    {
        return;
    }
    *m_autoContinue->GetValuePtr<bool>(service.instance) = true;
    m_autoContinueAsked = true;
    m_log("session flow: auto continue is on (testing), the main menu loads the newest save by itself");
}

bool SessionFlow::BodiesAllowed(uint64_t nowMs)
{
    if (!m_resolved && nowMs >= m_nextLookupMs)
    {
        m_nextLookupMs = nowMs + LookAgainEveryMs;
        m_resolved = Resolve();
        if (!m_resolved && !m_missingLogged)
        {
            m_missingLogged = true;
            m_log(std::string("session flow: no script half yet (NightCoopSessionFlow ") + (m_flowClass ? "there" : "missing") +
                  ", " + game::Describe(m_getContainer) + ", " + game::Describe(m_getService) +
                  "), needs codeware, redscript for mac and scripts/NightCoopMac. no partner bodies until it shows up");
        }
    }
    if (!m_resolved)
    {
        return false;
    }
    const int32_t state = ReadState();
    if (m_autoContinueAsked && m_menuSteps && nowMs >= m_nextStepsCheckMs)
    {
        m_nextStepsCheckMs = nowMs + 1000;
        if (const auto service = Service())
        {
            const int32_t steps = *m_menuSteps->GetValuePtr<int32_t>(service.instance);
            if (steps != m_lastMenuSteps)
            {
                m_lastMenuSteps = steps;
                m_log("session flow: menu steps so far " + std::to_string(steps) + " (1 start screen, 2 saves listed, 3 no saves, 4 load asked)");
            }
            if (steps % 10 == 1 && m_spacePresses < 10)
            {
                ++m_spacePresses;
                PressSpaceKey();
                m_log("session flow: pressed space on the press any key screen (auto continue)");
            }
        }
    }
    if (state != m_lastState)
    {
        m_lastState = state;
        if (state < 0)
        {
            m_log("session flow: the NightCoopSessionFlow service is not up, no partner bodies");
        }
        else if (!(state & SessionRunningBit))
        {
            m_log("session flow: no game session runs (or its ending), no partner bodies");
        }
        else
        {
            m_log("session flow: game session #" + std::to_string(state >> 2) +
                  ((state & PreGameBit) ? " is the main menu, no partner bodies here" : " is a loaded save, partner bodies allowed"));
        }
    }
    return state >= 0 && (state & SessionRunningBit) && !(state & PreGameBit);
}
}
