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
 PROBE_HOST_SAVE=<save folder> makes the host say it is in its world (SessionRoster) and answer a WorldWant with that
 folders metadata.*.json and sav.dat (WorldStatus 3 + WorldFile chunks), printing what the guest says back
 PROBE_CLOCK=<game seconds> makes the host send that GameClock evry 5 s to its guests and stay up for 10 min
 PROBE_VEHICLE=0x<tweakdbid> makes the joiner share a car of that record it drives along its walk (VehicleSpawn evry ~5 s,
 VehicleState and VehicleSeat evry step), and the joiner prints the hosts vehicle messages
 PROBE_NPC=0x<character record> makes the joiner share an npc of that record that walks 2 m beside it (NpcSpawn evry 625 ms,
 NpcSnapshot evry step) and lets it go at the end (NpcDespawn). its NpcAnim stream answers a NpcAnimWant with a full state and
 sends a float and an event evry step after that. npcs the host shares get counted with their NpcAnim batches, the first one
 gets a NpcAnimWant
 PROBE_LIFT=1 makes the joiner ask the host for a lift that does not exist (LiftSync kind 3) and print the answer
 PROBE_RIDE=1 makes the joiner sit in the front right seat of the car the host shares (VehicleSeat evry step)
 PROBE_DOWN=1 makes the joiner lie downed (flag 0x100) right next to the host (0.5 m) until a PlayerRevive comes
 PROBE_REVIVE=1 makes the joiner stand still and help the host up (PlayerRevive) 3 s after the host is down (flag 0x100)
 PROBE_WEAPON=0x<tweakdbid> in the environment makes the joiner hold that weapon and fire a shot evry 2 s (aim + shoot flags),
 and the joiner prints the hosts weapon fields whenever they change (health, camera and velocity evry 100 states)
 PROBE_QUESTS=1 on the host makes it say it is in its world, answer a QuestWant with a snapshot (the fact
 nightcoop_mac_probe_fact = 1, gmpl_player_dodged = 1 which a guest has to keep as its own, and a journal entry 0x4e434f50
 nobody has) and send nightcoop_mac_probe_fact + 1 as a delta evry 5 s for 10 min. on the joiner it sends QuestWant for
 world 1 at step 10, the same world again at step 20 (the host has to ignore that) and world 2 at step 110, and prints the
 hosts QuestFacts / QuestJournal batches with a few well known facts out of the snapshots
 HUB looks like 127.0.0.1:47900#FINGERPRINT
*/
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/RosterMessages.hpp"
#include "net/VehicleMessages.hpp"
#include "net/WorldMessages.hpp"
#include "net/NpcMessages.hpp"
#include "net/QuestMessages.hpp"

#include <Log.hpp>
#include <NetCrypto.hpp>
#include <ProtocolCore.hpp>

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
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
const char* const WellKnownFacts[] = {"q000_done", "q001_mission0_complete", "q101_johnny_char_entry", "q101_done", "glossary_scavengers",
                                      "nightcoop_host_world", "nightcoop_mac_probe_fact"};

void SendProbeSnapshot(nightcoop_mac::net::RelaySession& session, uint32_t peer, uint32_t number, int32_t probeFactValue)
{
    using namespace nightcoop_mac::net;
    QuestFact probeFact;
    probeFact.FactId = FactIdOf("nightcoop_mac_probe_fact");
    probeFact.Value = probeFactValue;
    QuestFact ownFact;
    ownFact.FactId = FactIdOf("gmpl_player_dodged");
    ownFact.Value = 1;
    for (const auto& message : SplitQuestFacts(true, number, {probeFact, ownFact}))
    {
        session.SendGameMessage(message, static_cast<int32_t>(peer));
    }
    QuestJournalEntry strangeEntry;
    strangeEntry.EntryHash = 0x4e434f50;
    strangeEntry.State = 2;
    strangeEntry.Notify = kJournalQuiet;
    for (const auto& message : SplitQuestJournal(true, number, true, 0, {strangeEntry}))
    {
        session.SendGameMessage(message, static_cast<int32_t>(peer));
    }
}

void PrintQuestMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint32_t& factBatches, uint32_t& journalBatches)
{
    using namespace nightcoop_mac::net;
    QuestFacts facts;
    QuestJournal journal;
    if (ReadQuestFacts(message, facts))
    {
        if (facts.Batch != kQuestSnapshotPart || factBatches < 3)
        {
            std::printf("probe: peer %u QuestFacts batch %u #%u total %u with %zu fact(s)\n", fromPeer, facts.Batch, facts.Number, facts.Total,
                        facts.Facts.size());
        }
        ++factBatches;
        for (const auto& fact : facts.Facts)
        {
            for (const char* factName : WellKnownFacts)
            {
                if (fact.Table == kQuestFactsTable && fact.FactId == FactIdOf(factName))
                {
                    std::printf("probe:   %s = %d\n", factName, fact.Value);
                }
            }
        }
        if (facts.Batch == kQuestDelta && !facts.Facts.empty() && factBatches < 40)
        {
            std::printf("probe:   first change %#010x = %d\n", facts.Facts[0].FactId, facts.Facts[0].Value);
        }
    }
    else if (ReadQuestJournal(message, journal))
    {
        if (journal.Batch != kQuestSnapshotPart || journalBatches < 3)
        {
            std::printf("probe: peer %u QuestJournal batch %u #%u total %u with %zu entr(ies), tracked %s %#010x\n", fromPeer, journal.Batch,
                        journal.Number, journal.Total, journal.Entries.size(), journal.TrackedKnown ? "yes" : "no", journal.Tracked);
        }
        ++journalBatches;
        if (journal.Batch == kQuestDelta && !journal.Entries.empty() && journalBatches < 40)
        {
            std::printf("probe:   first entry %#010x state %u notify %u\n", journal.Entries[0].EntryHash, journal.Entries[0].State,
                        journal.Entries[0].Notify);
        }
    }
    else
    {
        std::printf("probe: peer %u sent a quest message that does not read (%zu bytes)\n", fromPeer, message.size());
    }
    std::fflush(stdout);
}

