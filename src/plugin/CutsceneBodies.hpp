/*
 cutscene bodies nightcoops partner body hiding in scenes (the script half is NightCoopCutscenes in
 scripts/NightCoopMac/NightCoopCutscenes.reds): evry 500 ms we ask if a scene holds our player and the camera. while it does
 evry partner body gets its visual components turned off and back on once the scene lets go. test_hide in session.txt hides
 them from 20 s to 40 s into a save (testing only)
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class RemoteBodies;
}

namespace nightcoop_mac::cutscene_bodies
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, bool testHide, const std::vector<uint32_t>& peers, RemoteBodies& bodies, const LogFunction& log);
}
