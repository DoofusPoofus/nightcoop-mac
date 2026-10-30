#include "plugin/GameVersion.hpp"

#include <CoreFoundation/CoreFoundation.h>

namespace nightcoop_mac
{
namespace
{
struct KnownPatch
{
    const char* MacBundleVersion;
    WindowsExeVersion WindowsVersion;
};

constexpr KnownPatch KnownPatches[] = {
    {"2.3.1", {3, 0, 80, 51928}},
    {"2.5.0", {3, 0, 88, 46235}},
};
}

std::optional<WindowsExeVersion> WindowsVersionForMacGame(const std::string& macBundleVersion)
{
    for (const auto& patch : KnownPatches)
    {
        if (macBundleVersion == patch.MacBundleVersion)
        {
            return patch.WindowsVersion;
        }
    }
    return std::nullopt;
}

uint64_t PackForHello(const WindowsExeVersion& version)
{
    return (static_cast<uint64_t>(version.Major) << 48) | (static_cast<uint64_t>(version.Minor) << 32) |
           (static_cast<uint64_t>(version.Build) << 16) | version.Revision;
}

std::string ReadMacBundleVersion()
{
    CFBundleRef gameBundle = CFBundleGetMainBundle();
    if (!gameBundle)
    {
        return {};
    }
    auto versionValue = static_cast<CFStringRef>(CFBundleGetValueForInfoDictionaryKey(gameBundle, CFSTR("CFBundleShortVersionString")));
    if (!versionValue || CFGetTypeID(versionValue) != CFStringGetTypeID())
    {
        return {};
    }
    char versionText[64] = {};
    if (!CFStringGetCString(versionValue, versionText, sizeof(versionText), kCFStringEncodingUTF8))
    {
        return {};
    }
    return versionText;
}
}
