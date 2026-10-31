#include "plugin/PartnerPins.hpp"

#include "plugin/GameCalls.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <array>
#include <cmath>
#include <map>

namespace nightcoop_mac::partner_pins
{
namespace
{
constexpr uint64_t CheckEveryMs = 250;
constexpr float MoveAfterMeters = 1.0f;

using MappinId = std::array<uint8_t, 16>;

struct Pin
{
    MappinId Id {};
    float X = 0;
    float Y = 0;
    float Z = 0;
    std::string Name;
};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* GetMappinSystem = nullptr;
    RED4ext::CBaseFunction* RegisterMappin = nullptr;
    RED4ext::CBaseFunction* SetMappinPosition = nullptr;
    RED4ext::CBaseFunction* SetMappinDebugCaption = nullptr;
    RED4ext::CBaseFunction* UnregisterMappin = nullptr;
    RED4ext::CClass* MappinData = nullptr;
    RED4ext::CProperty* MappinType = nullptr;
    RED4ext::CProperty* Variant = nullptr;
    RED4ext::CProperty* Active = nullptr;
    RED4ext::CProperty* Caption = nullptr;
    RED4ext::CProperty* ThroughWalls = nullptr;
    uint32_t RemotePlayerVariant = 0;
};

Calls Game;
std::map<uint32_t, Pin> Pins;
void* PinsSystem = nullptr;
uint64_t NextCheckMs = 0;

bool PropertyIs(RED4ext::CProperty* property, const char* typeName)
{
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName;
}

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* rtti = RED4ext::CRTTISystem::Get();
    Game.GetMappinSystem = game::FindFunction("ScriptGameInstance", "GetMappinSystem");
    Game.RegisterMappin = game::FindFunction("gamemappinsMappinSystem", "RegisterMappin");
    Game.SetMappinPosition = game::FindFunction("gamemappinsMappinSystem", "SetMappinPosition");
    Game.SetMappinDebugCaption = game::FindFunction("gamemappinsMappinSystem", "SetMappinDebugCaption");
    Game.UnregisterMappin = game::FindFunction("gamemappinsMappinSystem", "UnregisterMappin");
    Game.MappinData = rtti->GetClass("gamemappinsMappinData");
    Game.MappinType = Game.MappinData ? Game.MappinData->GetProperty("mappinType") : nullptr;
    Game.Variant = Game.MappinData ? Game.MappinData->GetProperty("variant") : nullptr;
    Game.Active = Game.MappinData ? Game.MappinData->GetProperty("active") : nullptr;
    Game.Caption = Game.MappinData ? Game.MappinData->GetProperty("debugCaption") : nullptr;
    Game.ThroughWalls = Game.MappinData ? Game.MappinData->GetProperty("visibleThroughWalls") : nullptr;
    bool variantFound = false;
    if (auto* variants = rtti->GetEnum("gamedataMappinVariant"))
    {
        const RED4ext::CName remotePlayer("CPO_RemotePlayerVariant");
        for (uint32_t index = 0; index < variants->hashList.Size() && index < variants->valueList.Size(); ++index)
        {
            if (variants->hashList[index] == remotePlayer)
            {
                Game.RemotePlayerVariant = static_cast<uint32_t>(variants->valueList[index]);
                variantFound = true;
            }
        }
    }
    auto* idType = rtti->GetType("gameNewMappinID");
    Game.Usable = game::ParamsAre(Game.GetMappinSystem, {"ScriptGameInstance"}) &&
                  game::ParamsAre(Game.RegisterMappin, {"gamemappinsMappinData", "Vector4"}) &&
                  game::ParamsAre(Game.SetMappinPosition, {"gameNewMappinID", "Vector4"}) &&
                  game::ParamsAre(Game.SetMappinDebugCaption, {"gameNewMappinID", "String"}) &&
                  game::ParamsAre(Game.UnregisterMappin, {"gameNewMappinID"}) && PropertyIs(Game.MappinType, "TweakDBID") &&
                  PropertyIs(Game.Variant, "gamedataMappinVariant") && PropertyIs(Game.Active, "Bool") && PropertyIs(Game.Caption, "String") &&
                  PropertyIs(Game.ThroughWalls, "Bool") && variantFound && idType && idType->GetSize() <= sizeof(MappinId);
    log(Game.Usable ? "pins: partners get a CPO_RemotePlayerVariant pin on the minimap and the world map"
                    : "pins: the mappin system looks different than expected, no partner pins");
    return Game.Usable;
}

RED4ext::Handle<RED4ext::IScriptable> MappinSystem()
{
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> system;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    game::CallStatic(Game.GetMappinSystem, &system, args);
    return system;
}

