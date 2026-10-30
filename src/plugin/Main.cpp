/*
 NightCoopMac entry points for the mac RED4ext loader
 on load it checks the game build and reads session.txt from its folder
 once the game is in its Running state the relay session starts on its own thread
 evry frame (at most 30 times a second) the main thread samples our player for PlayerState
 remote states drive RemoteBodies (partner bodies like nightcoops puppets) and get logged evry 2 s
 session-status.txt next to the plugin always says wats going on (the hosting code ends up there too)
*/
#include "net/RelaySession.hpp"
#include "plugin/GameVersion.hpp"
#include "plugin/PlayerSampler.hpp"
#include "plugin/RemoteBodies.hpp"
#include "plugin/RttiDump.hpp"
#include "plugin/SessionFile.hpp"

#include <Log.hpp>
#include <NetCrypto.hpp>

#include <RED4ext/GameStates.hpp>
#include <RED4ext/RED4ext.hpp>

#include <enet/enet.h>

#include <chrono>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>

namespace
{
constexpr uint64_t SampleEveryMs = 33;
constexpr uint64_t RemoteReportEveryMs = 2000;
constexpr uint64_t RemoteForgetAfterMs = 5000;

RED4ext::v1::PluginHandle PluginHandle = nullptr;
const RED4ext::v1::Sdk* GameSdk = nullptr;
std::filesystem::path PluginFolder;
std::optional<nightcoop_mac::SessionPlan> Plan;
uint64_t HelloGameVersion = 0;
bool EnetReady = false;

std::unique_ptr<nightcoop_mac::net::RelaySession> Session;
std::unique_ptr<nightcoop_mac::PlayerSampler> Sampler;
std::unique_ptr<nightcoop_mac::RemoteBodies> Bodies;
nightcoop_mac::net::PlayerState LastLocalState;
bool LocalStateValid = false;
struct RemotePeerSighting
{
    nightcoop_mac::net::PlayerState State;
    uint64_t ReceivedMs = 0;
};
std::map<uint32_t, RemotePeerSighting> LatestRemoteStates;
uint64_t NextSampleMs = 0;
uint64_t NextRemoteReportMs = 0;
std::string LastStatusLine;

uint64_t SteadyMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void Log(const std::string& line)
{
    GameSdk->logger->InfoF(PluginHandle, "%s", line.c_str());
}

std::filesystem::path FindPluginFolder()
{
    Dl_info info {};
    if (dladdr(reinterpret_cast<const void*>(&FindPluginFolder), &info) && info.dli_fname)
    {
        return std::filesystem::path(info.dli_fname).parent_path();
    }
    return {};
}

void WriteStatusFile(const std::string& statusLine)
{
    if (statusLine == LastStatusLine)
    {
        return;
    }
    LastStatusLine = statusLine;
    std::ofstream(PluginFolder / "session-status.txt") << statusLine << "\n";
    Log("status: " + statusLine);
}

std::string DescribeStatus(const nightcoop_mac::net::SessionStatus& status)
{
    if (!status.Running)
    {
        return "stopped" + (status.LastProblem.empty() ? std::string() : ": " + status.LastProblem);
    }
    if (!status.InSession)
    {
        return "connecting";
    }
    if (Plan->IsHost)
    {
        return "hosting, code " + status.Code + ", " + std::to_string(status.ConnectedPeers) + " guest(s)";
    }
    return "in the session as peer " + std::to_string(status.MyPeerNumber);
}

bool OnRunningEnter(RED4ext::CGameApplication*)
{
    if (nightcoop_mac::WriteRttiDumpIfRequested(PluginFolder))
    {
        Log("wrote rtti-dump.txt");
    }
    if (!Plan || !EnetReady || Session)
    {
        return true;
    }
    nightcoop_mac::net::SessionConfig config;
    config.IsHost = Plan->IsHost;
    config.JoinCode = Plan->JoinCode;
    config.Hub = Plan->Hub;
    config.Nickname = Plan->Nickname;
    config.GameVersion = HelloGameVersion;
    Session = std::make_unique<nightcoop_mac::net::RelaySession>(Log);
    Sampler = std::make_unique<nightcoop_mac::PlayerSampler>(Log);
    Bodies = std::make_unique<nightcoop_mac::RemoteBodies>(Log, Plan->Bodies);
    Session->Start(config);
    return true;
}

void TakeRemoteStates(uint64_t nowMs)
{
    for (const auto& state : Session->TakeRemotePlayerStates())
    {
        LatestRemoteStates[state.PeerNumber] = {state, nowMs};
    }
    std::erase_if(LatestRemoteStates, [nowMs](const auto& entry) { return nowMs - entry.second.ReceivedMs > RemoteForgetAfterMs; });
}

std::vector<nightcoop_mac::RemotePeerView> RemotePeerViews()
{
    std::vector<nightcoop_mac::RemotePeerView> views;
    for (const auto& [peerNumber, sighting] : LatestRemoteStates)
    {
        views.push_back({peerNumber, sighting.State});
    }
    return views;
}

void ReportRemoteStates(uint64_t nowMs)
{
    if (nowMs < NextRemoteReportMs || LatestRemoteStates.empty())
    {
        return;
    }
    NextRemoteReportMs = nowMs + RemoteReportEveryMs;
    for (const auto& [peerNumber, sighting] : LatestRemoteStates)
    {
        const auto& state = sighting.State;
        GameSdk->logger->InfoF(PluginHandle, "remote peer %u at %.2f %.2f %.2f yaw %.1f speed %.2f", peerNumber, state.PositionX,
                               state.PositionY, state.PositionZ, state.Yaw, state.Speed);
    }
}

bool OnRunningUpdate(RED4ext::CGameApplication*)
{
    if (!Session)
    {
        return false;
    }
    const uint64_t nowMs = SteadyMs();
    WriteStatusFile(DescribeStatus(Session->GetStatus()));
    TakeRemoteStates(nowMs);
    if (Plan->SamplePlayer && nowMs >= NextSampleMs)
    {
        NextSampleMs = nowMs + SampleEveryMs;
        nightcoop_mac::net::PlayerState localState;
        LocalStateValid = Sampler->Sample(localState, nowMs);
        if (LocalStateValid)
        {
            LastLocalState = localState;
            Session->SetLocalPlayerState(localState);
        }
        Bodies->Tick(nowMs, RemotePeerViews(), LocalStateValid ? &LastLocalState : nullptr);
    }
    ReportRemoteStates(nowMs);
    return false;
}

bool OnRunningExit(RED4ext::CGameApplication*)
{
    if (Bodies)
    {
        Bodies->ForgetAll("forgotten, the game is closing");
    }
    if (Session)
    {
        Session->Stop();
        WriteStatusFile(DescribeStatus(Session->GetStatus()));
    }
    return true;
}

void SetUpSession()
{
    const std::string macVersion = nightcoop_mac::ReadMacBundleVersion();
    const auto windowsVersion = nightcoop_mac::WindowsVersionForMacGame(macVersion);
    if (!windowsVersion)
    {
        Log("NightCoopMac: game " + macVersion + " is not a known patch, staying passive");
        return;
    }
    HelloGameVersion = nightcoop_mac::PackForHello(*windowsVersion);
    GameSdk->logger->InfoF(PluginHandle, "NightCoopMac loading, protocol %u, mac game %s talks to peers as exe %u.%u.%u.%u",
                           nightcoop_mac::net::kProtocolVersion, macVersion.c_str(), windowsVersion->Major,
                           windowsVersion->Minor, windowsVersion->Build, windowsVersion->Revision);

    PluginFolder = FindPluginFolder();
    static RED4ext::v1::GameState runningState {OnRunningEnter, OnRunningUpdate, OnRunningExit};
    if (!GameSdk->gameStates->Add(PluginHandle, RED4ext::EGameStateType::Running, &runningState))
    {
        Log("could not hook the Running game state");
        return;
    }
    std::string problem;
    Plan = nightcoop_mac::ReadSessionPlan((PluginFolder / "session.txt").string(), problem);
    if (!Plan)
    {
        Log(problem.empty() ? "no session.txt, idling" : problem);
        return;
    }
    Log(Plan->IsHost ? "session.txt: host on " + Plan->Hub : "session.txt: join " + Plan->JoinCode + " on " + Plan->Hub);

    nc::log::SetSink([](nc::log::Level, const char* line) { Log(std::string("net: ") + line); });
    nc::netcrypto::SetDataDirectory((PluginFolder / "data").string());
    EnetReady = enet_initialize() == 0;
    if (!EnetReady)
    {
        Log("enet did not start, no session");
        return;
    }

}
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    if (aReason == RED4ext::v1::EMainReason::Load)
    {
        PluginHandle = aHandle;
        GameSdk = aSdk;
        SetUpSession();
    }
    else if (aReason == RED4ext::v1::EMainReason::Unload)
    {
        if (Session)
        {
            Session->Stop();
        }
        Log("NightCoopMac unloaded");
    }
    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"NightCoopMac";
    aInfo->author = L"DoofusPoofus";
    aInfo->version = RED4EXT_V1_SEMVER(0, 0, 2);
    aInfo->runtime = RED4EXT_V1_FILEVER(2, 5, 0, 0);
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
