/*
 game versions
 the mac loader reads the game version from Info.plist (2.5.0) but nightcoop peers talk in
 windows exe file versions (2.5 is 3.0.88.46235). Hello sends those 4 numbers packed as u16s
 so a mac player has to show up with the windows number of the same content patch
*/
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace nightcoop_mac
{
struct WindowsExeVersion
{
    uint16_t Major;
    uint16_t Minor;
    uint16_t Build;
    uint16_t Revision;
};

std::optional<WindowsExeVersion> WindowsVersionForMacGame(const std::string& macBundleVersion);
uint64_t PackForHello(const WindowsExeVersion& version);
std::string ReadMacBundleVersion();
}
