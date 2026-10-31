/*
 nightcoop probe
 a headless nightcoop client on the same RelaySession the plugin uses
 host mode opens a session prints the code and prints evry PlayerState it gets
 join mode joins by code, says its look is male, then walks PlayerStates back and forth along x (from X Y Z if given,
 150 steps of 80 ms or STEPS of them) crouching on and off, and sends PlayerAnim batches (a float, a bool, an event and a
 feature) in Game envelopes
 watch mode joins by code and prints the PlayerStates and game messages it gets for a while
 usage: nightcoop_probe host HUB [states to wait for] | join CODE HUB [X Y Z [STEPS [LOOKFILE [CLOTHESFILE]]]] | watch CODE HUB [seconds]
 CLOTHESFILE is a PlayerLook (31) as it is (the plugin writes own-clothes.bin), sent right after the look
 its look (PlayerCustomization 33) is LOOKFILE as it is (the plugin writes own-look.bin) or a male look with no parts
 PROBE_CLOCK=<game seconds> makes the host send that GameClock evry 5 s to its guests and stay up for 10 min
 PROBE_WEAPON=0x<tweakdbid> in the environment makes the joiner hold that weapon and fire a shot evry 2 s (aim + shoot flags),
 and the joiner prints the hosts weapon fields whenever they change (health, camera and velocity evry 100 states)
 HUB looks like 127.0.0.1:47900#FINGERPRINT
*/
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/RelaySession.hpp"

#include <Log.hpp>
#include <NetCrypto.hpp>

#include <enet/enet.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace
{
using namespace std::chrono_literals;

int WalkSteps = 150;
std::string LookFile;
std::string ClothesFile;
uint64_t ProbeWeapon = 0;
float WalkStartX = -1300.0f;
float WalkStartY = 1200.0f;
float WalkStartZ = 18.0f;
constexpr auto ProbeTimeout = 30s;

int RunHost(nightcoop_mac::net::RelaySession& session, int statesToWaitFor)
{
    bool printedCode = false;
    int statesSeen = 0;
    const char* clock = std::getenv("PROBE_CLOCK");
    const uint32_t clockSeconds = clock ? static_cast<uint32_t>(std::strtoul(clock, nullptr, 0)) : 0;
    auto nextClock = std::chrono::steady_clock::now();
    auto deadline = std::chrono::steady_clock::now() + (clock ? 600s : ProbeTimeout);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (clock && session.GetStatus().ConnectedPeers > 0 && std::chrono::steady_clock::now() >= nextClock)
        {
            nextClock = std::chrono::steady_clock::now() + 5s;
            session.SendGameMessage(nightcoop_mac::net::WriteGameClock(clockSeconds), nightcoop_mac::net::kEveryPeer, false);
        }
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
            if (clock && statesSeen++ % 100 != 0)
            {
                continue;
            }
            std::printf("probe: PlayerState from peer %u: pos %.2f %.2f %.2f yaw %.1f speed %.2f t %llu\n", state.PeerNumber,
                        state.PositionX, state.PositionY, state.PositionZ, state.Yaw, state.Speed,
                        static_cast<unsigned long long>(state.SessionTimeMs));
            std::fflush(stdout);
            if (!clock && ++statesSeen >= statesToWaitFor)
            {
                return 0;
            }
        }
        std::this_thread::sleep_for(20ms);
    }
    return 2;
}

uint64_t NameHash(const char* name)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (; *name; ++name)
    {
        hash = (hash ^ static_cast<uint8_t>(*name)) * 0x100000001b3ULL;
    }
    return hash;
}

