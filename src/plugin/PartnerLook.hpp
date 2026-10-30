/*
 partner look, the mac take on nightcoops PartnerLook
 a V bodys character customization parts get their look from the games character customization system: the look part code
 (game+0x4bacda8 on mac 2.5.0) works out the gender from the bodys record and calls the systems GetState(isMale)
 (vtable slot 0x1f8, game+0x4bc7228). that hands out the one state the system has (+0x78, our V) when its body gender
 matches and nothing otherwise. so a partner body wears our look, or none when its the other gender.
 like nightcoop this hooks GetState(isMale): while the look part code runs for a partner body (a thread local scope set by a
 second hook on the look part function) it returns that partners own state. both functions are found by their mac offsets and
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
}
