/*
 cutscene bodies nightcoops partner body hiding in scenes (the script half is NightCoopCutscenes in
 scripts/NightCoopMac/NightCoopCutscenes.reds): evry 500 ms we ask if a scene holds our player and the camera. while it does
 evry partner body gets its visual components turned off and back on once the scene lets go. test_hide in session.txt hides
 them from 20 s to 40 s into a save (testing only). a partner body standing inside our player (within 0.5 m) gets hidden too and
 comes back past 1 m (one spawn point for both)
 QuestScene 72: a guest keeps the hosts live scenes (kinds 1 and 6 up 2 down). while a scene holds our player in the hosts world
 evry live scene whose entry is within 30 m of us gets a kind 3 (we play it too, so the host hides our body) and kind 4 once
 the scene let go of us
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class RemoteBodies;
struct RemotePeerView;
namespace net
{
class RelaySession;
struct PlayerState;
}
}

namespace nightcoop_mac::cutscene_bodies
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool testHide, const std::vector<RemotePeerView>& views, const net::PlayerState* ourPlayer, RemoteBodies& bodies,
          const LogFunction& log);
bool HandlesScenes(uint8_t messageType);
void OnSceneMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, const LogFunction& log);
void TellHostScenes(bool inHostWorld, const net::PlayerState* ourPlayer, net::RelaySession& session, const LogFunction& log);
}
