/*
 look states, the game side of PlayerCustomization (33) like nightcoops partner look ReadOwn / state builder
 our look: the character customization system holds one gameuiCharacterCustomizationState (our V, only in a loaded save).
 its fields have no rtti props, so they get read at their raw offsets (the same on windows and mac, see the mac sdk notes):
   +0x41 built flag, +0x44 body gender male, +0x45 brain gender male, +0xC0 data version,
   +0x70 / +0x80 / +0x90 head / body / arms DynArray<gameuiCustomizationGroup>, +0xA0 DynArray<gameuiPerspectiveInfo>,
   +0xB0 redTagList. the arrays are walked and filled thru their rtti array types (taken from gameuiCharacterCustomizationPreset,
   which has the same lists as rtti props) and the element fields thru the element classes rtti props
 a partners look: a fresh gameuiCharacterCustomizationState gets the same fields from their message, then +0x41 = 1.
 PartnerLook hands that state to their body. main thread only
*/
#pragma once

#include "net/LookMessages.hpp"

#include <RED4ext/RED4ext.hpp>

#include <functional>
#include <string>

namespace nightcoop_mac::look_states
{
using LogFunction = std::function<void(const std::string&)>;

bool ReadOwnLook(net::PlayerCustomization& look, const LogFunction& log);
RED4ext::Handle<RED4ext::IScriptable> BuildState(const net::PlayerCustomization& look, const LogFunction& log);
}
