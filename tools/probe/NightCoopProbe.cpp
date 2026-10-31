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
 PROBE_LOOT=<item record name or 0x tdbid> (1 = Items.money) works as host and as joiner and prints evry loot message (38-44).
 it answers debits and grants of item gives like a player with endless pockets (the host one keeps a small ledger), gives the
 game 5 of the record after ~4 s, shares a bag drop with 3 of it 1 m next to the game player from ~5 s on (LootState evry 5 s,
 a LootTake gets granted and the bag shrinks, PROBE_LOOT_DENY=1 denies them) and takes 1 of the first item of the first open
 drop the game shares, printing the LootTakeResult. the host stays up 10 min with it
 HUB looks like 127.0.0.1:47900#FINGERPRINT
*/
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/LootMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/RosterMessages.hpp"
#include "net/VehicleMessages.hpp"
#include "net/WorldMessages.hpp"
#include "net/NpcMessages.hpp"

#include <Log.hpp>
#include <NetCrypto.hpp>

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
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

class LootProbe
{
public:
    explicit LootProbe(nightcoop_mac::net::RelaySession& session)
        : m_session(session)
    {
        const char* setting = std::getenv("PROBE_LOOT");
        if (!setting)
        {
            return;
        }
        const std::string recordName = std::string(setting) == "1" ? "Items.money" : setting;
        m_record = recordName.rfind("0x", 0) == 0 ? std::strtoull(recordName.c_str(), nullptr, 0) : nightcoop_mac::net::ItemRecordFromName(recordName);
        m_denyTakes = std::getenv("PROBE_LOOT_DENY") != nullptr;
        m_startedAt = std::chrono::steady_clock::now();
        m_bagQuantity = 3;
        std::printf("probe: loot with record %s (%#llx), takes get %s\n", recordName.c_str(), static_cast<unsigned long long>(m_record),
                    m_denyTakes ? "denied" : "granted");
    }

    bool Enabled() const
    {
        return m_record != 0;
    }

    bool OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message)
    {
        using namespace nightcoop_mac::net;
        const uint8_t type = MessageTypeOf(message);
        if (!Enabled() || type < kItemTransferOffer || type > 44)
        {
            return false;
        }
        uint8_t transferType = 0;
        ItemTransfer transfer;
        ItemTransferAck ack;
        LootState state;
        LootTake take;
        LootTakeResult result;
        if (ReadItemTransfer(message, transferType, transfer))
        {
            TakeTransfer(fromPeer, transferType, transfer);
        }
        else if (ReadItemTransferAck(message, ack))
        {
            std::printf("probe: transfer %#llx ack phase %u ok %d from peer %u\n", static_cast<unsigned long long>(ack.TransferId), ack.Phase, ack.Ok,
                        fromPeer);
            TakeAck(ack);
        }
        else if (ReadLootState(message, state))
        {
            TakeState(fromPeer, state);
        }
        else if (ReadLootTake(message, take))
        {
            AnswerTake(fromPeer, take);
        }
        else if (ReadLootTakeResult(message, result))
        {
            std::printf("probe: peer %u answers take %u on container %#010llx: %s\n", fromPeer, result.TakeNumber,
                        static_cast<unsigned long long>(result.ContainerId), result.Granted ? "granted" : "denied");
        }
        else
        {
            std::printf("probe: a loot message type %u from peer %u does not read\n", type, fromPeer);
        }
        std::fflush(stdout);
        return true;
    }

    void Step(bool gamePlayerKnown, float x, float y, float z)
    {
        const auto now = std::chrono::steady_clock::now();
        if (!Enabled() || !m_session.GetStatus().InSession || (WeAreHost() && m_session.PeerNicknames().empty()))
        {
            return;
        }
        if (!m_gaveOnce && now - m_startedAt >= 4s)
        {
            m_gaveOnce = true;
            GiveToTheGame();
        }
        if (gamePlayerKnown && !m_dropPlaced)
        {
            m_dropPlaced = true;
            m_dropX = x + 1.0f;
            m_dropY = y;
            m_dropZ = z;
        }
        if (m_dropPlaced && now - m_startedAt >= 5s && now >= m_nextStateAt)
        {
            m_nextStateAt = now + 5s;
            SendOurBag();
        }
    }

