/*
 NightCoopMac entry points for the mac RED4ext loader
 for now it only checks the game build and says which windows version it would show to peers
 the session net code and the game systems come next
*/
#include "plugin/GameVersion.hpp"

#include <RED4ext/RED4ext.hpp>

namespace
{
constexpr uint32_t NightCoopProtocolVersion = 143;

RED4ext::v1::PluginHandle PluginHandle = nullptr;
const RED4ext::v1::Sdk* GameSdk = nullptr;

void LogStartup()
{
    const std::string macVersion = nightcoop_mac::ReadMacBundleVersion();
    const auto windowsVersion = nightcoop_mac::WindowsVersionForMacGame(macVersion);
    if (!windowsVersion)
    {
        GameSdk->logger->InfoF(PluginHandle, "NightCoopMac: game %s is not a known patch, staying passive", macVersion.c_str());
        return;
    }
    GameSdk->logger->InfoF(PluginHandle,
                           "NightCoopMac loading, protocol %u, mac game %s talks to peers as exe %u.%u.%u.%u (hello 0x%016llx)",
                           NightCoopProtocolVersion, macVersion.c_str(), windowsVersion->Major, windowsVersion->Minor,
                           windowsVersion->Build, windowsVersion->Revision,
                           static_cast<unsigned long long>(nightcoop_mac::PackForHello(*windowsVersion)));
}
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    if (aReason == RED4ext::v1::EMainReason::Load)
    {
        PluginHandle = aHandle;
        GameSdk = aSdk;
        LogStartup();
    }
    else if (aReason == RED4ext::v1::EMainReason::Unload)
    {
        GameSdk->logger->Info(PluginHandle, "NightCoopMac unloaded");
    }
    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"NightCoopMac";
    aInfo->author = L"DoofusPoofus";
    aInfo->version = RED4EXT_V1_SEMVER(0, 0, 1);
    aInfo->runtime = RED4EXT_V1_FILEVER(2, 5, 0, 0);
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