bool Register(RED4ext::Handle<RED4ext::IScriptable>& system, const PartnerSpot& partner, Pin& pin)
{
    std::vector<uint8_t> data(Game.MappinData->GetSize());
    Game.MappinData->Construct(data.data());
    Game.MappinType->GetValuePtr<RED4ext::TweakDBID>(data.data())->value = RED4ext::TweakDBID("Mappins.CPO_RemotePlayerMappinDefinition").value;
    *Game.Variant->GetValuePtr<uint32_t>(data.data()) = Game.RemotePlayerVariant;
    *Game.Active->GetValuePtr<bool>(data.data()) = true;
    *Game.ThroughWalls->GetValuePtr<bool>(data.data()) = true;
    *Game.Caption->GetValuePtr<RED4ext::CString>(data.data()) = RED4ext::CString(partner.Name.c_str());
    RED4ext::Vector4 position {partner.X, partner.Y, partner.Z, 1.0f};
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, data.data());
    args.emplace_back(nullptr, &position);
    pin.Id.fill(0);
    const bool registered = game::CallMethod(system.instance, Game.RegisterMappin, pin.Id.data(), args);
    Game.MappinData->Destruct(data.data());
    pin.X = partner.X;
    pin.Y = partner.Y;
    pin.Z = partner.Z;
    pin.Name = partner.Name;
    return registered && pin.Id != MappinId {};
}

void Unregister(RED4ext::Handle<RED4ext::IScriptable>& system, Pin& pin)
{
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, pin.Id.data());
    game::CallMethod(system.instance, Game.UnregisterMappin, nullptr, args);
}
}

void Tick(uint64_t nowMs, bool inSave, const std::vector<PartnerSpot>& partners, const LogFunction& log)
{
    if (nowMs < NextCheckMs)
    {
        return;
    }
    NextCheckMs = nowMs + CheckEveryMs;
    if (!inSave)
    {
        Pins.clear();
        PinsSystem = nullptr;
        return;
    }
    if (!Resolve(log))
    {
        return;
    }
    auto system = MappinSystem();
    if (!system)
    {
        return;
    }
    if (system.instance != PinsSystem)
    {
        Pins.clear();
        PinsSystem = system.instance;
    }
    std::map<uint32_t, const PartnerSpot*> present;
    for (const auto& partner : partners)
    {
        present[partner.PeerNumber] = &partner;
    }
    for (auto pinEntry = Pins.begin(); pinEntry != Pins.end();)
    {
        if (!present.count(pinEntry->first))
        {
            Unregister(system, pinEntry->second);
            log("pins: peer " + std::to_string(pinEntry->first) + " left, their pin is gone");
            pinEntry = Pins.erase(pinEntry);
        }
        else
        {
            ++pinEntry;
        }
    }
    for (const auto& [peerNumber, partner] : present)
    {
        if (!std::isfinite(partner->X) || !std::isfinite(partner->Y) || !std::isfinite(partner->Z))
        {
            continue;
        }
        auto existing = Pins.find(peerNumber);
        if (existing == Pins.end())
        {
            Pin pin;
            if (Register(system, *partner, pin))
            {
                Pins[peerNumber] = pin;
                log("pins: peer " + std::to_string(peerNumber) + " '" + partner->Name + "' has a pin now");
            }
            continue;
        }
        Pin& pin = existing->second;
        const float deltaX = partner->X - pin.X;
        const float deltaY = partner->Y - pin.Y;
        const float deltaZ = partner->Z - pin.Z;
        if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ > MoveAfterMeters * MoveAfterMeters)
        {
            RED4ext::Vector4 position {partner->X, partner->Y, partner->Z, 1.0f};
            RED4ext::StackArgs_t args;
            args.emplace_back(nullptr, pin.Id.data());
            args.emplace_back(nullptr, &position);
            game::CallMethod(system.instance, Game.SetMappinPosition, nullptr, args);
            pin.X = partner->X;
            pin.Y = partner->Y;
            pin.Z = partner->Z;
        }
        if (pin.Name != partner->Name)
        {
            RED4ext::CString caption(partner->Name.c_str());
            RED4ext::StackArgs_t args;
            args.emplace_back(nullptr, pin.Id.data());
            args.emplace_back(nullptr, &caption);
            game::CallMethod(system.instance, Game.SetMappinDebugCaption, nullptr, args);
            pin.Name = partner->Name;
        }
    }
}

uint32_t PinCount()
{
    return static_cast<uint32_t>(Pins.size());
}
}
