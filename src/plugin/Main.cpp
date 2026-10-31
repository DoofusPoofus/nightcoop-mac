/*
 NightCoopMac entry points for the mac RED4ext loader
 on load it checks the game build and reads session.txt from its folder
 once the game is in its Running state the relay session starts on its own thread
 evry frame (at most 30 times a second) the main thread samples our player for PlayerState
 remote states drive RemoteBodies (partner bodies like nightcoops puppets) and get logged evry 2 s
 with bodies on the entity build job gets hooked at load (PartnerBodyBuilder) so partner bodies come out built like nightcoops
 SessionFlow says when bodies may exist at all: only in a loaded save, never in the main menu or a session thats ending
 game messages from partners go to AnimMirror, which plays their anim streams on their bodies
 our look goes to partners as PlayerCustomization (33) when it changes (checked evry 2 s, sent again evry 20 s), a partners
 look becomes a state of its own (LookStates) that their body wears (PartnerLook). our clothes go out as PlayerLook (31) the same
 way (checked evry 250 ms, sent again evry 10 s) and a partners clothes get put on their body (Wardrobe)
 AnimTap records the inputs the game writes into our players anim graph and sends them as PlayerAnim batches
 session-status.txt next to the plugin always says wats going on (the hosting code ends up there too)
 a crash in game code leaves crash-notes.txt there, naming the game objects the crashing thread had in its registers
*/
#include "net/LookMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/AnimMirror.hpp"
#include "plugin/AnimSetup.hpp"
#include "plugin/AnimTap.hpp"
#include "plugin/CrashNotes.hpp"
#include "plugin/Downed.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/GameClock.hpp"
#include "plugin/GameVersion.hpp"
#include "plugin/KeyWatch.hpp"
#include "plugin/LiftSync.hpp"
#include "plugin/LookStates.hpp"
#include "plugin/PartnerBodyBuilder.hpp"
#include "plugin/PartnerLook.hpp"
#include "plugin/PartnerPins.hpp"
#include "plugin/PartnerPlates.hpp"
#include "plugin/PartnerRoster.hpp"
#include "plugin/PlayerSampler.hpp"
#include "plugin/RemoteBodies.hpp"
#include "plugin/RttiDump.hpp"
#include "plugin/SessionFlow.hpp"
#include "plugin/TestKeys.hpp"
#include "plugin/Vehicles.hpp"
#include "plugin/Wardrobe.hpp"
#include "plugin/Weapons.hpp"
#include "plugin/SessionFile.hpp"

#include <Log.hpp>
#include <ProtocolCore.hpp>
#include <NetCrypto.hpp>

