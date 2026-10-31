#include "plugin/AnimPlayer.hpp"

#include "plugin/GameCalls.hpp"

#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <cstring>

namespace nightcoop_mac
{
namespace
{
constexpr uint8_t PlayerGraphSlot = 0;

struct QuaternionLayout
{
    float I;
    float J;
    float K;
    float R;
};
}

bool AnimPlayer::Ready()
{
    if (m_resolved)
    {
        return m_available;
    }
    m_resolved = true;
    m_getAnimationController = game::FindFunction("ScriptedPuppet", "GetAnimationControllerComponent");
    m_pushEvent = game::FindFunction("entAnimationControllerComponent", "PushEvent");
    m_setInputFloat = game::FindFunction("entAnimationControllerComponent", "SetInputFloat");
    m_setInputInt = game::FindFunction("entAnimationControllerComponent", "SetInputInt");
    m_setInputBool = game::FindFunction("entAnimationControllerComponent", "SetInputBool");
    m_setInputVector = game::FindFunction("entAnimationControllerComponent", "SetInputVector");
    m_setInputQuaternion = game::FindFunction("entAnimationControllerComponent", "SetInputQuaternion");
    m_applyFeature = game::FindFunction("entAnimationControllerComponent", "ApplyFeature");
    m_scriptedPuppetClass = RED4ext::CRTTISystem::Get()->GetClass("ScriptedPuppet");
    m_animFeatureClass = RED4ext::CRTTISystem::Get()->GetClass("animAnimFeature");
    m_available = m_scriptedPuppetClass && m_animFeatureClass && game::ParamsAre(m_getAnimationController, {}) &&
                  game::ReturnIs(m_getAnimationController, "handle:entAnimationControllerComponent") &&
                  game::ParamsAre(m_pushEvent, {"CName"}) && game::ParamsAre(m_setInputFloat, {"CName", "Float"}) &&
                  game::ParamsAre(m_setInputInt, {"CName", "Int32"}) && game::ParamsAre(m_setInputBool, {"CName", "Bool"}) &&
                  game::ParamsAre(m_setInputVector, {"CName", "Vector4"}) && game::ParamsAre(m_setInputQuaternion, {"CName", "Quaternion"}) &&
                  game::ParamsAre(m_applyFeature, {"CName", "handle:animAnimFeature"});
    return m_available;
}

bool AnimPlayer::ControllerOf(RED4ext::Handle<RED4ext::IScriptable>& puppet, RED4ext::Handle<RED4ext::IScriptable>& controller)
{
    if (!Ready() || !puppet)
    {
        return false;
    }
    auto* puppetClass = puppet.instance->GetType();
    RED4ext::StackArgs_t noArgs;
    return puppetClass && puppetClass->IsA(m_scriptedPuppetClass) && game::CallMethod(puppet.instance, m_getAnimationController, &controller, noArgs) &&
           controller;
}

bool AnimPlayer::ApplyFeature(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry)
{
    auto* featureClass = RED4ext::CRTTISystem::Get()->GetClass(RED4ext::CName(entry.FeatureType));
    if (!featureClass || !featureClass->IsA(m_animFeatureClass))
    {
        return false;
    }
    auto* feature = static_cast<RED4ext::IScriptable*>(featureClass->CreateInstance(true));
    if (!feature)
    {
        return false;
    }
    RED4ext::Handle<RED4ext::IScriptable> featureHandle(feature);
    for (const auto& field : entry.Fields)
    {
        auto* property = featureClass->GetProperty(RED4ext::CName(field.Name));
        if (!property || !property->type || property->type->GetSize() != field.Bytes.size())
        {
            continue;
        }
        std::memcpy(property->GetValuePtr<uint8_t>(feature), field.Bytes.data(), field.Bytes.size());
    }
    RED4ext::CName inputName(entry.Name);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &inputName);
    args.emplace_back(nullptr, &featureHandle);
    return game::CallMethod(controller.instance, m_applyFeature, nullptr, args);
}

AnimPlayer::Result AnimPlayer::Play(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry)
{
    if (entry.Slot != PlayerGraphSlot)
    {
        return Result::OtherSlot;
    }
    if (!Ready() || !controller)
    {
        return Result::Failed;
    }
    RED4ext::CName inputName(entry.Name);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &inputName);
    float floatValue = entry.Values[0];
    int32_t intValue = entry.IntValue;
    bool boolValue = entry.IntValue != 0;
    RED4ext::Vector4 vectorValue {entry.Values[0], entry.Values[1], entry.Values[2], entry.Values[3]};
    QuaternionLayout quaternionValue {entry.Values[0], entry.Values[1], entry.Values[2], entry.Values[3]};
    RED4ext::CBaseFunction* setter = nullptr;
    switch (entry.Kind)
    {
    case net::AnimKind::Feature: return ApplyFeature(controller, entry) ? Result::Applied : Result::Failed;
    case net::AnimKind::Float:
        setter = m_setInputFloat;
        args.emplace_back(nullptr, &floatValue);
        break;
    case net::AnimKind::Int:
        setter = m_setInputInt;
        args.emplace_back(nullptr, &intValue);
        break;
    case net::AnimKind::Bool:
        setter = m_setInputBool;
        args.emplace_back(nullptr, &boolValue);
        break;
    case net::AnimKind::Vector:
        setter = m_setInputVector;
        args.emplace_back(nullptr, &vectorValue);
        break;
    case net::AnimKind::Quaternion:
        setter = m_setInputQuaternion;
        args.emplace_back(nullptr, &quaternionValue);
        break;
    case net::AnimKind::Event: setter = m_pushEvent; break;
    case net::AnimKind::Wrapper:
    case net::AnimKind::Attach:
    case net::AnimKind::Detach: return Result::NoSetter;
    }
    return game::CallMethod(controller.instance, setter, nullptr, args) ? Result::Applied : Result::Failed;
}
}