private:
    struct LedgerLine
    {
        nightcoop_mac::net::ItemTransfer Transfer;
        uint32_t Sender = 0;
    };

    bool WeAreHost() const
    {
        return m_session.GetStatus().MyPeerNumber == 0;
    }

    uint32_t MyPeer() const
    {
        return m_session.GetStatus().MyPeerNumber;
    }

    uint64_t OurContainer() const
    {
        return (static_cast<uint64_t>(MyPeer()) << 24) | 1;
    }

    void TakeTransfer(uint32_t fromPeer, uint8_t transferType, const nightcoop_mac::net::ItemTransfer& transfer)
    {
        using namespace nightcoop_mac::net;
        std::printf("probe: item transfer type %u from peer %u: id %#llx %u x %#llx for peer %u\n", transferType, fromPeer,
                    static_cast<unsigned long long>(transfer.TransferId), transfer.Quantity, static_cast<unsigned long long>(transfer.Item.Record),
                    transfer.Recipient);
        if (transferType == kItemTransferOffer && WeAreHost())
        {
            m_ledger[transfer.TransferId] = {transfer, fromPeer};
            m_session.SendGameMessage(WriteItemTransfer(kItemTransferDebit, transfer), static_cast<int32_t>(fromPeer));
        }
        else if (transferType == kItemTransferDebit)
        {
            SendAck(transfer.TransferId, kTransferPhaseDebit, (transfer.TransferId >> 32) == MyPeer());
        }
        else if (transferType == kItemTransferGrant)
        {
            std::printf("probe: we got %u x %#llx from peer %u\n", transfer.Quantity, static_cast<unsigned long long>(transfer.Item.Record),
                        static_cast<uint32_t>(transfer.TransferId >> 32));
            SendAck(transfer.TransferId, kTransferPhaseGrant, true);
        }
    }

    void TakeState(uint32_t fromPeer, const nightcoop_mac::net::LootState& state)
    {
        using namespace nightcoop_mac::net;
        std::printf("probe: peer %u loot state kind %u container %#010llx rev %u state %u bag %d at %.1f %.1f %.1f with %zu entr(ies)", fromPeer, state.Kind,
                    static_cast<unsigned long long>(state.ContainerId), state.Revision, state.State, state.Bag, state.Position[0], state.Position[1],
                    state.Position[2], state.Entries.size());
        for (const auto& entry : state.Entries)
        {
            std::printf(" [%u x %#llx seed %u q %u]", entry.Quantity, static_cast<unsigned long long>(entry.Item.Record), entry.Item.RngSeed, entry.Quality);
        }
        std::printf("\n");
        if (m_tookOnce || state.State != kLootOpen || state.Entries.empty())
        {
            return;
        }
        m_tookOnce = true;
        LootTake ourTake;
        ourTake.Kind = state.Kind;
        ourTake.ContainerId = state.ContainerId;
        ourTake.TakeNumber = 1;
        ourTake.Item = state.Entries[0];
        ourTake.Item.Quantity = 1;
        m_session.SendGameMessage(WriteLootTake(ourTake), static_cast<int32_t>(fromPeer));
        std::printf("probe: taking 1 x %#llx from container %#010llx of peer %u\n", static_cast<unsigned long long>(ourTake.Item.Item.Record),
                    static_cast<unsigned long long>(ourTake.ContainerId), fromPeer);
    }

    void AnswerTake(uint32_t fromPeer, const nightcoop_mac::net::LootTake& take)
    {
        using namespace nightcoop_mac::net;
        const bool ours = take.ContainerId == OurContainer() && RecordWithoutOffset(take.Item.Item.Record) == RecordWithoutOffset(m_record);
        const bool granted = ours && !m_denyTakes && take.Item.Quantity <= m_bagQuantity;
        std::printf("probe: peer %u takes %u x %#llx from container %#010llx (take %u): %s\n", fromPeer, take.Item.Quantity,
                    static_cast<unsigned long long>(take.Item.Item.Record), static_cast<unsigned long long>(take.ContainerId), take.TakeNumber,
                    granted ? "granted" : "denied");
        m_session.SendGameMessage(WriteLootTakeResult({take.Kind, take.ContainerId, take.TakeNumber, granted}), static_cast<int32_t>(fromPeer));
        if (granted)
        {
            m_bagQuantity -= take.Item.Quantity;
            ++m_revision;
            m_nextStateAt = std::chrono::steady_clock::now();
        }
    }

    void GiveToTheGame()
    {
        using namespace nightcoop_mac::net;
        ItemTransfer gift;
        gift.TransferId = (static_cast<uint64_t>(MyPeer()) << 32) | 0x51;
        gift.Item.Record = m_record;
        gift.Quantity = 5;
        if (WeAreHost())
        {
            gift.Recipient = m_session.PeerNicknames().begin()->first;
            m_ledger[gift.TransferId] = {gift, 0};
            m_session.SendGameMessage(WriteItemTransfer(kItemTransferGrant, gift), static_cast<int32_t>(gift.Recipient));
            std::printf("probe: granting 5 x %#llx to peer %u (we are the host, the debit is ours)\n", static_cast<unsigned long long>(m_record),
                        gift.Recipient);
            return;
        }
        gift.Recipient = 0;
        m_session.SendGameMessage(WriteItemTransfer(kItemTransferOffer, gift), 0);
        std::printf("probe: offering 5 x %#llx to the host\n", static_cast<unsigned long long>(m_record));
    }

    void SendOurBag()
    {
        using namespace nightcoop_mac::net;
        LootState state;
        state.Kind = kLootDrop;
        state.ContainerId = OurContainer();
        state.Revision = m_revision;
        state.State = m_bagQuantity ? kLootOpen : kLootClosed;
        state.Bag = true;
        state.Position[0] = m_dropX;
        state.Position[1] = m_dropY;
        state.Position[2] = m_dropZ;
        if (m_bagQuantity)
        {
            state.Entries.push_back({{m_record, 0, 0, 0, 0}, m_bagQuantity, kLootQualityUnknown});
        }
        m_session.SendGameMessage(WriteLootState(state));
        if (m_revision != m_printedRevision)
        {
            m_printedRevision = m_revision;
            std::printf("probe: our bag %#010llx rev %u holds %u at %.1f %.1f %.1f\n", static_cast<unsigned long long>(state.ContainerId), m_revision,
                        m_bagQuantity, m_dropX, m_dropY, m_dropZ);
            std::fflush(stdout);
        }
    }

    void SendAck(uint64_t transferId, uint8_t phase, bool ok)
    {
        if (WeAreHost())
        {
            TakeAck({transferId, phase, ok});
            return;
        }
        m_session.SendGameMessage(nightcoop_mac::net::WriteItemTransferAck({transferId, phase, ok}), 0);
    }

    void TakeAck(const nightcoop_mac::net::ItemTransferAck& ack)
    {
        using namespace nightcoop_mac::net;
        const auto line = m_ledger.find(ack.TransferId);
        if (!WeAreHost() || line == m_ledger.end())
        {
            return;
        }
        const auto& transfer = line->second.Transfer;
        if (ack.Phase == kTransferPhaseDebit && ack.Ok && transfer.Recipient == 0)
        {
            std::printf("probe: host: transfer %#llx debited, we got %u x %#llx, complete\n", static_cast<unsigned long long>(ack.TransferId),
                        transfer.Quantity, static_cast<unsigned long long>(transfer.Item.Record));
        }
        else if (ack.Phase == kTransferPhaseDebit && ack.Ok)
        {
            m_session.SendGameMessage(WriteItemTransfer(kItemTransferGrant, transfer), static_cast<int32_t>(transfer.Recipient));
            return;
        }
        else if (ack.Phase == kTransferPhaseGrant)
        {
            std::printf("probe: host: transfer %#llx %s\n", static_cast<unsigned long long>(ack.TransferId), ack.Ok ? "complete" : "lost at the recipient");
        }
        else
        {
            std::printf("probe: host: transfer %#llx refused by its sender, nothing taken\n", static_cast<unsigned long long>(ack.TransferId));
        }
        m_ledger.erase(line);
    }

    nightcoop_mac::net::RelaySession& m_session;
    uint64_t m_record = 0;
    bool m_denyTakes = false;
    std::chrono::steady_clock::time_point m_startedAt;
    std::chrono::steady_clock::time_point m_nextStateAt;
    std::map<uint64_t, LedgerLine> m_ledger;
    bool m_gaveOnce = false;
    bool m_tookOnce = false;
    bool m_dropPlaced = false;
    float m_dropX = 0;
    float m_dropY = 0;
    float m_dropZ = 0;
    uint32_t m_bagQuantity = 0;
    uint32_t m_revision = 1;
    uint32_t m_printedRevision = 0;
};

