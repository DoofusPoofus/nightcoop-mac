/*
 anim player, plays one recorded anim graph input on a puppets entAnimationControllerComponent thru the script setters
 (feature -> ApplyFeature with the feature class and fields found by name hash, float / int / bool / vector / quaternion ->
 SetInputX, event -> PushEvent). wrapper weights and attach / detach have no script setter. only slot 0 gets played
 shared by the partner body mirror (PlayerAnim) and the npc copies (NpcAnim)
*/
#pragma once

#include "net/GameMessages.hpp"

#include <RED4ext/RED4ext.hpp>

namespace nightcoop_mac
{
class AnimPlayer
{
public:
    enum class Result
    {
        Applied,
        Failed,
        NoSetter,
        OtherSlot,
    };

    bool Ready();
    bool ControllerOf(RED4ext::Handle<RED4ext::IScriptable>& puppet, RED4ext::Handle<RED4ext::IScriptable>& controller);
    Result Play(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry);

private:
    bool ApplyFeature(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry);

    bool m_resolved = false;
    bool m_available = false;
    RED4ext::CBaseFunction* m_getAnimationController = nullptr;
    RED4ext::CBaseFunction* m_pushEvent = nullptr;
    RED4ext::CBaseFunction* m_setInputFloat = nullptr;
    RED4ext::CBaseFunction* m_setInputInt = nullptr;
    RED4ext::CBaseFunction* m_setInputBool = nullptr;
    RED4ext::CBaseFunction* m_setInputVector = nullptr;
    RED4ext::CBaseFunction* m_setInputQuaternion = nullptr;
    RED4ext::CBaseFunction* m_applyFeature = nullptr;
    RED4ext::CClass* m_scriptedPuppetClass = nullptr;
    RED4ext::CClass* m_animFeatureClass = nullptr;
};
}