int RunHost(nightcoop_mac::net::RelaySession& session, int statesToWaitFor)
{
    bool printedCode = false;
    int statesSeen = 0;
    const char* clock = std::getenv("PROBE_CLOCK");
    const uint32_t clockSeconds = clock ? static_cast<uint32_t>(std::strtoul(clock, nullptr, 0)) : 0;
    auto nextClock = std::chrono::steady_clock::now();
    const char* hostSave = std::getenv("PROBE_HOST_SAVE");
    const bool probeQuests = std::getenv("PROBE_QUESTS") != nullptr;
    auto deadline = std::chrono::steady_clock::now() + (clock || hostSave || probeQuests ? 600s : ProbeTimeout);
    auto nextQuestDelta = std::chrono::steady_clock::now();
    std::set<uint32_t> questPeers;
    uint32_t questSnapshots = 0;
    int32_t probeFactValue = 1;
    auto nextRoster = std::chrono::steady_clock::now();
    std::vector<uint8_t> saveBytes;
    std::vector<uint8_t> metadataBytes;
    size_t saveSent = 0;
    size_t metadataSent = 0;
    uint32_t servingPeer = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if ((hostSave || probeQuests) && session.GetStatus().ConnectedPeers > 0 && std::chrono::steady_clock::now() >= nextRoster)
        {
            nextRoster = std::chrono::steady_clock::now() + 2s;
            nightcoop_mac::net::SessionRoster roster;
            roster.Revision = 1;
            roster.HostState = nightcoop_mac::net::kHostStateInWorld;
            roster.Entries.push_back({0, "probe host", nightcoop_mac::net::kWhereInWorld, 0, nightcoop_mac::net::kRoleHost, 0x80, 0, 0, "", {}, ""});
            session.SendGameMessage(nightcoop_mac::net::WriteSessionRoster(roster));
        }
        for (const auto& message : session.TakeGameMessages())
        {
            nightcoop_mac::net::WorldWant want;
            nightcoop_mac::net::WorldStatus worldStatus;
            uint32_t questWorld = 0;
            if (probeQuests && nightcoop_mac::net::ReadQuestWant(message.Message, questWorld))
            {
                std::printf("probe: peer %u loaded its world #%u and wants our facts and journal, snapshot #%u goes\n", message.FromPeer, questWorld,
                            questSnapshots + 1);
                SendProbeSnapshot(session, message.FromPeer, ++questSnapshots, probeFactValue);
                questPeers.insert(message.FromPeer);
                std::fflush(stdout);
            }
            else if (hostSave && nightcoop_mac::net::ReadWorldWant(message.Message, want))
            {
                std::printf("probe: peer %u wants our world (reason %u), sending %s\n", message.FromPeer, want.Reason, hostSave);
                std::ifstream save(std::string(hostSave) + "/sav.dat", std::ios::binary);
                saveBytes.assign(std::istreambuf_iterator<char>(save), std::istreambuf_iterator<char>());
                std::ifstream metadata(std::string(hostSave) + "/metadata.9.json", std::ios::binary);
                metadataBytes.assign(std::istreambuf_iterator<char>(metadata), std::istreambuf_iterator<char>());
                saveSent = 0;
                metadataSent = 0;
                servingPeer = message.FromPeer;
                session.SendGameMessage(nightcoop_mac::net::WriteWorldStatus({3, 1, "probe-host-save"}), static_cast<int32_t>(servingPeer));
            }
            else if (nightcoop_mac::net::ReadWorldStatus(message.Message, worldStatus))
            {
                std::printf("probe: peer %u says world status %u %s\n", message.FromPeer, worldStatus.Code, worldStatus.Text.c_str());
            }
        }
        for (int chunkCount = 0; servingPeer && chunkCount < 8 && (metadataSent < metadataBytes.size() || saveSent < saveBytes.size()); ++chunkCount)
        {
            const bool metadataFirst = metadataSent < metadataBytes.size();
            const auto& bytes = metadataFirst ? metadataBytes : saveBytes;
            size_t& sent = metadataFirst ? metadataSent : saveSent;
            nightcoop_mac::net::WorldFile chunk;
            chunk.TransferId = 1;
            chunk.Kind = metadataFirst ? nightcoop_mac::net::kWorldFileMetadata : nightcoop_mac::net::kWorldFileSave;
            chunk.TotalSize = static_cast<uint32_t>(bytes.size());
            static uint64_t metadataHash = 0;
            static uint64_t saveHash = 0;
            if (sent == 0)
            {
                (metadataFirst ? metadataHash : saveHash) = nightcoop_mac::net::WorldFileHash(bytes);
            }
            chunk.Hash = metadataFirst ? metadataHash : saveHash;
            chunk.Offset = static_cast<uint32_t>(sent);
            const size_t length = std::min<size_t>(nightcoop_mac::net::kWorldChunkBytes, bytes.size() - sent);
            chunk.Bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(sent), bytes.begin() + static_cast<std::ptrdiff_t>(sent + length));
            session.SendGameMessage(nightcoop_mac::net::WriteWorldFile(chunk), static_cast<int32_t>(servingPeer));
            sent += length;
            if (!metadataFirst && sent >= bytes.size())
            {
                std::printf("probe: sent the whole save (%zu bytes)\n", bytes.size());
            }
        }
        if (probeQuests && !questPeers.empty() && std::chrono::steady_clock::now() >= nextQuestDelta)
        {
            nextQuestDelta = std::chrono::steady_clock::now() + 5s;
            nightcoop_mac::net::QuestFact probeFact;
            probeFact.FactId = nightcoop_mac::net::FactIdOf("nightcoop_mac_probe_fact");
            probeFact.Value = ++probeFactValue;
            for (const uint32_t peer : questPeers)
            {
                for (const auto& message : nightcoop_mac::net::SplitQuestFacts(false, 0, {probeFact}))
                {
                    session.SendGameMessage(message, static_cast<int32_t>(peer));
                }
            }
            std::printf("probe: delta nightcoop_mac_probe_fact = %d to %zu peer(s)\n", probeFactValue, questPeers.size());
            std::fflush(stdout);
        }
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
            if ((clock || hostSave || probeQuests) && statesSeen++ % 100 != 0)
            {
                continue;
            }
            std::printf("probe: PlayerState from peer %u: pos %.2f %.2f %.2f yaw %.1f speed %.2f t %llu\n", state.PeerNumber,
                        state.PositionX, state.PositionY, state.PositionZ, state.Yaw, state.Speed,
                        static_cast<unsigned long long>(state.SessionTimeMs));
            std::fflush(stdout);
            if (!clock && !hostSave && !probeQuests && ++statesSeen >= statesToWaitFor)
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
    std::set<uint32_t> hostNpcs;
    uint32_t hostNpcsGone = 0;
    uint32_t hostNpcAnimBatches = 0;
    uint32_t hostNpcAnimInputs = 0;
    uint32_t hostNpcAnimFullStates = 0;
    std::set<uint32_t> hostNpcsAnimated;
    bool hostNpcAnimAsked = false;
    bool probeNpcWanted = false;
    uint32_t probeNpcSequence = 0;
    uint32_t rosterRevision = 0xFFFFFFFF;
    const bool probeQuests = std::getenv("PROBE_QUESTS") != nullptr;
    uint32_t questFactBatches = 0;
    uint32_t questJournalBatches = 0;
    uint32_t hostShots = 0;
    uint16_t hostWeaponFlags = 0;
    uint8_t hostWeaponState = 0;
    const bool probeRevives = std::getenv("PROBE_REVIVE") != nullptr || std::getenv("PROBE_RIDE") != nullptr;
    const bool probeRides = std::getenv("PROBE_RIDE") != nullptr;
    uint32_t hostCarNetId = 0;
    bool rideSent = false;
    bool probeLiesDown = std::getenv("PROBE_DOWN") != nullptr;
    bool hostPositionKnown = false;
    float hostX = 0;
    float hostY = 0;
    float hostZ = 0;
    int hostDownSinceStep = -1;
    bool hostWasDown = false;
    uint64_t probeVehicle = 0;
    if (const char* vehicle = std::getenv("PROBE_VEHICLE"))
    {
        probeVehicle = std::strtoull(vehicle, nullptr, 0);
        std::printf("probe: driving a car of record %#llx\n", static_cast<unsigned long long>(probeVehicle));
    }
    const uint32_t probeCarNetId = (session.GetStatus().MyPeerNumber << 24) | 1;
    if (const char* weapon = std::getenv("PROBE_WEAPON"))
    {
        ProbeWeapon = std::strtoull(weapon, nullptr, 0);
        std::printf("probe: holding weapon %#llx and shooting evry 2 s\n", static_cast<unsigned long long>(ProbeWeapon));
    }
    for (int step = 0; step < WalkSteps; ++step)
    {
        nightcoop_mac::net::PlayerState state;
        const int stepInLeg = step % 300;
        state.PositionX = WalkStartX + (probeRevives ? 0.0f : (stepInLeg < 150 ? stepInLeg : 300 - stepInLeg) * 0.05f);
        state.PositionY = WalkStartY;
        state.PositionZ = WalkStartZ;
        state.Yaw = 90.0f;
        state.Speed = 5.0f;
        state.Flags = (step / 40) % 2 ? 1 : 0;
        if (std::getenv("PROBE_DOWN"))
        {
            state.Flags = probeLiesDown ? 0x100 : 0;
            if (hostPositionKnown)
            {
                state.PositionX = hostX + 0.5f;
                state.PositionY = hostY;
                state.PositionZ = hostZ;
            }
        }
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
        if (probeVehicle)
        {
            const float halfYaw = state.Yaw * 3.14159265f / 360.0f;
            const float orientation[4] = {0, 0, std::sin(halfYaw), std::cos(halfYaw)};
            if (step % 60 == 0)
            {
                nightcoop_mac::net::VehicleSpawn spawn;
                spawn.NetId = probeCarNetId;
                spawn.HomePeer = session.GetStatus().MyPeerNumber;
                spawn.Record = probeVehicle;
                spawn.Position[0] = state.PositionX;
                spawn.Position[1] = state.PositionY;
                spawn.Position[2] = state.PositionZ;
                std::copy(orientation, orientation + 4, spawn.Orientation);
                session.SendGameMessage(nightcoop_mac::net::WriteVehicleSpawn(spawn));
            }
            nightcoop_mac::net::VehicleState carState;
            carState.NetId = probeCarNetId;
            carState.StampMs = session.SessionClockMs();
            carState.Position[0] = state.PositionX;
            carState.Position[1] = state.PositionY;
            carState.Position[2] = state.PositionZ;
            std::copy(orientation, orientation + 4, carState.Orientation);
            carState.Velocity[0] = (stepInLeg < 150 ? 1.0f : -1.0f) * 0.05f / 0.08f;
            carState.Flags = nightcoop_mac::net::kVehicleEngineOn | nightcoop_mac::net::kVehicleSomeoneDrives;
            carState.Occupants.push_back({NameHash("seat_front_left"), nightcoop_mac::net::kOccupantPlayer, session.GetStatus().MyPeerNumber});
            session.SendGameMessage(nightcoop_mac::net::WriteVehicleState(carState), nightcoop_mac::net::kEveryPeer, false);
            nightcoop_mac::net::VehicleSeat seat;
            seat.NetId = probeCarNetId;
            seat.Slot = NameHash("seat_front_left");
            session.SendGameMessage(nightcoop_mac::net::WriteVehicleSeat(seat), nightcoop_mac::net::kEveryPeer, step == 0);
        }
        if (const char* npcRecord = std::getenv("PROBE_NPC"))
        {
            const uint32_t npcNetId = (session.GetStatus().MyPeerNumber << 24) | 7;
            if (step % 8 == 0)
            {
                nightcoop_mac::net::NpcSpawn spawn;
                spawn.NetId = npcNetId;
                spawn.Record = std::strtoull(npcRecord, nullptr, 0);
                spawn.Position[0] = state.PositionX;
                spawn.Position[1] = state.PositionY + 2.0f;
                spawn.Position[2] = state.PositionZ;
                spawn.Yaw = state.Yaw;
                session.SendGameMessage(nightcoop_mac::net::WriteNpcSpawn(spawn));
            }
            nightcoop_mac::net::NpcSnapshot npcState;
            npcState.NetId = npcNetId;
            npcState.Position[0] = state.PositionX;
            npcState.Position[1] = state.PositionY + 2.0f;
            npcState.Position[2] = state.PositionZ;
            npcState.Yaw = state.Yaw;
            npcState.Speed = state.Speed;
            npcState.OwnerClockMs = session.SessionClockMs();
            session.SendGameMessage(nightcoop_mac::net::WriteNpcSnapshot(npcState), nightcoop_mac::net::kEveryPeer, false);
            if (probeNpcWanted)
            {
                nightcoop_mac::net::AnimBatch npcAnim;
                npcAnim.Unknown00 = npcNetId;
                npcAnim.EntriesHaveExtra = true;
                npcAnim.Unknown08 = session.SessionClockMs();
                npcAnim.Unknown10 = ++probeNpcSequence;
                nightcoop_mac::net::AnimEntry speedInput;
                speedInput.Kind = nightcoop_mac::net::AnimKind::Float;
                speedInput.Name = NameHash("speed");
                speedInput.Values[0] = state.Speed;
                nightcoop_mac::net::AnimEntry stepEvent;
                stepEvent.Kind = nightcoop_mac::net::AnimKind::Event;
                stepEvent.Name = NameHash("probe_step");
                npcAnim.Entries = {speedInput, stepEvent};
                session.SendGameMessage(nightcoop_mac::net::WriteNpcAnim(npcAnim));
            }
            if (step == WalkSteps - 1)
            {
                session.SendGameMessage(nightcoop_mac::net::WriteNpcDespawn({npcNetId, 1, 0xFFFFFFFF}));
            }
        }
        if (probeQuests && (step == 10 || step == 20 || step == 110))
        {
            const uint32_t world = step == 110 ? 2 : 1;
            session.SendGameMessage(nightcoop_mac::net::WriteQuestWant(world), 0);
            std::printf("probe: QuestWant for our world #%u%s\n", world, step == 20 ? " again (the host should ignore it)" : "");
            std::fflush(stdout);
        }
        if (std::getenv("PROBE_LIFT") && step == 30)
        {
            nightcoop_mac::net::LiftSync request;
            request.Kind = nightcoop_mac::net::kLiftRequest;
            request.Peer = session.GetStatus().MyPeerNumber;
            request.LiftKey = 0x1234;
            request.FloorTo = 2;
            request.RequestNumber = 1;
            request.RequestKind = 1;
            session.SendGameMessage(nightcoop_mac::net::WriteLiftSync(request), 0);
            std::printf("probe: asked the host for lift 0x1234 to floor 2\n");
        }
        if (probeRides && hostCarNetId)
        {
            nightcoop_mac::net::VehicleSeat ride;
            ride.NetId = hostCarNetId;
            ride.Slot = NameHash("seat_front_right");
            session.SendGameMessage(nightcoop_mac::net::WriteVehicleSeat(ride), nightcoop_mac::net::kEveryPeer, !rideSent);
            if (!rideSent)
            {
                std::printf("probe: sitting in the hosts car %#010x\n", hostCarNetId);
                rideSent = true;
            }
        }
        if (step % 5 == 0)
        {
            session.SendGameMessage(MakeTestAnimBatch(step));
        }
        if (step % 25 == 0)
        {
            nightcoop_mac::net::PlayerPresence presence;
            presence.Name = "probe partner";
            presence.Where = nightcoop_mac::net::kWhereInWorld;
            presence.Flags = nightcoop_mac::net::kPresenceInHostWorld;
            session.SendGameMessage(nightcoop_mac::net::WritePlayerPresence(presence), 0);
        }
        for (const auto& hostState : session.TakeRemotePlayerStates())
        {
            hostPositionKnown = true;
            hostX = hostState.PositionX;
            hostY = hostState.PositionY;
            hostZ = hostState.PositionZ;
            const bool hostDown = (hostState.Flags & 0x100) != 0;
            if (hostDown != hostWasDown)
            {
                hostWasDown = hostDown;
                hostDownSinceStep = hostDown ? step : -1;
                std::printf("probe: peer %u is %s\n", hostState.PeerNumber, hostDown ? "down (flag 0x100)" : "up again");
                std::fflush(stdout);
            }
            if (probeRevives && hostDown && hostDownSinceStep >= 0 && step - hostDownSinceStep >= 38)
            {
                nightcoop_mac::net::PlayerRevive revive;
                revive.TargetPeer = hostState.PeerNumber;
                revive.HelperPosition[0] = state.PositionX;
                revive.HelperPosition[1] = state.PositionY;
                revive.HelperPosition[2] = state.PositionZ;
                session.SendGameMessage(nightcoop_mac::net::WritePlayerRevive(revive), static_cast<int32_t>(hostState.PeerNumber));
                std::printf("probe: helping peer %u up\n", hostState.PeerNumber);
                hostDownSinceStep = step + 1000;
            }
            if (hostStatesSeen++ % 100 == 0)
            {
                std::printf("probe: peer %u health %.2f time dilation %.2f camera pitch %.1f aim offset %.1f velocity %.2f %.2f %.2f\n",
                            hostState.PeerNumber, hostState.Health, hostState.TimeDilation, hostState.CameraPitch, hostState.AimYawOffset,
                            hostState.VelocityX, hostState.VelocityY, hostState.VelocityZ);
                std::fflush(stdout);
            }
            const uint16_t weaponFlags = hostState.Flags & 0x83D4;
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
            const uint8_t messageType = nightcoop_mac::net::MessageTypeOf(message.Message);
            if (messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestFacts) || messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestJournal))
            {
                PrintQuestMessage(message.FromPeer, message.Message, questFactBatches, questJournalBatches);
                continue;
            }
            nightcoop_mac::net::AnimWant want;
            nightcoop_mac::net::AnimBatch batch;
            uint32_t gameSeconds = 0;
            nightcoop_mac::net::NpcSpawn hostNpc;
            nightcoop_mac::net::NpcDespawn hostNpcGone;
            if (nightcoop_mac::net::ReadNpcSpawn(message.Message, hostNpc))
            {
                if (hostNpcs.insert(hostNpc.NetId).second && hostNpcs.size() <= 5)
                {
                    std::printf("probe: peer %u shares npc %#010x record %#llx at %.1f %.1f %.1f\n", message.FromPeer, hostNpc.NetId,
                                static_cast<unsigned long long>(hostNpc.Record), hostNpc.Position[0], hostNpc.Position[1], hostNpc.Position[2]);
                }
                continue;
            }
            if (nightcoop_mac::net::ReadNpcDespawn(message.Message, hostNpcGone))
            {
                ++hostNpcsGone;
                continue;
            }
            nightcoop_mac::net::AnimBatch npcAnim;
            if (nightcoop_mac::net::ReadNpcAnim(message.Message, npcAnim))
            {
                ++hostNpcAnimBatches;
                hostNpcAnimInputs += static_cast<uint32_t>(npcAnim.Entries.size());
                hostNpcsAnimated.insert(npcAnim.Unknown00);
                if (npcAnim.FullState)
                {
                    ++hostNpcAnimFullStates;
                    std::printf("probe: full anim state of npc %#010x: %zu input(s), batch %u\n", npcAnim.Unknown00, npcAnim.Entries.size(), npcAnim.Unknown10);
                }
                if (!hostNpcAnimAsked && hostNpcs.count(npcAnim.Unknown00))
                {
                    hostNpcAnimAsked = true;
                    session.SendGameMessage(nightcoop_mac::net::WriteNpcAnimWant({npcAnim.Unknown00, true, true}), static_cast<int32_t>(message.FromPeer));
                }
                continue;
            }
            nightcoop_mac::net::NpcAnimWant npcWant;
            if (nightcoop_mac::net::ReadNpcAnimWant(message.Message, npcWant))
            {
                std::printf("probe: peer %u wants the full anim state of npc %#010x\n", message.FromPeer, npcWant.NetId);
                if (!probeNpcWanted && std::getenv("PROBE_NPC"))
                {
                    probeNpcWanted = true;
                    nightcoop_mac::net::AnimBatch fullState;
                    fullState.Unknown00 = npcWant.NetId;
                    fullState.FullState = true;
                    fullState.EntriesHaveExtra = true;
                    fullState.Unknown10 = ++probeNpcSequence;
                    nightcoop_mac::net::AnimEntry speedInput;
                    speedInput.Kind = nightcoop_mac::net::AnimKind::Float;
                    speedInput.Name = NameHash("speed");
                    nightcoop_mac::net::AnimEntry crouchInput;
                    crouchInput.Kind = nightcoop_mac::net::AnimKind::Bool;
                    crouchInput.Name = NameHash("crouch");
                    fullState.Entries = {speedInput, crouchInput};
                    session.SendGameMessage(nightcoop_mac::net::WriteNpcAnim(fullState), static_cast<int32_t>(message.FromPeer));
                }
                continue;
            }
            nightcoop_mac::net::LiftSync lift;
            if (nightcoop_mac::net::ReadLiftSync(message.Message, lift))
            {
                std::printf("probe: lift message kind %u for %#llx floor %d result %u\n", lift.Kind, static_cast<unsigned long long>(lift.LiftKey),
                            lift.FloorTo, lift.Result);
                continue;
            }
            nightcoop_mac::net::PlayerRevive revive;
            if (nightcoop_mac::net::ReadPlayerRevive(message.Message, revive))
            {
                std::printf("probe: peer %u helps us up (from %.1f %.1f %.1f), getting up\n", message.FromPeer, revive.HelperPosition[0],
                            revive.HelperPosition[1], revive.HelperPosition[2]);
                probeLiesDown = false;
                continue;
            }
            nightcoop_mac::net::AnimSetup setup;
            nightcoop_mac::net::SessionRoster roster;
            nightcoop_mac::net::VehicleSpawn hostCar;
            if (nightcoop_mac::net::ReadVehicleSpawn(message.Message, hostCar))
            {
                std::printf("probe: peer %u shares car %#010x record %#llx at %.1f %.1f %.1f\n", message.FromPeer, hostCar.NetId,
                            static_cast<unsigned long long>(hostCar.Record), hostCar.Position[0], hostCar.Position[1], hostCar.Position[2]);
                hostCarNetId = hostCar.NetId;
                continue;
            }
            if (nightcoop_mac::net::ReadSessionRoster(message.Message, roster))
            {
                if (roster.Revision != rosterRevision)
                {
                    rosterRevision = roster.Revision;
                    std::printf("probe: roster rev %u host state %u:", roster.Revision, roster.HostState);
                    for (const auto& entry : roster.Entries)
                    {
                        std::printf(" [peer %u '%s' where %u role %u]", entry.PeerNumber, entry.Name.c_str(), entry.Where, entry.Role);
                    }
                    std::printf("\n");
                }
                continue;
            }
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
    if (probeQuests)
    {
        std::printf("probe: got %u QuestFacts and %u QuestJournal batch(es) from the host\n", questFactBatches, questJournalBatches);
    }
    if (!hostNpcs.empty() || hostNpcsGone)
    {
        std::printf("probe: the host shared %zu npc(s) with us, %u despawn(s)\n", hostNpcs.size(), hostNpcsGone);
        std::printf("probe: npc anim from the host: %u batch(es), %u input(s), %u full state(s), %zu npc(s) animated\n", hostNpcAnimBatches,
                    hostNpcAnimInputs, hostNpcAnimFullStates, hostNpcsAnimated.size());
    }
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