#include <RED4ext/GameStates.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

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
std::unique_ptr<nightcoop_mac::Vehicles> Cars;
std::unique_ptr<nightcoop_mac::AnimMirror> Mirror;
std::unique_ptr<nightcoop_mac::SessionFlow> Flow;
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
uint64_t NextCrashNotesCheckMs = 0;
uint64_t LateDumpAtMs = 0;
constexpr uint64_t OwnLookEveryMs = 2000;
constexpr uint64_t OwnLookResendMs = 20000;
uint64_t NextOwnLookMs = 0;
uint64_t NextAnsweredReportMs = 0;
uint64_t NextTrafficReportMs = 0;
uint64_t SaveSeenAtMs = 0;
uint64_t NextTestMoveMs = 0;
uint32_t TestMovesDone = 0;
uint64_t NextTestWeaponStepMs = 0;
uint32_t TestWeaponSteps = 0;
uint32_t TestWeaponShots = 0;
uint64_t NextPinsReportMs = 0;
bool TestHelpHolding = false;
uint32_t PinsReported = 0;
int32_t ProfilesReported = -1;
uint32_t AnsweredSinceReport = 0;
uint64_t OwnLookSentMs = 0;
uint64_t OwnLookFingerprint = 0;
struct PeerLook
{
    uint64_t Fingerprint = 0;
    RED4ext::Handle<RED4ext::IScriptable> State;
};
std::map<uint32_t, PeerLook> PeerLooks;
constexpr uint64_t OwnClothesEveryMs = 250;
constexpr uint64_t OwnClothesResendMs = 10000;
uint64_t NextOwnClothesMs = 0;
uint64_t OwnClothesSentMs = 0;
uint64_t OwnClothesFingerprint = 0;
struct PeerClothing
{
    uint64_t Fingerprint = 0;
    std::shared_ptr<const nightcoop_mac::net::PlayerClothes> Clothes;
};
std::map<uint32_t, PeerClothing> PeerClothes;
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
    nightcoop_mac::InstallCrashNotes(PluginFolder);
    if (nightcoop_mac::WriteRttiDumpIfRequested(PluginFolder))
    {
        Log("wrote rtti-dump.txt");
        LateDumpAtMs = SteadyMs() + 30000;
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
    Cars = std::make_unique<nightcoop_mac::Vehicles>(Log);
    Mirror = std::make_unique<nightcoop_mac::AnimMirror>(Log);
    Flow = std::make_unique<nightcoop_mac::SessionFlow>(Log);
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

void ShareOwnLook(uint64_t nowMs)
{
    if (nowMs < NextOwnLookMs || !Session->GetStatus().InSession)
    {
        return;
    }
    NextOwnLookMs = nowMs + OwnLookEveryMs;
    nightcoop_mac::net::PlayerCustomization look;
    if (!nightcoop_mac::look_states::ReadOwnLook(look, Log))
    {
        return;
    }
    nightcoop_mac::partner_look::SetOwnGender(look.BodyMale);
    const auto message = nightcoop_mac::net::WritePlayerCustomization(look);
    const uint64_t fingerprint = nightcoop_mac::net::LookFingerprint(message);
    if (fingerprint == OwnLookFingerprint && nowMs - OwnLookSentMs < OwnLookResendMs)
    {
        return;
    }
    if (fingerprint != OwnLookFingerprint)
    {
        Log("looks: our look goes to partners (" + std::to_string(look.Head.size()) + " head, " + std::to_string(look.Body.size()) + " body, " +
            std::to_string(look.Arms.size()) + " arms group(s), " + (look.BodyMale ? "male" : "female") + " body, " + std::to_string(message.size()) +
            " bytes)");
        std::ofstream(PluginFolder / "own-look.bin", std::ios::binary).write(reinterpret_cast<const char*>(message.data()), message.size());
    }
    OwnLookFingerprint = fingerprint;
    OwnLookSentMs = nowMs;
    Session->SendGameMessage(message);
}

void RunWeaponTest(uint64_t nowMs)
{
    if (!NextTestWeaponStepMs)
    {
        NextTestWeaponStepMs = nowMs + 8000;
    }
    if (TestWeaponShots >= 10 || TestWeaponSteps > 40 || nowMs < NextTestWeaponStepMs)
    {
        return;
    }
    NextTestWeaponStepMs = nowMs + 2000;
    static auto* getPlayer = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    if (!getPlayer || !nightcoop_mac::game::CallStatic(getPlayer, &player, args) || !player)
    {
        return;
    }
    if (TestWeaponSteps++ == 0)
    {
        nightcoop_mac::weapons::DrawTestWeapon(player, "Items.Preset_Lexington_Default", Log);
        return;
    }
    if (nightcoop_mac::weapons::FakeShot(player))
    {
        Log("test weapon: fake shot " + std::to_string(++TestWeaponShots) + " of 10 took a round out (" + nightcoop_mac::weapons::DescribeOwn(player) +
            ")");
    }
    else if (TestWeaponSteps % 5 == 2)
    {
        Log("test weapon: no shot yet (" + nightcoop_mac::weapons::DescribeOwn(player) + ")");
    }
}

void ReportPins(uint64_t nowMs)
{
    if (nowMs < NextPinsReportMs)
    {
        return;
    }
    NextPinsReportMs = nowMs + 10000;
    const uint32_t pins = nightcoop_mac::partner_pins::PinCount();
    const int32_t profiles = Flow->PartnerPinProfiles();
    if (pins != PinsReported || profiles != ProfilesReported)
    {
        PinsReported = pins;
        ProfilesReported = profiles;
        Log("pins: " + std::to_string(pins) + " partner pin(s) up, the minimap made " + std::to_string(profiles) + " look(s) for them so far");
    }
}

void ShareOwnClothes(uint64_t nowMs)
{
    if (nowMs < NextOwnClothesMs || !Session->GetStatus().InSession)
    {
        return;
    }
    NextOwnClothesMs = nowMs + OwnClothesEveryMs;
    nightcoop_mac::net::PlayerClothes clothes;
    if (!nightcoop_mac::wardrobe::ReadOwnClothes(clothes, Log))
    {
        return;
    }
    const auto message = nightcoop_mac::net::WritePlayerClothes(clothes);
    const uint64_t fingerprint = nightcoop_mac::net::LookFingerprint(message);
    if (fingerprint == OwnClothesFingerprint && nowMs - OwnClothesSentMs < OwnClothesResendMs)
    {
        return;
    }
    if (fingerprint != OwnClothesFingerprint)
    {
        Log("wardrobe: our clothes go to partners (" + std::to_string(clothes.Items.size()) + " piece(s), " + (clothes.Female ? "female" : "male") + ")");
        std::ofstream(PluginFolder / "own-clothes.bin", std::ios::binary).write(reinterpret_cast<const char*>(message.data()), message.size());
    }
    OwnClothesFingerprint = fingerprint;
    OwnClothesSentMs = nowMs;
    Session->SendGameMessage(message);
}

void TakePeerClothes(uint32_t peerNumber, const std::vector<uint8_t>& message)
{
    const uint64_t fingerprint = nightcoop_mac::net::LookFingerprint(message);
    auto& known = PeerClothes[peerNumber];
    if (known.Fingerprint == fingerprint)
    {
        return;
    }
    auto clothes = std::make_shared<nightcoop_mac::net::PlayerClothes>();
    if (!nightcoop_mac::net::ReadPlayerClothes(message, *clothes))
    {
        Log("wardrobe: peer " + std::to_string(peerNumber) + " sent clothes that do not read");
        known.Fingerprint = fingerprint;
        return;
    }
    known.Fingerprint = fingerprint;
    known.Clothes = clothes;
    Log("wardrobe: peer " + std::to_string(peerNumber) + " clothes came in (" + std::to_string(clothes->Items.size()) + " piece(s), " +
        (clothes->Female ? "female" : "male") + ")");
}

void TakePeerLook(uint32_t peerNumber, const std::vector<uint8_t>& message)
{
    const uint64_t fingerprint = nightcoop_mac::net::LookFingerprint(message);
    auto& known = PeerLooks[peerNumber];
    if (known.Fingerprint == fingerprint)
    {
        return;
    }
    nightcoop_mac::net::PlayerCustomization look;
    if (!nightcoop_mac::net::ReadPlayerCustomization(message, look))
    {
        Log("looks: peer " + std::to_string(peerNumber) + " sent a look that does not read, their body keeps our look");
        known.Fingerprint = fingerprint;
        return;
    }
    known.Fingerprint = fingerprint;
    known.State = nightcoop_mac::look_states::BuildState(look, Log);
    Log("looks: peer " + std::to_string(peerNumber) + " look came in (" + std::to_string(look.Head.size()) + " head, " + std::to_string(look.Body.size()) +
        " body, " + std::to_string(look.Arms.size()) + " arms group(s), " + (look.BodyMale ? "male" : "female") + "): " +
        (known.State ? "their body wears it" : "could not be built, their body keeps our look"));
    Bodies->UseLook(peerNumber, known.State, look.BodyMale);
}

std::vector<nightcoop_mac::RemotePeerView> RemotePeerViews()
{
    const auto genders = Session->PeerGenders();
    std::vector<nightcoop_mac::RemotePeerView> views;
    for (const auto& [peerNumber, sighting] : LatestRemoteStates)
    {
        const auto gender = genders.find(peerNumber);
        const auto look = PeerLooks.find(peerNumber);
        const auto clothes = PeerClothes.find(peerNumber);
        views.push_back({peerNumber, sighting.State, gender != genders.end(), gender != genders.end() && gender->second,
                         look != PeerLooks.end() ? look->second.State : RED4ext::Handle<RED4ext::IScriptable>(),
                         clothes != PeerClothes.end() ? clothes->second.Clothes : nullptr,
                         clothes != PeerClothes.end() ? clothes->second.Fingerprint : 0});
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
    if (LateDumpAtMs && SteadyMs() >= LateDumpAtMs)
    {
        LateDumpAtMs = 0;
        nightcoop_mac::WriteRttiDumpIfRequested(PluginFolder);
        Log("wrote rtti-dump.txt again with the game systems up");
    }
    if (!Session)
    {
        return false;
    }
    const uint64_t nowMs = SteadyMs();
    if (nowMs >= NextCrashNotesCheckMs)
    {
        NextCrashNotesCheckMs = nowMs + 5000;
        nightcoop_mac::KeepCrashNotesOnTop();
    }
    WriteStatusFile(DescribeStatus(Session->GetStatus()));
    TakeRemoteStates(nowMs);
    const bool inSave = LocalStateValid && Flow->BodiesAllowed(nowMs);
    for (const auto& message : Session->TakeGameMessages())
    {
        if (nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::PlayerCustomization))
        {
            TakePeerLook(message.FromPeer, message.Message);
        }
        else if (nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::PlayerLook))
        {
            TakePeerClothes(message.FromPeer, message.Message);
        }
        else if (nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::PlayerAnimWant))
        {
            nightcoop_mac::anim_tap::SendFullState(message.FromPeer, *Session);
        }
        else if (nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::PlayerPresence) ||
                 nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::SessionRoster))
        {
            nightcoop_mac::partner_roster::OnMessage(message.FromPeer, message.Message, *Session, Log);
        }
        else if (nightcoop_mac::net::MessageTypeOf(message.Message) >= static_cast<uint8_t>(nc::proto::MsgType::VehicleSpawn) &&
                 nightcoop_mac::net::MessageTypeOf(message.Message) <= static_cast<uint8_t>(nc::proto::MsgType::VehicleSeat))
        {
            Cars->OnMessage(message.FromPeer, message.Message, nowMs);
        }
        else if (nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::PlayerRevive) ||
                 nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::DownedNotice))
        {
            nightcoop_mac::downed::OnMessage(message.FromPeer, message.Message, LocalStateValid ? &LastLocalState : nullptr, *Session, Log);
        }
        else if (nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::LiftSync))
        {
            nightcoop_mac::lift_sync::OnMessage(message.FromPeer, message.Message, *Session, Log);
        }
        else if (nightcoop_mac::net::MessageTypeOf(message.Message) == static_cast<uint8_t>(nc::proto::MsgType::GameClock))
        {
            nightcoop_mac::game_clock::OnMessage(message.FromPeer, message.Message, inSave, Session->GetStatus().MyPeerNumber == 0, Log);
        }
        Mirror->OnGameMessage(message.FromPeer, message.Message, nowMs);
    }
    nightcoop_mac::partner_roster::Tick(nowMs, inSave, Plan->Nickname, *Session, Log);
    Cars->Tick(nowMs, inSave, *Session, *Bodies);
    nightcoop_mac::lift_sync::Tick(nowMs, inSave, !LatestRemoteStates.empty(), *Session, Log);
    if (inSave)
    {
        std::vector<nightcoop_mac::partner_plates::PartnerSpot> plateSpots;
        for (const auto& view : RemotePeerViews())
        {
            plateSpots.push_back({view.PeerNumber, view.State.PositionX, view.State.PositionY, view.State.PositionZ,
                                  nightcoop_mac::partner_roster::NameOf(view.PeerNumber, *Session)});
        }
        nightcoop_mac::partner_plates::Tick(nowMs, inSave, plateSpots, Log);
    }
    if (Plan->TestVehicle && inSave)
    {
        Cars->RunTestCar(nowMs);
    }
    if (Plan->SamplePlayer && nowMs >= NextSampleMs)
    {
        NextSampleMs = nowMs + SampleEveryMs;
        nightcoop_mac::net::PlayerState localState;
        const bool hadLocalState = LocalStateValid;
        LocalStateValid = Sampler->Sample(localState, nowMs);
        if (LocalStateValid != hadLocalState)
        {
            Log(LocalStateValid ? "our player is in a world" : "our player is gone, the world is going away");
        }
        if (LocalStateValid)
        {
            if (nightcoop_mac::downed::WeAreDown())
            {
                localState.Flags |= nightcoop_mac::downed::DownFlag;
            }
            LastLocalState = localState;
            Session->SetLocalPlayerState(localState);
        }
        const bool bodiesAllowed = Flow->BodiesAllowed(nowMs);
        const auto peerViews = RemotePeerViews();
        std::vector<nightcoop_mac::downed::Partner> downedPartners;
        for (const auto& view : peerViews)
        {
            downedPartners.push_back({view.PeerNumber, view.State, nightcoop_mac::partner_roster::NameOf(view.PeerNumber, *Session)});
        }
        nightcoop_mac::downed::Tick(nowMs, bodiesAllowed && LocalStateValid, LocalStateValid ? &LastLocalState : nullptr, downedPartners, *Session, Log);
        if (Plan->TestHelp && bodiesAllowed)
        {
            const uint64_t phase = nowMs % 8000;
            const bool holdF = phase < 2000;
            if (holdF != TestHelpHolding)
            {
                TestHelpHolding = holdF;
                nightcoop_mac::PostKey(nightcoop_mac::KeyF, "f", holdF);
            }
        }
        if (Plan->TestDowned && bodiesAllowed)
        {
            nightcoop_mac::downed::RunTest(nowMs, !peerViews.empty(), Log);
        }
        if (Plan->TestMoves && bodiesAllowed)
        {
            if (!SaveSeenAtMs)
            {
                SaveSeenAtMs = nowMs;
                NextTestMoveMs = nowMs + 8000;
            }
            if (TestMovesDone < 8 && nowMs >= NextTestMoveMs)
            {
                NextTestMoveMs = nowMs + 3000;
                ++TestMovesDone;
                nightcoop_mac::PressCrouchKey();
                Log("test moves: pressed c (" + std::to_string(TestMovesDone) + " of 8)");
            }
        }
        nightcoop_mac::game_clock::Tick(nowMs, bodiesAllowed && LocalStateValid, *Session, Log);
        nightcoop_mac::anim_setup::Tick(nowMs, bodiesAllowed && LocalStateValid, *Session, Log);
        if (Plan->TestWeapon && bodiesAllowed)
        {
            RunWeaponTest(nowMs);
        }
        if (Plan->AutoContinue)
        {
            Flow->AskForAutoContinue();
        }
        Bodies->Tick(nowMs, peerViews, LocalStateValid ? &LastLocalState : nullptr, bodiesAllowed || Plan->Bodies.InMenuForTesting);
        std::vector<nightcoop_mac::partner_pins::PartnerSpot> partnerSpots;
        for (const auto& view : peerViews)
        {
            partnerSpots.push_back({view.PeerNumber, view.State.PositionX, view.State.PositionY, view.State.PositionZ,
                                    nightcoop_mac::partner_roster::NameOf(view.PeerNumber, *Session)});
        }
        nightcoop_mac::partner_pins::Tick(nowMs, bodiesAllowed && LocalStateValid, partnerSpots, Log);
        ReportPins(nowMs);
        Mirror->Tick(nowMs, *Bodies, *Session);
    }
    ReportRemoteStates(nowMs);
    ShareOwnLook(nowMs);
    ShareOwnClothes(nowMs);
    nightcoop_mac::anim_tap::Tick(nowMs, *Session);
    AnsweredSinceReport += nightcoop_mac::partner_look::TakeAnsweredCount();
    if (AnsweredSinceReport && nowMs >= NextAnsweredReportMs)
    {
        NextAnsweredReportMs = nowMs + 5000;
        Log("look: " + std::to_string(AnsweredSinceReport) + " look request(s) of partner bodies got their own look");
        AnsweredSinceReport = 0;
    }
    if (nowMs >= NextTrafficReportMs)
    {
        NextTrafficReportMs = nowMs + 5000;
        if (const auto traffic = nightcoop_mac::partner_look::TakeTrafficReport(); !traffic.empty())
        {
            Log(traffic);
        }
    }
    return false;
}

bool OnRunningExit(RED4ext::CGameApplication*)
{
    if (Mirror)
    {
        Mirror->ForgetAll();
    }
    nightcoop_mac::anim_tap::Forget();
    if (Bodies)
    {
        Bodies->ForgetAll("forgotten, the game is closing", true);
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
    nightcoop_mac::anim_tap::Install(GameSdk, PluginHandle, Log);
    if (Plan->Bodies.Enabled)
    {
        nightcoop_mac::partner_body_builder::Install(GameSdk, PluginHandle, Log);
        nightcoop_mac::partner_look::Install(GameSdk, PluginHandle, Log);
    }

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
