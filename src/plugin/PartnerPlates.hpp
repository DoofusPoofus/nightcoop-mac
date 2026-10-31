/*
 partner plates, the plugin half of nightcoops world markers (the script half is NightCoopPartnerPlates in
 scripts/NightCoopMac/NightCoopPartnerMarkers.reds). evry frame in a save each partner spot 2.05 m up goes thru the active
 camera: in front of it (deeper than 0.1 m) and within 2000 m it gets projected with gameCameraSystem.ProjectPoint, the
 axis signs checked once with points 10 m ahead and 1 m right / up like nightcoops calibration, and placed in the 3840 x 2160
 hud space. scale 1.0 up to 15 m down to 0.72 at 300 m, closer than 6 m it fades to 35 %, distance text empty under 8 m,
 metres to 1000 m (5 m steps past 100), then km. a partner off screen or behind us sticks to the screen border in their
 direction (3.5 % / 5 % margins, scale at most 0.85, opacity * 0.85) like nightcoops plates
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::partner_plates
{
using LogFunction = std::function<void(const std::string&)>;

struct PartnerSpot
{
    uint32_t PeerNumber = 0;
    float X = 0;
    float Y = 0;
    float Z = 0;
    std::string Name;
};

void Tick(uint64_t nowMs, bool inSave, const std::vector<PartnerSpot>& partners, const LogFunction& log);
}
