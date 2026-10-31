/*
 anim setup, the PlayerAnimSetup (34) half of nightcoops partner look: our rig and animation sets go to partners, so a
 windows partner can build our body on our rig and sets when its gender differs from theirs
 read like nightcoop does from our player: the entAnimatedComponent named root gives the rig (its rRef path) and its
 animations.gameplay sets, then evry entAnimationSetupExtensionComponent adds its animations.gameplay sets after them.
 a set is its anim set path, priority and variable names. checked evry 2 s while in a session, sent reliable to evryone
 when it changed and evry 20 s
 not done yet: putting our live sets (or a partners) onto a partner body in the build job like nightcoops body builder
*/
#pragma once

#include "net/GameMessages.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::anim_setup
{
using LogFunction = std::function<void(const std::string&)>;

bool ReadOwn(net::AnimSetup& setup, const LogFunction& log);
void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log);
}