int RunHost(nightcoop_mac::net::RelaySession& session, int statesToWaitFor)
{
    bool printedCode = false;
    int statesSeen = 0;
    const char* clock = std::getenv("PROBE_CLOCK");
    const uint32_t clockSeconds = clock ? static_cast<uint32_t>(std::strtoul(clock, nullptr, 0)) : 0;
    auto nextClock = std::chrono::steady_clock::now();
    const char* hostSave = std::getenv("PROBE_HOST_SAVE");
    LootProbe loot(session);
    bool guestKnown = false;
    float guestX = 0;
    float guestY = 0;
    float guestZ = 0;
    const bool staysUp = clock || hostSave || loot.Enabled();
    auto deadline = std::chrono::steady_clock::now() + (staysUp ? 600s : ProbeTimeout);
    auto nextRoster = std::chrono::steady_clock::now();
    std::vector<uint8_t> saveBytes;
    std::vector<uint8_t> metadataBytes;
    size_t saveSent = 0;
    size_t metadataSent = 0;
    uint32_t servingPeer = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (hostSave && session.GetStatus().ConnectedPeers > 0 && std::chrono::steady_clock::now() >= nextRoster)
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
            if (loot.OnMessage(message.FromPeer, message.Message))
            {
                continue;
            }
            if (hostSave && nightcoop_mac::net::ReadWorldWant(message.Message, want))
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
        loot.Step(guestKnown, guestX, guestY, guestZ);
        for (const auto& state : session.TakeRemotePlayerStates())
        {
            guestKnown = true;
            guestX = state.PositionX;
            guestY = state.PositionY;
            guestZ = state.PositionZ;
            if (staysUp && statesSeen++ % 100 != 0)
            {
                continue;
            }
            std::printf("probe: PlayerState from peer %u: pos %.2f %.2f %.2f yaw %.1f speed %.2f t %llu\n", state.PeerNumber,
                        state.PositionX, state.PositionY, state.PositionZ, state.Yaw, state.Speed,
                        static_cast<unsigned long long>(state.SessionTimeMs));
            std::fflush(stdout);
            if (!staysUp && ++statesSeen >= statesToWaitFor)
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
    LootProbe loot(session);
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
        loot.Step(hostPositionKnown, hostX, hostY, hostZ);
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
            nightcoop_mac::net::AnimWant want;
            nightcoop_mac::net::AnimBatch batch;
            uint32_t gameSeconds = 0;
            if (loot.OnMessage(message.FromPeer, message.Message))
            {
                continue;
            }
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
