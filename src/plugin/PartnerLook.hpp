/*
 partner look, the mac take on nightcoops PartnerLook
 a V bodys character customization parts get their look from the games character customization system: the look part code
 (game+0x4bacda8 on mac 2.5.0) works out the gender from the bodys record and calls the systems GetState(isMale)
 (vtable slot 0x1f8, game+0x4bc7228). that hands out the one state the system has (+0x78, our V) when its body gender
 matches and nothing otherwise. so a partner body wears our look, or none when its the other gender.
 like nightcoop this hooks GetState(isMale): when it is asked for a partner body it returns that partners own state.
 which body asks: in a save the parts ask from about 20 different callers, so a small asm entry hands the callers callee saved
 registers (x19..x28) to the detour, and the one that is a partner body (its entity id at +0x48) or a component of one
 (component +0x50 -> entity) names the body. every read is a safe vm read. the look part function (only used in the menu)
 also sets a thread local scope. both functions are found by their mac offsets and
 only hooked when their first 24 code bytes are the ones seen in 2.5.0, so another build leaves them alone
 GetState() (slot 0x1f0, game+0x4bc6098) is hooked the same way for parts that ask without a gender. outside any scope a
 GetState(isMale) of the other gender than our own V goes to the only partner body of that gender, like nightcoops fallback
 (our own V never asks for the other gender, so that call is a partners TPP view, a part job and so on)
 the partner states come from SetPartnerState (main thread), keyed by the bodys entity id
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace nightcoop_mac::partner_look
{
using LogFunction = std::function<void(const std::string&)>;

bool Install(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, LogFunction log);
bool IsInstalled();
void SetPartnerState(uint64_t entityHash, const RED4ext::Handle<RED4ext::IScriptable>& state, bool isMale);
void SetOwnGender(bool isMale);
void ForgetPartnerState(uint64_t entityHash);
void ForgetAllPartnerStates();
uint32_t TakeAnsweredCount();
std::string TakeTrafficReport();
}