std::vector<uint8_t> MakeTestAnimBatch(int step)
{
    using namespace nightcoop_mac::net;
    AnimBatch batch;
    batch.Unknown00 = static_cast<uint32_t>(step);
    batch.FullState = step == 0;
    AnimEntry speed;
    speed.Kind = AnimKind::Float;
    speed.Name = NameHash("Speed");
    speed.Values[0] = 4.0f;
    AnimEntry crouch;
    crouch.Kind = AnimKind::Bool;
    crouch.Name = NameHash("isCrouching");
    crouch.IntValue = (step / 40) % 2;
    AnimEntry jump;
    jump.Kind = AnimKind::Event;
    jump.Name = NameHash("Jump");
    AnimEntry locomotion;
    locomotion.Kind = AnimKind::Feature;
    locomotion.Name = NameHash("PlayerLocomotionStateMachine");
    locomotion.FeatureType = NameHash("AnimFeature_PlayerLocomotionStateMachine");
    batch.Entries = {speed, crouch, jump, locomotion};
    return WriteAnimBatch(batch);
}

int RunWatcher(nightcoop_mac::net::RelaySession& session, int seconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    int statesSeen = 0;
    int gameMessagesSeen = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!session.GetStatus().Running)
        {
            std::printf("probe: session ended: %s\n", session.GetStatus().LastProblem.c_str());
            return 1;
        }
        for (const auto& message : session.TakeGameMessages())
        {
            if (gameMessagesSeen++ % 10 == 0)
            {
                std::printf("probe: game message type %u from peer %u (%d so far)\n", nightcoop_mac::net::MessageTypeOf(message.Message),
                            message.FromPeer, gameMessagesSeen);
                std::fflush(stdout);
            }
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
    std::printf("probe: watched %d states and %d game messages\n", statesSeen, gameMessagesSeen);
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
    std::vector<uint8_t> look;
    if (!LookFile.empty())
    {
        std::ifstream lookFile(LookFile, std::ios::binary);
        look.assign(std::istreambuf_iterator<char>(lookFile), std::istreambuf_iterator<char>());
    }
    if (look.empty())
    {
        nightcoop_mac::net::PlayerCustomization plainLook;
        plainLook.BodyMale = true;
        plainLook.BrainMale = true;
        look = nightcoop_mac::net::WritePlayerCustomization(plainLook);
    }
    std::printf("probe: sending a look of %zu bytes\n", look.size());
    session.SendGameMessage(look);
    if (!ClothesFile.empty())
    {
        std::ifstream clothesFile(ClothesFile, std::ios::binary);
        const std::vector<uint8_t> clothes((std::istreambuf_iterator<char>(clothesFile)), std::istreambuf_iterator<char>());
        if (!clothes.empty())
        {
            std::printf("probe: sending clothes of %zu bytes\n", clothes.size());
            session.SendGameMessage(clothes);
        }
    }
    session.SendGameMessage(nightcoop_mac::net::WriteAnimWant({true, true}), 0);
    uint32_t animBatches = 0;
    uint32_t animInputs = 0;
    uint32_t fullStates = 0;
    uint64_t hostWeapon = 0;
    uint32_t hostStatesSeen = 0;
    uint32_t hostShots = 0;
    uint16_t hostWeaponFlags = 0;
    uint8_t hostWeaponState = 0;
    if (const char* weapon = std::getenv("PROBE_WEAPON"))
    {
        ProbeWeapon = std::strtoull(weapon, nullptr, 0);
        std::printf("probe: holding weapon %#llx and shooting evry 2 s\n", static_cast<unsigned long long>(ProbeWeapon));
    }
    for (int step = 0; step < WalkSteps; ++step)
    {
        nightcoop_mac::net::PlayerState state;
        const int stepInLeg = step % 300;
        state.PositionX = WalkStartX + (stepInLeg < 150 ? stepInLeg : 300 - stepInLeg) * 0.05f;
        state.PositionY = WalkStartY;
        state.PositionZ = WalkStartZ;
        state.Yaw = 90.0f;
        state.Speed = 5.0f;
        state.Flags = (step / 40) % 2 ? 1 : 0;
        if (ProbeWeapon)
        {
            state.RightWeaponRecord = ProbeWeapon;
            state.RightShotCount = static_cast<uint32_t>(step / 25);
            const bool shooting = step % 25 < 3;
            state.WeaponState = shooting ? 8 : 5;
            state.UpperBodyState = 6;
            state.Flags |= 0x04 | (shooting ? 0x40 : 0);
        }
        session.SetLocalPlayerState(state);
        if (step % 5 == 0)
        {
            session.SendGameMessage(MakeTestAnimBatch(step));
        }
        for (const auto& hostState : session.TakeRemotePlayerStates())
        {
            if (hostStatesSeen++ % 100 == 0)
            {
                std::printf("probe: peer %u health %.2f time dilation %.2f camera pitch %.1f aim offset %.1f velocity %.2f %.2f %.2f\n",
                            hostState.PeerNumber, hostState.Health, hostState.TimeDilation, hostState.CameraPitch, hostState.AimYawOffset,
                            hostState.VelocityX, hostState.VelocityY, hostState.VelocityZ);
                std::fflush(stdout);
            }
            const uint16_t weaponFlags = hostState.Flags & 0x80C4;
            if (hostState.RightWeaponRecord != hostWeapon || hostState.RightShotCount != hostShots || weaponFlags != hostWeaponFlags ||
                hostState.WeaponState != hostWeaponState)
            {
                hostWeapon = hostState.RightWeaponRecord;
                hostShots = hostState.RightShotCount;
                hostWeaponFlags = weaponFlags;
                hostWeaponState = hostState.WeaponState;
                std::printf("probe: peer %u weapon %#llx shots %u weapon state %u upper body %u flags %#x\n", hostState.PeerNumber,
                            static_cast<unsigned long long>(hostWeapon), hostShots, hostWeaponState, hostState.UpperBodyState, weaponFlags);
                std::fflush(stdout);
            }
        }
        for (const auto& message : session.TakeGameMessages())
        {
            nightcoop_mac::net::AnimWant want;
            nightcoop_mac::net::AnimBatch batch;
            uint32_t gameSeconds = 0;
            nightcoop_mac::net::AnimSetup setup;
            if (nightcoop_mac::net::ReadAnimSetup(message.Message, setup))
            {
                std::printf("probe: peer %u anim setup rig %#llx with %zu set(s), first %#llx priority %u with %zu variable(s)\n", message.FromPeer,
                            static_cast<unsigned long long>(setup.Rig), setup.Sets.size(),
                            static_cast<unsigned long long>(setup.Sets.empty() ? 0 : setup.Sets[0].AnimSet), setup.Sets.empty() ? 0 : setup.Sets[0].Priority,
                            setup.Sets.empty() ? 0 : setup.Sets[0].VariableNames.size());
            }
            else if (nightcoop_mac::net::ReadGameClock(message.Message, gameSeconds))
            {
                std::printf("probe: peer %u game clock %u s (%02u:%02u)\n", message.FromPeer, gameSeconds, gameSeconds / 3600 % 24, gameSeconds / 60 % 60);
            }
            else if (nightcoop_mac::net::ReadAnimWant(message.Message, want))
            {
                std::printf("probe: peer %u asked us for our full anim state (%d %d)\n", message.FromPeer, want.First, want.Second);
            }
            else if (nightcoop_mac::net::ReadAnimBatch(message.Message, batch))
            {
                ++animBatches;
                animInputs += static_cast<uint32_t>(batch.Entries.size());
                fullStates += batch.FullState ? 1 : 0;
            }
        }
        std::this_thread::sleep_for(80ms);
    }
    std::printf("probe: walked %d steps, got %u anim batch(es) with %u input(s) from the host, %u full state(s)\n", WalkSteps, animBatches,
                animInputs, fullStates);
    return 0;
}
}

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if ((mode != "host" || argc < 3) && ((mode != "join" && mode != "watch") || argc < 4))
    {
        std::printf("usage: %s host HUB [states] | join CODE HUB [X Y Z [STEPS]] | watch CODE HUB [seconds]\n", argv[0]);
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
        if (argc > 7)
        {
            WalkSteps = std::atoi(argv[7]);
        }
        if (argc > 8)
        {
            LookFile = argv[8];
        }
        if (argc > 9)
        {
            ClothesFile = argv[9];
        }
        result = RunGuest(session);
    }
    session.Stop();
    enet_deinitialize();
    return result;
}
