#include "plugin/AnimSetup.hpp"

#include "net/LookMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

namespace nightcoop_mac::anim_setup
{
namespace
{
constexpr uint64_t CheckEveryMs = 2000;
constexpr uint64_t ResendEveryMs = 20000;

struct Types
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetPlayer = nullptr;
    RED4ext::CBaseFunction* GetComponents = nullptr;
    RED4ext::CClass* AnimatedClass = nullptr;
    RED4ext::CClass* ExtensionClass = nullptr;
    RED4ext::CProperty* ComponentName = nullptr;
    RED4ext::CProperty* Rig = nullptr;
    RED4ext::CProperty* AnimatedSetup = nullptr;
    RED4ext::CProperty* ExtensionSetup = nullptr;
    RED4ext::CProperty* Gameplay = nullptr;
    RED4ext::CProperty* EntryAnimSet = nullptr;
    RED4ext::CProperty* EntryPriority = nullptr;
    RED4ext::CProperty* EntryVariables = nullptr;
};

Types Game;
uint64_t NextCheckMs = 0;
uint64_t SentFingerprint = 0;
uint64_t SentAtMs = 0;

bool PropertyIs(RED4ext::CProperty* property, const char* typeName)
{
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName;
}

RED4ext::CProperty* PropertyOf(const char* className, const char* propertyName, const char* typeName)
{
    auto* owner = RED4ext::CRTTISystem::Get()->GetClass(className);
    auto* property = owner ? owner->GetProperty(propertyName) : nullptr;
    return PropertyIs(property, typeName) ? property : nullptr;
}

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    Game.GetPlayer = rtti->GetFunction("GetPlayer;GameInstance");
    Game.GetComponents = game::FindFunction("entEntity", "GetComponents");
    Game.AnimatedClass = rtti->GetClass("entAnimatedComponent");
    Game.ExtensionClass = rtti->GetClass("entAnimationSetupExtensionComponent");
    Game.ComponentName = PropertyOf("entIComponent", "name", "CName");
    Game.Rig = PropertyOf("entAnimatedComponent", "rig", "rRef:animRig");
    Game.AnimatedSetup = PropertyOf("entAnimatedComponent", "animations", "animAnimSetup");
    Game.ExtensionSetup = PropertyOf("entAnimationSetupExtensionComponent", "animations", "animAnimSetup");
    Game.Gameplay = PropertyOf("animAnimSetup", "gameplay", "array:animAnimSetupEntry");
    Game.EntryAnimSet = PropertyOf("animAnimSetupEntry", "animSet", "raRef:animAnimSet");
    Game.EntryPriority = PropertyOf("animAnimSetupEntry", "priority", "Uint8");
    Game.EntryVariables = PropertyOf("animAnimSetupEntry", "variableNames", "array:CName");
    Game.Usable = Game.GetPlayer && game::ParamsAre(Game.GetComponents, {}) && Game.AnimatedClass && Game.ExtensionClass && Game.ComponentName &&
                  Game.Rig && Game.AnimatedSetup && Game.ExtensionSetup && Game.Gameplay && Game.EntryAnimSet && Game.EntryPriority &&
                  Game.EntryVariables;
    if (!Game.Usable)
    {
        log("anim setup: the animated component types look different than expected, partners dont get our rig and animation sets");
    }
    return Game.Usable;
}

void AddSets(void* setup, net::AnimSetup& out)
{
    auto* gameplayType = static_cast<RED4ext::CRTTIBaseArrayType*>(Game.Gameplay->type);
    auto* entries = Game.Gameplay->GetValuePtr<uint8_t>(setup);
    const uint32_t count = gameplayType->GetLength(entries);
    for (uint32_t index = 0; index < count; ++index)
    {
        void* entry = gameplayType->GetElement(entries, index);
        net::AnimSetEntry set;
        set.AnimSet = *Game.EntryAnimSet->GetValuePtr<uint64_t>(entry);
        set.Priority = *Game.EntryPriority->GetValuePtr<uint8_t>(entry);
        const auto* variables = Game.EntryVariables->GetValuePtr<RED4ext::DynArray<RED4ext::CName>>(entry);
        for (const auto& variable : *variables)
        {
            set.VariableNames.push_back(variable.hash);
        }
        out.Sets.push_back(std::move(set));
    }
}
}

bool ReadOwn(net::AnimSetup& setup, const LogFunction& log)
{
    if (!Resolve(log))
    {
        return false;
    }
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t playerArgs;
    playerArgs.emplace_back(nullptr, &gameInstance);
    RED4ext::DynArray<RED4ext::Handle<RED4ext::IScriptable>> components;
    RED4ext::StackArgs_t noArgs;
    if (!game::CallStatic(Game.GetPlayer, &player, playerArgs) || !player ||
        !game::CallMethod(player.instance, Game.GetComponents, &components, noArgs))
    {
        return false;
    }
    setup = net::AnimSetup {};
    const RED4ext::CName rootName("root");
    bool rootFound = false;
    for (auto& component : components)
    {
        auto* componentClass = component ? component->GetType() : nullptr;
        if (componentClass && componentClass->IsA(Game.AnimatedClass) && *Game.ComponentName->GetValuePtr<RED4ext::CName>(component.instance) == rootName)
        {
            setup.Rig = *Game.Rig->GetValuePtr<uint64_t>(component.instance);
            AddSets(Game.AnimatedSetup->GetValuePtr<uint8_t>(component.instance), setup);
            rootFound = true;
            break;
        }
    }
    if (!rootFound)
    {
        return false;
    }
    for (auto& component : components)
    {
        auto* componentClass = component ? component->GetType() : nullptr;
        if (componentClass && componentClass->IsA(Game.ExtensionClass))
        {
            AddSets(Game.ExtensionSetup->GetValuePtr<uint8_t>(component.instance), setup);
        }
    }
    setup.SetCount = static_cast<uint16_t>(setup.Sets.size());
    return true;
}

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || nowMs < NextCheckMs || !session.GetStatus().InSession)
    {
        return;
    }
    NextCheckMs = nowMs + CheckEveryMs;
    net::AnimSetup setup;
    if (!ReadOwn(setup, log))
    {
        return;
    }
    const auto message = net::WriteAnimSetup(setup);
    const uint64_t fingerprint = net::LookFingerprint(message);
    if (fingerprint == SentFingerprint && nowMs - SentAtMs < ResendEveryMs)
    {
        return;
    }
    session.SendGameMessage(message);
    if (fingerprint != SentFingerprint)
    {
        char rig[32];
        std::snprintf(rig, sizeof(rig), "%#llx", static_cast<unsigned long long>(setup.Rig));
        log(std::string("anim setup: our rig ") + rig + " and " + std::to_string(setup.Sets.size()) + " animation set(s) go to partners (" +
            std::to_string(message.size()) + " bytes)");
    }
    SentFingerprint = fingerprint;
    SentAtMs = nowMs;
}
}
