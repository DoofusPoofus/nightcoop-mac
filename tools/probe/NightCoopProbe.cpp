/*
 nightcoop probe
 a headless nightcoop client on the same RelaySession the plugin uses
 host mode opens a session prints the code and prints evry PlayerState it gets
 join mode joins by code, says its look is male, then walks a few PlayerStates along x (from X Y Z if given)
 crouching on and off
 watch mode joins by code and prints evry PlayerState it gets for a while
 usage: nightcoop_probe host HUB [states to wait for] | join CODE HUB [X Y Z] | watch CODE HUB [seconds]
 HUB looks like 127.0.0.1:47900#FINGERPRINT
*/
#include "net/RelaySession.hpp"

#include <Log.hpp>
#include <NetCrypto.hpp>

#include <enet/enet.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace
{
using namespace std::chrono_literals;

constexpr int WalkSteps = 150;
float WalkStartX = -1300.0f;
float WalkStartY = 1200.0f;
float WalkStartZ = 18.0f;
constexpr auto ProbeTimeout = 30s;

int RunHost(nightcoop_mac::net::RelaySession& session, int statesToWaitFor)
{
    bool printedCode = false;
    int statesSeen = 0;
    const auto deadline = std::chrono::steady_clock::now() + ProbeTimeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto status = session.GetStatus();
        if (!status.Running)
        {
            std::printf("probe: session ended: %s\n", status.LastProblem.c_str());
            return 1;
        }
        if (!printedCode && !status.Code.empty())
        {
            std::printf("probe: session created, code %s\n", status.Code.c_str());
            std::fflush(stdout);
            printedCode = true;
        }
        for (const auto& state : session.TakeRemotePlayerStates())
        {
            std::printf("probe: PlayerState from peer %u: pos %.2f %.2f %.2f yaw %.1f speed %.2f t %llu\n", state.PeerNumber,
                        state.PositionX, state.PositionY, state.PositionZ, state.Yaw, state.Speed,
                        static_cast<unsigned long long>(state.SessionTimeMs));
            std::fflush(stdout);
            if (++statesSeen >= statesToWaitFor)
            {
                return 0;
            }
        }
        std::this_thread::sleep_for(20ms);
    }
    return 2;
}

int RunWatcher(nightcoop_mac::net::RelaySession& session, int seconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    int statesSeen = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!session.GetStatus().Running)
        {
            std::printf("probe: session ended: %s\n", session.GetStatus().LastProblem.c_str());
            return 1;
        }
        for (const auto& state : session.TakeRemotePlayerStates())
        {
            if (statesSeen++ % 15 == 0)
            {
                std::printf("probe: peer %u at %.2f %.2f %.2f yaw %.1f speed %.2f (%d states so far)\n", state.PeerNumber,
                            state.PositionX, state.PositionY, state.PositionZ, state.Yaw, state.Speed, statesSeen);
                std::fflush(stdout);
            }
        }
        std::this_thread::sleep_for(20ms);
    }
    std::printf("probe: watched %d states\n", statesSeen);
    return statesSeen > 0 ? 0 : 2;
}

int RunGuest(nightcoop_mac::net::RelaySession& session)
{
    const auto deadline = std::chrono::steady_clock::now() + ProbeTimeout;
    while (!session.GetStatus().InSession)
    {
        if (!session.GetStatus().Running || std::chrono::steady_clock::now() > deadline)
        {
            std::printf("probe: never got in: %s\n", session.GetStatus().LastProblem.c_str());
            return 1;
        }
        std::this_thread::sleep_for(20ms);
    }
    std::printf("probe: in the session as peer %u\n", session.GetStatus().MyPeerNumber);
    session.SendToSession({0x50, 0x4F, 0x43, 0x4E, 33, 1, 0, 0, 0, 0, 1, 0});
    for (int step = 0; step < WalkSteps; ++step)
    {
        nightcoop_mac::net::PlayerState state;
        state.PositionX = WalkStartX + step * 0.05f;
        state.PositionY = WalkStartY;
        state.PositionZ = WalkStartZ;
        state.Yaw = 90.0f;
        state.Speed = 5.0f;
        state.Flags = (step / 40) % 2 ? 1 : 0;
        session.SetLocalPlayerState(state);
        std::this_thread::sleep_for(80ms);
    }
    std::printf("probe: walked %d steps\n", WalkSteps);
    return 0;
}
}

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if ((mode != "host" || argc < 3) && ((mode != "join" && mode != "watch") || argc < 4))
    {
        std::printf("usage: %s host HUB [states] | join CODE HUB | watch CODE HUB [seconds]\n", argv[0]);
        return 1;
    }
    nc::log::SetSink([](nc::log::Level, const char* line) { std::fprintf(stderr, "[nc] %s\n", line); });
    nc::netcrypto::SetDataDirectory("probe-data");
    if (enet_initialize() != 0)
    {
        return 1;
    }

    nightcoop_mac::net::RelaySession session([](const std::string& line) { std::fprintf(stderr, "[probe] %s\n", line.c_str()); });
    nightcoop_mac::net::SessionConfig config;
    config.IsHost = mode == "host";
    config.Hub = config.IsHost ? argv[2] : argv[3];
    config.JoinCode = config.IsHost ? "" : argv[2];
    config.Nickname = "macprobe";
    config.GameVersion = 0x000300000058B49BULL;
    session.Start(config);

    int result = 0;
    if (config.IsHost)
    {
        result = RunHost(session, argc > 3 ? std::atoi(argv[3]) : 5);
    }
    else if (mode == "watch")
    {
        result = RunWatcher(session, argc > 4 ? std::atoi(argv[4]) : 60);
    }
    else
    {
        if (argc > 6)
        {
            WalkStartX = static_cast<float>(std::atof(argv[4]));
            WalkStartY = static_cast<float>(std::atof(argv[5]));
            WalkStartZ = static_cast<float>(std::atof(argv[6]));
        }
        result = RunGuest(session);
    }
    session.Stop();
    enet_deinitialize();
    return result;
}
