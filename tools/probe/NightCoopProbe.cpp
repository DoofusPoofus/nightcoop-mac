/*
 nightcoop probe
 a headless nightcoop client on the same RelaySession the plugin uses
 host mode opens a session prints the code and prints evry PlayerState it gets
 join mode joins by code says its look is male then walks PlayerStates back and forth along x (from X Y Z if given
 150 steps of 80 ms or STEPS of them) crouching on and off and sends PlayerAnim batches (a float a bool an event and a
 feature) in Game envelopes
 watch mode joins by code and prints the PlayerStates and game messages it gets for a while
 usage: nightcoop_probe host HUB [states to wait for] | join CODE HUB [X Y Z [STEPS [LOOKFILE [CLOTHESFILE]]]] | watch CODE HUB [seconds]
 CLOTHESFILE is a PlayerLook (31) as it is (the plugin writes own-clothes.bin) sent right after the look
 its look (PlayerCustomization 33) is LOOKFILE as it is (the plugin writes own-look.bin) or a male look with no parts
 PROBE_LEAVE_AFTER_ARRIVAL=1 makes such a host leave 20 s after a guest says it arrived (WorldStatus 7. with PROBE_RELOAD after the reload)
 PROBE_HOST_SAVE=<save folder> makes the host say it is in its world (SessionRoster) and answer a WorldWant with that
 folders metadata.*.json and sav.dat (WorldStatus 3 + WorldFile chunks) printing what the guest says back
 PROBE_CLOCK=<game seconds> makes the host send that GameClock evry 5 s to its guests and stay up for 10 min
 PROBE_VEHICLE=0x<tweakdbid> makes the joiner share a car of that record it drives along its walk (VehicleSpawn evry ~5 s
 VehicleState and VehicleSeat evry step) and the joiner prints the hosts vehicle messages
 PROBE_NPC=0x<character record> makes the joiner share an npc of that record that walks 2 m beside it (NpcSpawn evry 625 ms
 NpcSnapshot evry step) and lets it go at the end (NpcDespawn). its NpcAnim stream answers a NpcAnimWant with a full state and
 sends a float and an event evry step after that. npcs the host shares get counted with their NpcAnim batches the first one
 gets a NpcAnimWant. the probe npc turns hostile to the host halfway with 60 % health and combat states and holds
 PROBE_NPC_WEAPON=0x<record> then. HitClaims on it get answered like an owner (HitResult NpcDespawn 3 with the killer at 0 %)
 PROBE_CLAIM claims 35 % of the first npc the host shares evry 2 s
 PROBE_HACK=1 quickhacks the first npc the host shares with QuickHack.BlindLvl1Hack evry 4 s (HackSync completion)
 PROBE_REWARD=<reward record name> on the host sends it as a quest reward (RewardWord kind 1) evry 10 s, the joiner prints
 the hosts rewards
 PROBE_CALL=<contact> on the host rings that contact 25 s after the guest is there, starts the call 6 s later and ends it
 20 s after the ring (PhoneSync commits)
 PROBE_BLAST=0x<device key> [PROBE_BLAST_X/Y/Z] says at step 100 that our player blew up that barrel (DeviceBlast)
 PROBE_DEVICE_ECHO=1 sends evry device action the host shares back to it as our own request 40 steps later
 PROBE_FAR=<meters> moves the joiners walk that far along x from where it was asked to stand
 PROBE_RELOAD=<save folder> (with PROBE_HOST_SAVE) makes the host load that save 60 s after the guest arrived: WorldReload 1
 then the new save goes out like a join
 PROBE_DEFEAT=<n> tells the host n of its shared npcs (with health above 0) died here (NpcDefeat) the joiner prints NpcDead tables and npc deaths
 PROBE_PUSH=1 says at step 110 that our grenade went off where the joiner stands (PlayerBlast radius 6)
 PROBE_FX=<effect resource path hash> shows that effect at step 120 where the joiner stands (PlayerFx)
 PROBE_CARHIT=1 (with PROBE_VEHICLE) says at step 200 that our car hit the hosts player (VehicleHit 9 m/s)
 PROBE_DEAD=1 on the host says the first npc a guest shares 40 s on died in our world (NpcDead) and prints NpcDefeat
 PROBE_TAKE=1 takes the first npc the host hands over (NpcHandoffAck + NpcClaim 3), owns it for 100 steps and lets it go,
 without it evry offer gets refused
 PROBE_OFFER=1 (with PROBE_NPC) hands its npc to the host at step 120 and stops driving it
 PROBE_MIRROR shares one of the hosts own npcs back to it as our npc (same record entity id keys and spot) for 120 steps
 so the host should make its own npc our puppet and give it back after
 PROBE_HIT=<attack record name> (with PROBE_NPC) says our npc hit the hosts body with that attack evry 3 s (PlayerHit)
 PROBE_LIFT=1 makes the joiner ask the host for a lift that does not exist (LiftSync kind 3) and print the answer
 PROBE_DOOR=0x<door key> (the game logs keys as doors: our door 0x...) makes the joiner stand still at X Y Z (so its body can
 hold that door open) and tell the host about that door evry 200 steps (DoorSync kind 1: opened at step 30 / closed by itself
 at 80 / quest ForceLock at 120 / quest ForceUnlock at 160) printing evry DoorSync that comes back. on the host it flips that
 door open / closed for evryone evry 8 s (kind 2) and takes evry kind 1 a guest sends (answer with the request number) for 10 min
 PROBE_RIDE=1 makes the joiner sit in the front right seat of the car the host shares (VehicleSeat evry step)
 PROBE_DOWN=1 makes the joiner lie downed (flag 0x100) right next to the host (0.5 m) until a PlayerRevive comes
 PROBE_REVIVE=1 makes the joiner stand still and help the host up (PlayerRevive) 3 s after the host is down (flag 0x100)
 PROBE_WEAPON=0x<tweakdbid> in the environment makes the joiner hold that weapon and fire a shot evry 2 s (aim + shoot flags)
 and the joiner prints the hosts weapon fields whenever they change (health camera and velocity evry 100 states)
 PROBE_DIALOG=<option>[<rows>] on the host opens a choice vote evry 30 s (DialogBallot with rows fake options 3 by default
 then DialogTally) decides the option the guest votes for right away or <option> after 20 s (shared time + 1.5 s) sends a
 DialogLine evry 6 s and stays up for 10 min. on the joiner it prints the hosts ballots tallies lines and skips votes for
 <option> 2 s after a ballot walks for at least 4 min and sends one scene skip press (SceneLineSkip) at step 100 and its end
 at step 140
 PROBE_QUESTS=1 on the host makes it say it is in its world answer a QuestWant with a snapshot (the fact
 nightcoop_mac_probe_fact = 1 gmpl_player_dodged = 1 which a guest has to keep as its own and a journal entry 0x4e434f50
 nobody has) and send nightcoop_mac_probe_fact + 1 as a delta evry 5 s for 10 min. on the joiner it sends QuestWant for
 world 1 at step 10 the same world again at step 20 (the host has to ignore that) and world 2 at step 110 and prints the
 hosts QuestFacts / QuestJournal batches with a few well known facts out of the snapshots
 PROBE_LOOT=<item record name or 0x tdbid> (1 = Items.money) works as host and as joiner and prints evry loot message (38-44).
 it answers debits and grants of item gives like a player with endless pockets (the host one keeps a small ledger) gives the
 game 5 of the record after ~4 s shares a bag drop with 3 of it 1 m next to the game player from ~5 s on (LootState evry 5 s
 a LootTake gets granted and the bag shrinks PROBE_LOOT_DENY=1 denies them) and takes 1 of the first item of the first open
 drop the game shares printing the LootTakeResult. the host stays up 10 min with it
 PROBE_CARRY=1 (with PROBE_NPC) makes the joiner pick its npc up at step 30 (PlayerCarry pickup then carry evry second) and drop
 it 1 m ahead at step 100 (drop sent 3 times). PROBE_CARRY=0x<entity id> carries that entity of the hosts world instead.
 the joiner prints evry PlayerCarry the host sends
 PROBE_BUILD=<value> makes the joiner send BuildStats with all 11 stats at that value evry ~5 s and it prints the hosts build
 HUB looks like 127.0.0.1:47900#FINGERPRINT
*/
#include "net/DoorMessages.hpp"
#include "net/DeviceMessages.hpp"
#include "net/DialogMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/LootMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/RosterMessages.hpp"
#include "net/VehicleMessages.hpp"
#include "net/WorldMessages.hpp"
#include "net/NpcMessages.hpp"
#include "net/CombatMessages.hpp"
#include "net/QuestMessages.hpp"
#include "net/PlayerExtrasMessages.hpp"
#include "net/GrenadeMessages.hpp"

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

struct ProbeDialogPlan
{
    bool Enabled = false;
    int8_t Option = 0;
    uint8_t Rows = 3;
};

ProbeDialogPlan ReadProbeDialogPlan()
{
    ProbeDialogPlan plan;
    const char* setting = std::getenv("PROBE_DIALOG");
    if (!setting)
    {
        return plan;
    }
    plan.Enabled = true;
    char* rest = nullptr;
    plan.Option = static_cast<int8_t>(std::clamp<long>(std::strtol(setting, &rest, 0), 0, 15));
    if (rest && *rest == ',')
    {
        plan.Rows = static_cast<uint8_t>(std::clamp<long>(std::strtol(rest + 1, nullptr, 0), 1, 16));
    }
    plan.Option = std::min<int8_t>(plan.Option, static_cast<int8_t>(plan.Rows - 1));
    return plan;
}

void PrintBallot(uint32_t fromPeer, const nightcoop_mac::net::DialogBallot& ballot)
{
    std::printf("probe: peer %u opened vote #%u '%s' flags %#x, %zu voter(s):", fromPeer, ballot.VoteId, ballot.Title.c_str(), ballot.Flags,
                ballot.Voters.size());
    for (size_t index = 0; index < ballot.Options.size(); ++index)
    {
        std::printf(" [%zu '%s' type %#x]", index, ballot.Options[index].Text.c_str(), ballot.Options[index].ChoiceType);
    }
    std::printf("\n");
    std::fflush(stdout);
}

void PrintTally(uint32_t fromPeer, const nightcoop_mac::net::DialogTally& tally, uint64_t sessionClockMs)
{
    std::printf("probe: peer %u tally of vote #%u: state %u option %d needed %u, votes", fromPeer, tally.VoteId, tally.State, tally.Option, tally.Needed);
    for (const auto& vote : tally.Votes)
    {
        std::printf(" [peer %u -> %d]", vote.Peer, vote.Option);
    }
    std::printf(", %zu voter(s)", tally.Voters.size());
    if (tally.SharedTime)
    {
        std::printf(", goes at %llu (in %lld ms)", static_cast<unsigned long long>(tally.SharedTime),
                    static_cast<long long>(tally.SharedTime) - static_cast<long long>(sessionClockMs));
    }
    std::printf("\n");
    std::fflush(stdout);
}

bool PrintSceneSkip(uint32_t fromPeer, const std::vector<uint8_t>& message)
{
    nightcoop_mac::net::SceneLineSkip skip;
    if (!nightcoop_mac::net::ReadSceneLineSkip(message, skip))
    {
        return false;
    }
    std::printf("probe: peer %u says peer %u %s (scene %#llx, %u ms, flags %#x)\n", fromPeer, skip.Peer,
                skip.Flags & nightcoop_mac::net::kSkipPress ? "started a scene fast forward" : "ended its scene fast forward",
                static_cast<unsigned long long>(skip.SceneId), skip.Milliseconds, skip.Flags);
    std::fflush(stdout);
    return true;
}

void SendProbeSkip(nightcoop_mac::net::RelaySession& session, bool press, uint32_t milliseconds)
{
    nightcoop_mac::net::SceneLineSkip skip;
    skip.Peer = session.GetStatus().MyPeerNumber;
    skip.SceneId = 1;
    skip.Milliseconds = milliseconds;
    skip.SharedTime = std::max<uint64_t>(session.SessionClockMs(), 1);
    skip.Flags = press ? nightcoop_mac::net::kSkipPress : 0;
    skip.Places.push_back({1, 0x1234, 1});
    session.SendGameMessage(nightcoop_mac::net::WriteSceneLineSkip(skip));
    std::printf("probe: sent a scene fast forward %s\n", press ? "press" : "end");
}

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

uint64_t RecordId(const char* name)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t length = 0;
    for (const char* letter = name; *letter; ++letter, ++length)
    {
        crc ^= static_cast<uint8_t>(*letter);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return static_cast<uint64_t>(~crc) | (static_cast<uint64_t>(length) << 32);
}

int RunHost(nightcoop_mac::net::RelaySession& session, int statesToWaitFor)
{
    bool printedCode = false;
    int statesSeen = 0;
    const char* clock = std::getenv("PROBE_CLOCK");
    const uint32_t clockSeconds = clock ? static_cast<uint32_t>(std::strtoul(clock, nullptr, 0)) : 0;
    auto nextClock = std::chrono::steady_clock::now();
    const char* hostSave = std::getenv("PROBE_HOST_SAVE");
    const char* doorKeyText = std::getenv("PROBE_DOOR");
    const uint64_t probeDoorKey = doorKeyText ? std::strtoull(doorKeyText, nullptr, 0) : 0;
    auto nextDoorFlip = std::chrono::steady_clock::now() + 8s;
    bool probeDoorOpen = false;
    uint64_t probeDoorVersion = 0;
    const ProbeDialogPlan dialogPlan = ReadProbeDialogPlan();
    auto nextBallot = std::chrono::steady_clock::now() + 5s;
    auto nextDialogLine = std::chrono::steady_clock::now() + 6s;
    auto ballotDeadline = std::chrono::steady_clock::time_point::max();
    nightcoop_mac::net::DialogBallot openBallot;
    std::map<uint32_t, int8_t> ballotVotes;
    uint32_t ballotsOpened = 0;
    uint32_t dialogLinesSent = 0;
    bool voteModeSent = false;
    auto decideVote = [&](int8_t option, uint8_t state) {
        nightcoop_mac::net::DialogTally tally;
        tally.VoteId = openBallot.VoteId;
        tally.State = state;
        tally.Option = option;
        tally.Needed = static_cast<uint8_t>(openBallot.Voters.size() / 2 + 1);
        for (const auto& [peer, vote] : ballotVotes)
        {
            tally.Votes.push_back({peer, vote});
        }
        tally.Voters = openBallot.Voters;
        tally.SharedTime = session.SessionClockMs() + 1500;
        session.SendGameMessage(nightcoop_mac::net::WriteDialogTally(tally));
        std::printf("probe: vote #%u decided: option %d '%s' at %llu%s\n", openBallot.VoteId, option, openBallot.Options[static_cast<size_t>(option)].Text.c_str(),
                    static_cast<unsigned long long>(tally.SharedTime), state == nightcoop_mac::net::kTallyDecidedByTime ? " (by time)" : "");
        std::fflush(stdout);
        ballotDeadline = std::chrono::steady_clock::time_point::max();
    };
    const bool probeQuests = std::getenv("PROBE_QUESTS") != nullptr;
    auto nextQuestDelta = std::chrono::steady_clock::now();
    std::set<uint32_t> questPeers;
    uint32_t questSnapshots = 0;
    int32_t probeFactValue = 1;
    LootProbe loot(session);
    bool guestKnown = false;
    float guestX = 0;
    float guestY = 0;
    float guestZ = 0;
    const char* probeReward = std::getenv("PROBE_REWARD");
    const char* probeCall = std::getenv("PROBE_CALL");
    auto callStartedAt = std::chrono::steady_clock::time_point::max();
    int callPhaseSent = 0;
    uint32_t callCommits = 0;
    const bool staysUp = clock || hostSave || std::getenv("PROBE_DEAD") || loot.Enabled() || probeDoorKey || dialogPlan.Enabled || probeQuests || probeReward || probeCall;
    auto deadline = std::chrono::steady_clock::now() + (staysUp ? 600s : ProbeTimeout);
    auto nextRoster = std::chrono::steady_clock::now();
    auto nextReward = std::chrono::steady_clock::now() + 20s;
    uint32_t rewardSerial = 0;
    std::vector<uint8_t> saveBytes;
    std::vector<uint8_t> metadataBytes;
    size_t saveSent = 0;
    size_t metadataSent = 0;
    uint32_t servingPeer = 0;
    uint32_t servingTransfer = 1;
    const char* reloadSave = std::getenv("PROBE_RELOAD");
    const bool probeDead = std::getenv("PROBE_DEAD") != nullptr;
    auto deadAfter = std::chrono::steady_clock::time_point::max();
    bool deadSent = false;
    auto reloadAt = std::chrono::steady_clock::time_point::max();
    uint32_t reloadPeer = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (reloadSave && reloadPeer && std::chrono::steady_clock::now() >= reloadAt)
        {
            reloadAt = std::chrono::steady_clock::time_point::max();
            std::printf("probe: we load another save (%s), peer %u gets it\n", reloadSave, reloadPeer);
            session.SendGameMessage(nightcoop_mac::net::WriteWorldReload({1, 0, "probe reload"}), static_cast<int32_t>(reloadPeer));
            std::ifstream save(std::string(reloadSave) + "/sav.dat", std::ios::binary);
            saveBytes.assign(std::istreambuf_iterator<char>(save), std::istreambuf_iterator<char>());
            std::ifstream metadata(std::string(reloadSave) + "/metadata.9.json", std::ios::binary);
            metadataBytes.assign(std::istreambuf_iterator<char>(metadata), std::istreambuf_iterator<char>());
            saveSent = 0;
            metadataSent = 0;
            servingPeer = reloadPeer;
            ++servingTransfer;
            session.SendGameMessage(nightcoop_mac::net::WriteWorldStatus({3, servingTransfer, "probe-reload-save"}), static_cast<int32_t>(servingPeer));
            reloadSave = nullptr;
            deadline = std::max(deadline, std::chrono::steady_clock::now() + 400s);
        }
        if (probeCall && session.GetStatus().ConnectedPeers > 0)
        {
            const auto now = std::chrono::steady_clock::now();
            if (callStartedAt == std::chrono::steady_clock::time_point::max())
            {
                callStartedAt = now + 25s;
            }
            const int wantedPhase = now >= callStartedAt + 20s ? 3 : now >= callStartedAt + 6s ? 2 : now >= callStartedAt ? 1 : 0;
            if (wantedPhase > callPhaseSent)
            {
                callPhaseSent = wantedPhase;
                nightcoop_mac::net::PhoneSync phone;
                phone.Kind = nightcoop_mac::net::kPhoneCommitted;
                phone.Action = nightcoop_mac::net::kPhoneActionCall;
                phone.CommitNumber = ++callCommits;
                phone.CallId = 1;
                phone.HostClockMs = session.SessionClockMs();
                phone.Contact = probeCall;
                phone.CallPhase = static_cast<uint8_t>(wantedPhase);
                phone.CallMode = 2;
                phone.Flags = nightcoop_mac::net::kPhoneRejectable | nightcoop_mac::net::kPhoneShowAvatar;
                session.SendGameMessage(nightcoop_mac::net::WritePhoneSync(phone));
                std::printf("probe: call with %s goes to phase %d\n", probeCall, wantedPhase);
                std::fflush(stdout);
            }
        }
        if (probeReward && session.GetStatus().ConnectedPeers > 0 && std::chrono::steady_clock::now() >= nextReward)
        {
            nextReward = std::chrono::steady_clock::now() + 10s;
            nightcoop_mac::net::RewardWord word {nightcoop_mac::net::kRewardQuest, RecordId(probeReward), ++rewardSerial, 0};
            session.SendGameMessage(nightcoop_mac::net::WriteRewardWord(word));
            std::printf("probe: sent our quest reward %s (#%u)\n", probeReward, rewardSerial);
            std::fflush(stdout);
        }
        if ((hostSave || probeQuests) && session.GetStatus().ConnectedPeers > 0 && std::chrono::steady_clock::now() >= nextRoster)
        {
            nextRoster = std::chrono::steady_clock::now() + 2s;
            nightcoop_mac::net::SessionRoster roster;
            roster.Revision = 1;
            roster.HostState = nightcoop_mac::net::kHostStateInWorld;
            roster.Entries.push_back({0, "probe host", nightcoop_mac::net::kWhereInWorld, 0, nightcoop_mac::net::kRoleHost, 0x80, 0, 0, "", {}, ""});
            session.SendGameMessage(nightcoop_mac::net::WriteSessionRoster(roster));
        }
        if (probeDoorKey && session.GetStatus().ConnectedPeers > 0 && std::chrono::steady_clock::now() >= nextDoorFlip)
        {
            nextDoorFlip = std::chrono::steady_clock::now() + 8s;
            probeDoorOpen = !probeDoorOpen;
            probeDoorVersion = std::max(probeDoorVersion + 1, session.SessionClockMs());
            nightcoop_mac::net::DoorSync flip;
            flip.Kind = nightcoop_mac::net::kDoorShared;
            flip.DoorKey = probeDoorKey;
            flip.State = probeDoorOpen ? nightcoop_mac::net::kDoorOpen : 0;
            flip.Changed = nightcoop_mac::net::kDoorOpen;
            flip.Version = probeDoorVersion;
            session.SendGameMessage(nightcoop_mac::net::WriteDoorSync(flip));
            std::printf("probe: door %#llx goes %s for evryone (version %llu)\n", static_cast<unsigned long long>(probeDoorKey),
                        probeDoorOpen ? "open" : "closed", static_cast<unsigned long long>(probeDoorVersion));
            std::fflush(stdout);
        }
        if (dialogPlan.Enabled && session.GetStatus().ConnectedPeers > 0)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!voteModeSent)
            {
                voteModeSent = true;
                session.SendGameMessage(nightcoop_mac::net::WriteVoteModeWord({1, nightcoop_mac::net::kVoteModeMajority, 0}));
            }
            if (now >= nextBallot)
            {
                nextBallot = now + 30s;
                ballotDeadline = now + 20s;
                ballotVotes.clear();
                openBallot = {};
                openBallot.VoteId = ++ballotsOpened;
                openBallot.Title = "probe vote";
                for (uint8_t row = 0; row < dialogPlan.Rows; ++row)
                {
                    openBallot.Options.push_back({"probe option " + std::to_string(row + 1), 0});
                }
                openBallot.Voters = {0, 1};
                session.SendGameMessage(nightcoop_mac::net::WriteDialogBallot(openBallot));
                nightcoop_mac::net::DialogTally openTally;
                openTally.VoteId = openBallot.VoteId;
                openTally.Needed = 2;
                openTally.Voters = openBallot.Voters;
                session.SendGameMessage(nightcoop_mac::net::WriteDialogTally(openTally));
                std::printf("probe: opened vote #%u with %u option(s), the guest has 20 s to vote\n", openBallot.VoteId, dialogPlan.Rows);
                std::fflush(stdout);
            }
            if (now >= ballotDeadline)
            {
                decideVote(dialogPlan.Option, nightcoop_mac::net::kTallyDecidedByTime);
            }
            if (now >= nextDialogLine)
            {
                nextDialogLine = now + 6s;
                nightcoop_mac::net::DialogLine line;
                line.LineId = 0x5000 + ++dialogLinesSent;
                line.LineType = 1;
                line.Seconds = 3.0f;
                line.Speaker = "Probe Host";
                line.Text = "probe line " + std::to_string(dialogLinesSent);
                line.Language = "en-us";
                session.SendGameMessage(nightcoop_mac::net::WriteDialogLine(line));
            }
        }
        for (const auto& message : session.TakeGameMessages())
        {
            nightcoop_mac::net::DialogVote dialogVote;
            if (dialogPlan.Enabled && nightcoop_mac::net::ReadDialogVote(message.Message, dialogVote))
            {
                std::printf("probe: peer %u votes %d in vote #%u\n", message.FromPeer, dialogVote.Option, dialogVote.VoteId);
                std::fflush(stdout);
                if (dialogVote.VoteId == openBallot.VoteId && ballotDeadline != std::chrono::steady_clock::time_point::max() && dialogVote.Option >= 0 &&
                    static_cast<size_t>(dialogVote.Option) < openBallot.Options.size())
                {
                    ballotVotes[message.FromPeer] = dialogVote.Option;
                    ballotVotes[0] = dialogVote.Option;
                    decideVote(dialogVote.Option, nightcoop_mac::net::kTallyDecided);
                }
                continue;
            }
            if (dialogPlan.Enabled && PrintSceneSkip(message.FromPeer, message.Message))
            {
                continue;
            }
            nightcoop_mac::net::WorldWant want;
            nightcoop_mac::net::WorldStatus worldStatus;
            nightcoop_mac::net::DoorSync guestDoor;
            if (probeDoorKey && nightcoop_mac::net::ReadDoorSync(message.Message, guestDoor) && guestDoor.Kind == nightcoop_mac::net::kDoorTell)
            {
                std::printf("probe: peer %u says door %#llx is state %u (changed %u cause %u quest ops %#x side %u #%u from version %llu) at %.1f %.1f %.1f, "
                            "taking it\n",
                            message.FromPeer, static_cast<unsigned long long>(guestDoor.DoorKey), guestDoor.State, guestDoor.Changed, guestDoor.Cause,
                            guestDoor.QuestOps, guestDoor.Side, guestDoor.RequestNumber, static_cast<unsigned long long>(guestDoor.Version),
                            guestDoor.Position[0], guestDoor.Position[1], guestDoor.Position[2]);
                std::fflush(stdout);
                probeDoorVersion = std::max(probeDoorVersion + 1, session.SessionClockMs());
                nightcoop_mac::net::DoorSync taken = guestDoor;
                taken.Kind = nightcoop_mac::net::kDoorShared;
                taken.Peer = message.FromPeer;
                taken.Version = probeDoorVersion;
                session.SendGameMessage(nightcoop_mac::net::WriteDoorSync(taken), static_cast<int32_t>(message.FromPeer));
                if (guestDoor.DoorKey == probeDoorKey)
                {
                    probeDoorOpen = (guestDoor.State & nightcoop_mac::net::kDoorOpen) != 0;
                }
            }
            uint32_t questWorld = 0;
            if (probeQuests && nightcoop_mac::net::ReadQuestWant(message.Message, questWorld))
            {
                std::printf("probe: peer %u loaded its world #%u and wants our facts and journal, snapshot #%u goes\n", message.FromPeer, questWorld,
                            questSnapshots + 1);
                SendProbeSnapshot(session, message.FromPeer, ++questSnapshots, probeFactValue);
                questPeers.insert(message.FromPeer);
                std::fflush(stdout);
            }
            nightcoop_mac::net::NpcSpawn guestNpc;
            nightcoop_mac::net::NpcDespawn guestNpcGone;
            std::vector<nightcoop_mac::net::DeadPerson> guestDead;
            uint64_t guestClock = 0;
            if (probeDead && !deadSent && nightcoop_mac::net::ReadNpcSpawn(message.Message, guestNpc) && !guestNpc.Crowd)
            {
                if (deadAfter == std::chrono::steady_clock::time_point::max())
                {
                    deadAfter = std::chrono::steady_clock::now() + 40s;
                }
                if (std::chrono::steady_clock::now() >= deadAfter)
                {
                    deadSent = true;
                    nightcoop_mac::net::DeadPerson person {guestNpc.OwnerEntity, guestNpc.Record, guestNpc.MatchKeyA, guestNpc.MatchKeyB,
                                                           {guestNpc.Position[0], guestNpc.Position[1], guestNpc.Position[2]}, 0};
                    session.SendGameMessage(nightcoop_mac::net::WriteNpcDead(false, {person}), static_cast<int32_t>(message.FromPeer));
                    std::printf("probe: says npc %#010x of peer %u (entity %#llx record %#llx) died in our world\n", guestNpc.NetId, message.FromPeer,
                                static_cast<unsigned long long>(guestNpc.OwnerEntity), static_cast<unsigned long long>(guestNpc.Record));
                }
                continue;
            }
            if (nightcoop_mac::net::ReadNpcDespawn(message.Message, guestNpcGone) && guestNpcGone.Reason == 3)
            {
                std::printf("probe: npc %#010x of peer %u died (killer %#x)\n", guestNpcGone.NetId, message.FromPeer, guestNpcGone.Killer);
                continue;
            }
            if (nightcoop_mac::net::ReadNpcDefeat(message.Message, guestClock, guestDead))
            {
                std::printf("probe: peer %u says %zu of its npcs died (NpcDefeat)\n", message.FromPeer, guestDead.size());
                continue;
            }
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
                if (worldStatus.Code == 7 && reloadSave && !reloadPeer)
                {
                    reloadPeer = message.FromPeer;
                    reloadAt = std::chrono::steady_clock::now() + 60s;
                    std::printf("probe: the guest is in our world, we load another save in 60 s\n");
                }
                if (worldStatus.Code == 7 && std::getenv("PROBE_LEAVE_AFTER_ARRIVAL") && !reloadSave)
                {
                    deadline = std::min(deadline, std::chrono::steady_clock::now() + 20s);
                    std::printf("probe: the guest is in our world, we leave in 20 s\n");
                }
            }
        }
        for (int chunkCount = 0; servingPeer && chunkCount < 8 && (metadataSent < metadataBytes.size() || saveSent < saveBytes.size()); ++chunkCount)
        {
            const bool metadataFirst = metadataSent < metadataBytes.size();
            const auto& bytes = metadataFirst ? metadataBytes : saveBytes;
            size_t& sent = metadataFirst ? metadataSent : saveSent;
            nightcoop_mac::net::WorldFile chunk;
            chunk.TransferId = servingTransfer;
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
    uint32_t hostNpcSnapshots = 0;
    uint32_t hostNpcsArmed = 0;
    uint32_t hostNpcsStated = 0;
    uint32_t hostNpcAttitudesToUs[3] = {0, 0, 0};
    std::set<uint8_t> hostNpcHighLevels;
    std::set<uint32_t> hostNpcsArmedSeen;
    bool probeNpcWanted = false;
    float probeNpcHealth = 100.0f;
    bool probeNpcDead = false;
    uint32_t claimsAnswered = 0;
    uint32_t probeClaimNumber = 0;
    uint32_t hostClaimResults = 0;
    uint32_t firstHostNpc = 0;
    bool defeatSent = false;
    std::set<uint32_t> defeatedNpcs;
    std::map<uint32_t, float> hostNpcHealth;
    nightcoop_mac::net::NpcSpawn mirroredNpc;
    bool mirrorChosen = false;
    bool mirrorReleased = false;
    int mirrorStartStep = 0;
    nightcoop_mac::net::NpcHandoff takenNpc;
    bool tookNpc = false;
    bool takenReleased = false;
    int takenAtStep = 0;
    std::vector<std::pair<int, nightcoop_mac::net::DeviceSync>> devicesToEcho;
    uint16_t deviceRequestNumber = 0;
    uint32_t probeNpcSequence = 0;
    uint32_t rosterRevision = 0xFFFFFFFF;
    const bool probeQuests = std::getenv("PROBE_QUESTS") != nullptr;
    uint32_t questFactBatches = 0;
    uint32_t questJournalBatches = 0;
    uint32_t hostBuildRevision = 0xFFFFFFFF;
    uint32_t hostShots = 0;
    uint16_t hostWeaponFlags = 0;
    uint8_t hostWeaponState = 0;
    const bool probeRevives = std::getenv("PROBE_REVIVE") != nullptr || std::getenv("PROBE_RIDE") != nullptr;
    const char* doorKeyText = std::getenv("PROBE_DOOR");
    const uint64_t probeDoorKey = doorKeyText ? std::strtoull(doorKeyText, nullptr, 0) : 0;
    uint64_t probeDoorVersionSeen = 0;
    uint16_t probeDoorRequest = static_cast<uint16_t>(std::chrono::steady_clock::now().time_since_epoch().count() | 1);
    if (probeDoorKey)
    {
        std::printf("probe: standing at the door %#llx\n", static_cast<unsigned long long>(probeDoorKey));
    }
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
    const ProbeDialogPlan dialogPlan = ReadProbeDialogPlan();
    if (dialogPlan.Enabled)
    {
        WalkSteps = std::max(WalkSteps, 3000);
        std::printf("probe: voting for option %d in the hosts choice votes\n", dialogPlan.Option);
    }
    nightcoop_mac::net::DialogBallot hostBallot;
    int voteAtStep = -1;
    for (int step = 0; step < WalkSteps; ++step)
    {
        if (dialogPlan.Enabled && step == voteAtStep && !hostBallot.Options.empty())
        {
            const int8_t option = std::min<int8_t>(dialogPlan.Option, static_cast<int8_t>(hostBallot.Options.size() - 1));
            const uint32_t item = static_cast<size_t>(option) < hostBallot.RowIds.size() ? hostBallot.RowIds[static_cast<size_t>(option)].Item : 0;
            session.SendGameMessage(nightcoop_mac::net::WriteDialogVote({hostBallot.VoteId, option, item}), 0);
            std::printf("probe: voted for option %d '%s' in vote #%u\n", option, hostBallot.Options[static_cast<size_t>(option)].Text.c_str(), hostBallot.VoteId);
        }
        if (dialogPlan.Enabled && (step == 100 || step == 140))
        {
            SendProbeSkip(session, step == 100, step == 140 ? 3200 : 0);
        }
        nightcoop_mac::net::PlayerState state;
        const int stepInLeg = step % 300;
        state.PositionX = WalkStartX + (probeRevives || probeDoorKey ? 0.0f : (stepInLeg < 150 ? stepInLeg : 300 - stepInLeg) * 0.05f);
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
        if (std::getenv("PROBE_CARHIT") && std::getenv("PROBE_VEHICLE") && step == 200)
        {
            nightcoop_mac::net::VehicleHit carHit {probeCarNetId, 0, session.SessionClockMs(), {1.0f, 0.0f, 0.0f}, {400.0f, 0.0f, 80.0f}, 9.0f};
            session.SendGameMessage(nightcoop_mac::net::WriteVehicleHit(carHit), 0);
            std::printf("probe: our car %#010x hit the hosts player (VehicleHit 9 m/s)\n", probeCarNetId);
        }
        if (const char* effectText = std::getenv("PROBE_FX"); effectText && step == 120)
        {
            nightcoop_mac::net::PlayerEffect effect {1, std::strtoull(effectText, nullptr, 0), {state.PositionX, state.PositionY, state.PositionZ + 1.0f}, {0, 0, 0, 1}, 0};
            session.SendGameMessage(nightcoop_mac::net::WritePlayerFx({effect}));
            std::printf("probe: shows effect %#llx where the joiner stands (PlayerFx)\n", static_cast<unsigned long long>(effect.Resource));
        }
        if (std::getenv("PROBE_PUSH") && step == 110)
        {
            nightcoop_mac::net::Blast blast {{state.PositionX, state.PositionY, state.PositionZ}, 6.0f, 0x5EED0001ULL};
            session.SendGameMessage(nightcoop_mac::net::WritePlayerBlast({blast}));
            std::printf("probe: our grenade went off at %.1f %.1f %.1f (PlayerBlast radius 6)\n", state.PositionX, state.PositionY, state.PositionZ);
        }
        if (const char* hitRecord = std::getenv("PROBE_HIT"); hitRecord && std::getenv("PROBE_NPC") && !probeNpcDead && step > 60 && step % 40 == 0)
        {
            nightcoop_mac::net::PlayerHit hit;
            hit.HitNumber = static_cast<uint32_t>(step);
            hit.AttackerNetId = (session.GetStatus().MyPeerNumber << 24) | 7;
            hit.SessionTimeMs = session.SessionClockMs();
            hit.Details.AttackRecord = RecordId(hitRecord);
            hit.Details.AttackType = 10;
            hit.Details.Offset[2] = 1.2f;
            hit.Details.Direction[1] = 1.0f;
            hit.Details.Flags = {26, 29};
            session.SendGameMessage(nightcoop_mac::net::WritePlayerHit(hit), 0);
            std::printf("probe: told the host our npc hit their body (%s = %#llx)\n", hitRecord, static_cast<unsigned long long>(hit.Details.AttackRecord));
        }
        if (std::getenv("PROBE_OFFER") && std::getenv("PROBE_NPC") && !probeNpcDead && step == 120)
        {
            nightcoop_mac::net::NpcHandoff offer;
            offer.Spawn.NetId = (session.GetStatus().MyPeerNumber << 24) | 7;
            offer.Spawn.Record = std::strtoull(std::getenv("PROBE_NPC"), nullptr, 0);
            offer.Spawn.Position[0] = state.PositionX;
            offer.Spawn.Position[1] = state.PositionY + 2.0f;
            offer.Spawn.Position[2] = state.PositionZ;
            offer.Snapshot.NetId = offer.Spawn.NetId;
            std::memcpy(offer.Snapshot.Position, offer.Spawn.Position, sizeof(offer.Snapshot.Position));
            offer.Snapshot.OwnerClockMs = session.SessionClockMs();
            session.SendGameMessage(nightcoop_mac::net::WriteNpcHandoff(offer), 0);
            probeNpcDead = true;
            std::printf("probe: offered our npc %#010x to the host\n", offer.Spawn.NetId);
        }
        if (const char* blastKey = std::getenv("PROBE_BLAST"); blastKey && step == 100)
        {
            nightcoop_mac::net::DeviceBlast blast;
            blast.Kind = nightcoop_mac::net::kBlastLive;
            nightcoop_mac::net::BlastedDevice device;
            device.DeviceKey = std::strtoull(blastKey, nullptr, 0);
            device.Position[0] = std::getenv("PROBE_BLAST_X") ? static_cast<float>(std::atof(std::getenv("PROBE_BLAST_X"))) : state.PositionX;
            device.Position[1] = std::getenv("PROBE_BLAST_Y") ? static_cast<float>(std::atof(std::getenv("PROBE_BLAST_Y"))) : state.PositionY;
            device.Position[2] = std::getenv("PROBE_BLAST_Z") ? static_cast<float>(std::atof(std::getenv("PROBE_BLAST_Z"))) : state.PositionZ;
            device.ByOurPlayer = 1;
            blast.Devices.push_back(device);
            session.SendGameMessage(nightcoop_mac::net::WriteDeviceBlast(blast));
            std::printf("probe: told evryone barrel %#llx blew\n", static_cast<unsigned long long>(device.DeviceKey));
        }
        for (auto echo = devicesToEcho.begin(); echo != devicesToEcho.end();)
        {
            if (step < echo->first)
            {
                ++echo;
                continue;
            }
            nightcoop_mac::net::DeviceSync request = echo->second;
            request.Kind = nightcoop_mac::net::kDeviceRequest;
            request.Peer = session.GetStatus().MyPeerNumber;
            request.RequestNumber = ++deviceRequestNumber;
            std::swap(request.StateBefore, request.StateAfter);
            session.SendGameMessage(nightcoop_mac::net::WriteDeviceSync(request), 0);
            std::printf("probe: asked the host to do it again on device %#llx (#%u)\n", static_cast<unsigned long long>(request.EntityHash), request.RequestNumber);
            echo = devicesToEcho.erase(echo);
        }
        if (tookNpc && !takenReleased)
        {
            nightcoop_mac::net::NpcSpawn takenSpawn = takenNpc.Spawn;
            if ((step - takenAtStep) % 8 == 0)
            {
                session.SendGameMessage(nightcoop_mac::net::WriteNpcSpawn(takenSpawn));
            }
            nightcoop_mac::net::NpcSnapshot takenState = takenNpc.Snapshot;
            takenState.OwnerClockMs = session.SessionClockMs();
            session.SendGameMessage(nightcoop_mac::net::WriteNpcSnapshot(takenState), nightcoop_mac::net::kEveryPeer, false);
            if (step - takenAtStep >= 100)
            {
                takenReleased = true;
                session.SendGameMessage(nightcoop_mac::net::WriteNpcDespawn({takenSpawn.NetId, 1, 0xFFFFFFFF}));
                std::printf("probe: let go of the npc the host handed us\n");
            }
        }
        if (std::getenv("PROBE_MIRROR") && mirrorChosen && !mirrorReleased)
        {
            nightcoop_mac::net::NpcSpawn mirrorSpawn = mirroredNpc;
            mirrorSpawn.NetId = (session.GetStatus().MyPeerNumber << 24) | 0x100;
            if ((step - mirrorStartStep) % 8 == 0)
            {
                session.SendGameMessage(nightcoop_mac::net::WriteNpcSpawn(mirrorSpawn));
            }
            nightcoop_mac::net::NpcSnapshot mirrorState;
            mirrorState.NetId = mirrorSpawn.NetId;
            std::memcpy(mirrorState.Position, mirroredNpc.Position, sizeof(mirrorState.Position));
            mirrorState.Yaw = mirroredNpc.Yaw;
            mirrorState.OwnerClockMs = session.SessionClockMs();
            session.SendGameMessage(nightcoop_mac::net::WriteNpcSnapshot(mirrorState), nightcoop_mac::net::kEveryPeer, false);
            if (step - mirrorStartStep >= 120)
            {
                mirrorReleased = true;
                session.SendGameMessage(nightcoop_mac::net::WriteNpcDespawn({mirrorSpawn.NetId, 1, 0xFFFFFFFF}));
                std::printf("probe: let go of our mirror of the hosts npc\n");
            }
        }
        if (std::getenv("PROBE_HACK") && firstHostNpc && step % 50 == 0)
        {
            nightcoop_mac::net::HackSync hack;
            hack.Stage = nightcoop_mac::net::kHackStageCompletion;
            hack.Serial = static_cast<uint32_t>(step);
            hack.HackerPeer = session.GetStatus().MyPeerNumber;
            hack.NetId = firstHostNpc;
            hack.ActionRecord = RecordId("QuickHack.BlindLvl1Hack");
            session.SendGameMessage(nightcoop_mac::net::WriteHackSync(hack), 0);
            std::printf("probe: quickhacked the hosts npc %#010x (blind)\n", firstHostNpc);
        }
        if (std::getenv("PROBE_CLAIM") && firstHostNpc && step % 25 == 0)
        {
            nightcoop_mac::net::HitClaim claim;
            claim.ClaimNumber = ++probeClaimNumber;
            claim.NetId = firstHostNpc;
            claim.HealthPercent = 35.0f;
            claim.ClaimTimeMs = session.SessionClockMs();
            claim.Details.AttackType = 10;
            claim.Details.Flags = {26};
            session.SendGameMessage(nightcoop_mac::net::WriteHitClaim(claim), 0);
        }
        if (const char* npcRecord = std::getenv("PROBE_NPC"); npcRecord && !probeNpcDead)
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
            npcState.Health = std::min(probeNpcHealth, step < WalkSteps / 2 ? 100.0f : 60.0f);
            npcState.States[0] = 3;
            npcState.States[1] = step < WalkSteps / 2 ? 6 : 0;
            npcState.States[2] = step < WalkSteps / 2 ? 5 : 2;
            npcState.PartnerAttitudes.push_back({0, static_cast<uint8_t>(step < WalkSteps / 2 ? 1 : 2)});
            if (const char* npcWeapon = std::getenv("PROBE_NPC_WEAPON"))
            {
                npcState.RightHandItem = step < WalkSteps / 2 ? 0 : std::strtoull(npcWeapon, nullptr, 0);
            }
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
        if (const char* carried = std::getenv("PROBE_CARRY"); carried && step >= 30 && step <= 109)
        {
            const uint64_t carriedEntity = std::strtoull(carried, nullptr, 0);
            nightcoop_mac::net::PlayerCarry carry;
            carry.CarryNumber = 1;
            carry.SenderClockMs = session.SessionClockMs();
            carry.NetId = carriedEntity > 1 ? 0 : (session.GetStatus().MyPeerNumber << 24) | 7;
            carry.EntityHash = carriedEntity > 1 ? carriedEntity : 0;
            carry.Position[0] = state.PositionX;
            carry.Position[1] = state.PositionY;
            carry.Position[2] = state.PositionZ + 1.5f;
            const bool picking = step == 30;
            const bool holding = step > 30 && step < 100 && step % 12 == 0;
            const bool dropping = step == 100 || step == 103 || step == 109;
            if (picking || holding)
            {
                carry.Kind = picking ? nightcoop_mac::net::kCarryPickup : nightcoop_mac::net::kCarryHolding;
                carry.Flags = nightcoop_mac::net::kCarryActive;
            }
            else if (dropping)
            {
                carry.Kind = nightcoop_mac::net::kCarryDrop;
                carry.Position[1] = state.PositionY + 1.0f;
                carry.Position[2] = state.PositionZ;
            }
            if (picking || holding || dropping)
            {
                session.SendGameMessage(nightcoop_mac::net::WritePlayerCarry(carry));
                if (picking || step == 100)
                {
                    std::printf("probe: %s %s %#llx\n", picking ? "picked up" : "dropped", carry.NetId ? "our npc" : "entity",
                                static_cast<unsigned long long>(carry.NetId ? carry.NetId : carry.EntityHash));
                }
            }
        }
        if (const char* buildValue = std::getenv("PROBE_BUILD"); buildValue && step % 60 == 0)
        {
            nightcoop_mac::net::BuildStats build;
            build.Revision = 1;
            for (uint8_t index = 0; index < nightcoop_mac::net::kBuildStatCount; ++index)
            {
                build.Stats.push_back({index, static_cast<float>(std::atof(buildValue))});
            }
            session.SendGameMessage(nightcoop_mac::net::WriteBuildStats(build));
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
        const int doorStep = step % 200;
        if (probeDoorKey && (doorStep == 30 || doorStep == 80 || doorStep == 120 || doorStep == 160))
        {
            nightcoop_mac::net::DoorSync tell;
            tell.Kind = nightcoop_mac::net::kDoorTell;
            tell.Peer = session.GetStatus().MyPeerNumber;
            tell.DoorKey = probeDoorKey;
            tell.Version = probeDoorVersionSeen;
            if (++probeDoorRequest == 0)
            {
                ++probeDoorRequest;
            }
            tell.RequestNumber = probeDoorRequest;
            tell.Position[0] = state.PositionX;
            tell.Position[1] = state.PositionY;
            tell.Position[2] = state.PositionZ;
            const char* what = "opened it";
            tell.Changed = nightcoop_mac::net::kDoorOpen;
            if (doorStep == 30)
            {
                tell.State = nightcoop_mac::net::kDoorOpen;
            }
            else if (doorStep == 80)
            {
                tell.Cause = nightcoop_mac::net::kDoorCauseClosedByItself;
                what = "it closed by itself";
            }
            else if (doorStep == 120)
            {
                tell.State = nightcoop_mac::net::kDoorLocked;
                tell.Changed = nightcoop_mac::net::kDoorLocked;
                tell.Cause = nightcoop_mac::net::kDoorCauseQuest;
                tell.QuestOps = 1 << nightcoop_mac::net::kDoorOpForceLock;
                what = "our quest locked it";
            }
            else
            {
                tell.Changed = nightcoop_mac::net::kDoorLocked;
                tell.Cause = nightcoop_mac::net::kDoorCauseQuest;
                tell.QuestOps = 1 << nightcoop_mac::net::kDoorOpForceUnlock;
                what = "our quest unlocked it";
            }
            session.SendGameMessage(nightcoop_mac::net::WriteDoorSync(tell), 0);
            std::printf("probe: told the host door %#llx: %s (#%u, from version %llu)\n", static_cast<unsigned long long>(probeDoorKey), what,
                        tell.RequestNumber, static_cast<unsigned long long>(probeDoorVersionSeen));
            std::fflush(stdout);
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
            const uint8_t messageType = nightcoop_mac::net::MessageTypeOf(message.Message);
            if (messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestFacts) || messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestJournal))
            {
                PrintQuestMessage(message.FromPeer, message.Message, questFactBatches, questJournalBatches);
                continue;
            }
            nightcoop_mac::net::AnimWant want;
            nightcoop_mac::net::AnimBatch batch;
            uint32_t gameSeconds = 0;
            if (loot.OnMessage(message.FromPeer, message.Message))
            {
                continue;
            }
            nightcoop_mac::net::NpcSpawn hostNpc;
            nightcoop_mac::net::NpcDespawn hostNpcGone;
            bool fullDeadTable = false;
            std::vector<nightcoop_mac::net::DeadPerson> hostDead;
            if (nightcoop_mac::net::ReadNpcDead(message.Message, fullDeadTable, hostDead))
            {
                std::printf("probe: NpcDead from peer %u: %s%zu npcs (first entity %#llx record %#llx)\n", message.FromPeer, fullDeadTable ? "whole table " : "",
                            hostDead.size(), hostDead.empty() ? 0ULL : static_cast<unsigned long long>(hostDead[0].Entity),
                            hostDead.empty() ? 0ULL : static_cast<unsigned long long>(hostDead[0].Record));
                continue;
            }
            if (nightcoop_mac::net::ReadNpcSpawn(message.Message, hostNpc))
            {
                if (!firstHostNpc)
                {
                    firstHostNpc = hostNpc.NetId;
                }
                const int defeatsWanted = std::getenv("PROBE_DEFEAT") ? std::max(1, std::atoi(std::getenv("PROBE_DEFEAT"))) : 0;
                const auto knownHealth = hostNpcHealth.find(hostNpc.NetId);
                if (defeatsWanted && !defeatSent && step > 60 && !hostNpc.Crowd && knownHealth != hostNpcHealth.end() && knownHealth->second > 0.0f &&
                    defeatedNpcs.insert(hostNpc.NetId).second)
                {
                    defeatSent = static_cast<int>(defeatedNpcs.size()) >= defeatsWanted;
                    nightcoop_mac::net::DeadPerson person {hostNpc.OwnerEntity, hostNpc.Record, hostNpc.MatchKeyA, hostNpc.MatchKeyB,
                                                           {hostNpc.Position[0], hostNpc.Position[1], hostNpc.Position[2]}, 1};
                    session.SendGameMessage(nightcoop_mac::net::WriteNpcDefeat(session.SessionClockMs(), {person}), 0);
                    std::printf("probe: tells the host its npc %#010x (entity %#llx record %#llx) died here\n", hostNpc.NetId,
                                static_cast<unsigned long long>(hostNpc.OwnerEntity), static_cast<unsigned long long>(hostNpc.Record));
                }
                if (std::getenv("PROBE_MIRROR") && !mirrorChosen && step > 20)
                {
                    mirrorChosen = true;
                    mirroredNpc = hostNpc;
                    mirrorStartStep = step;
                    std::printf("probe: shares the hosts npc %#010x (entity %#llx, record %#llx, keys %#llx %#llx) back as our own\n", hostNpc.NetId,
                                static_cast<unsigned long long>(hostNpc.OwnerEntity), static_cast<unsigned long long>(hostNpc.Record),
                                static_cast<unsigned long long>(hostNpc.MatchKeyA), static_cast<unsigned long long>(hostNpc.MatchKeyB));
                }
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
                if (hostNpcGone.Reason == 3)
                {
                    std::printf("probe: the hosts npc %#010x died (killer %#x)\n", hostNpcGone.NetId, hostNpcGone.Killer);
                }
                continue;
            }
            nightcoop_mac::net::NpcSnapshot hostNpcState;
            if (nightcoop_mac::net::ReadNpcSnapshot(message.Message, hostNpcState))
            {
                ++hostNpcSnapshots;
                hostNpcHealth[hostNpcState.NetId] = hostNpcState.Health;
                hostNpcsArmed += hostNpcState.RightHandItem ? 1 : 0;
                hostNpcsStated += (hostNpcState.States[0] || hostNpcState.States[2]) ? 1 : 0;
                hostNpcHighLevels.insert(hostNpcState.States[2]);
                for (const auto& partnerAttitude : hostNpcState.PartnerAttitudes)
                {
                    if (partnerAttitude.Peer == session.GetStatus().MyPeerNumber)
                    {
                        ++hostNpcAttitudesToUs[partnerAttitude.Attitude <= 2 ? partnerAttitude.Attitude : 1];
                    }
                }
                if (hostNpcState.RightHandItem && hostNpcsArmedSeen.insert(hostNpcState.NetId).second && hostNpcsArmedSeen.size() <= 3)
                {
                    std::printf("probe: host npc %#010x holds %#llx, states %u %u %u %u, health %.0f\n", hostNpcState.NetId,
                                static_cast<unsigned long long>(hostNpcState.RightHandItem), hostNpcState.States[0], hostNpcState.States[1],
                                hostNpcState.States[2], hostNpcState.States[3], hostNpcState.Health);
                }
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
            nightcoop_mac::net::NpcHandoffAck offerAnswer;
            if (nightcoop_mac::net::ReadNpcHandoffAck(message.Message, offerAnswer))
            {
                std::printf("probe: peer %u %s our npc %#010x (their entity %#llx, reason %u)\n", message.FromPeer, offerAnswer.Accepted ? "took" : "refused",
                            offerAnswer.NetId, static_cast<unsigned long long>(offerAnswer.Entity), offerAnswer.Reason);
                continue;
            }
            nightcoop_mac::net::NpcClaim seenClaim;
            if (nightcoop_mac::net::ReadNpcClaim(message.Message, seenClaim))
            {
                std::printf("probe: peer %u claims npc %#010x from peer %u (kind %u)\n", message.FromPeer, seenClaim.NetId, seenClaim.FromPeer, seenClaim.Kind);
                continue;
            }
            nightcoop_mac::net::NpcHandoff offered;
            if (nightcoop_mac::net::ReadNpcHandoff(message.Message, offered))
            {
                const bool take = std::getenv("PROBE_TAKE") && !tookNpc;
                nightcoop_mac::net::NpcHandoffAck answer;
                answer.NetId = offered.Spawn.NetId;
                answer.Accepted = take;
                answer.Reason = take ? 0 : nightcoop_mac::net::kHandoffNoBody;
                answer.Entity = take ? 0x4e43 : 0;
                session.SendGameMessage(nightcoop_mac::net::WriteNpcHandoffAck(answer), static_cast<int32_t>(message.FromPeer));
                std::printf("probe: peer %u offers us npc %#010x (record %#llx, health %.0f) and we %s it\n", message.FromPeer, offered.Spawn.NetId,
                            static_cast<unsigned long long>(offered.Spawn.Record), offered.Snapshot.Health, take ? "take" : "refuse");
                if (take)
                {
                    tookNpc = true;
                    takenNpc = offered;
                    takenAtStep = step;
                    session.SendGameMessage(nightcoop_mac::net::WriteNpcClaim({offered.Spawn.NetId, nightcoop_mac::net::kClaimAfterHandoff, message.FromPeer}));
                }
                continue;
            }
            nightcoop_mac::net::RewardWord hostReward;
            if (nightcoop_mac::net::ReadRewardWord(message.Message, hostReward))
            {
                std::printf("probe: peer %u quest reward %#llx (#%u)\n", message.FromPeer, static_cast<unsigned long long>(hostReward.Reward), hostReward.Serial);
                continue;
            }
            nightcoop_mac::net::DeviceBlast hostBlast;
            if (nightcoop_mac::net::ReadDeviceBlast(message.Message, hostBlast))
            {
                for (const auto& device : hostBlast.Devices)
                {
                    std::printf("probe: peer %u blast kind %u of %#llx at %.1f %.1f %.1f (our player %u)\n", message.FromPeer, hostBlast.Kind,
                                static_cast<unsigned long long>(device.DeviceKey), device.Position[0], device.Position[1], device.Position[2],
                                device.ByOurPlayer);
                }
                continue;
            }
            nightcoop_mac::net::DeviceSync hostDevice;
            if (nightcoop_mac::net::ReadDeviceSync(message.Message, hostDevice))
            {
                std::printf("probe: peer %u device %#llx kind %u action kind %u state %d -> %d version %llu request %u fields %zu\n", message.FromPeer,
                            static_cast<unsigned long long>(hostDevice.EntityHash), hostDevice.Kind, hostDevice.ActionKind, hostDevice.StateBefore,
                            hostDevice.StateAfter, static_cast<unsigned long long>(hostDevice.Version), hostDevice.RequestNumber, hostDevice.Fields.size());
                if (std::getenv("PROBE_DEVICE_ECHO") && hostDevice.RequestNumber == 0)
                {
                    devicesToEcho.push_back({step + 40, hostDevice});
                }
                continue;
            }
            nightcoop_mac::net::HackSync hackOnUs;
            if (nightcoop_mac::net::ReadHackSync(message.Message, hackOnUs))
            {
                std::printf("probe: peer %u quickhacks our npc %#010x with %#llx (stage %u, serial %u)\n", message.FromPeer, hackOnUs.NetId,
                            static_cast<unsigned long long>(hackOnUs.ActionRecord), hackOnUs.Stage, hackOnUs.Serial);
                continue;
            }
            nightcoop_mac::net::HitClaim claimOnUs;
            if (nightcoop_mac::net::ReadHitClaim(message.Message, claimOnUs))
            {
                nightcoop_mac::net::HitResult answer;
                answer.ClaimNumber = claimOnUs.ClaimNumber;
                const uint32_t probeNpcNetId = (session.GetStatus().MyPeerNumber << 24) | 7;
                if (probeNpcDead || static_cast<uint32_t>(claimOnUs.NetId) != probeNpcNetId)
                {
                    answer.Outcome = probeNpcDead ? nightcoop_mac::net::kHitTargetDead : nightcoop_mac::net::kHitNoTarget;
                }
                else
                {
                    answer.AppliedPercent = std::min(claimOnUs.HealthPercent, probeNpcHealth);
                    probeNpcHealth = std::max(0.0f, probeNpcHealth - claimOnUs.HealthPercent);
                    answer.HealthAfter = probeNpcHealth;
                }
                session.SendGameMessage(nightcoop_mac::net::WriteHitResult(answer), static_cast<int32_t>(message.FromPeer));
                ++claimsAnswered;
                std::printf("probe: peer %u claims #%u on npc %#010x: %.1f %% (%zu flag(s), attack type %u)%s, our npc is at %.1f %%\n", message.FromPeer,
                            claimOnUs.ClaimNumber, static_cast<uint32_t>(claimOnUs.NetId), claimOnUs.HealthPercent, claimOnUs.Details.Flags.size(),
                            claimOnUs.Details.AttackType, claimOnUs.Flags & nightcoop_mac::net::kHitClaimLethal ? " lethal" : "", probeNpcHealth);
                if (!probeNpcDead && probeNpcHealth <= 0.0f && answer.Outcome == nightcoop_mac::net::kHitApplied)
                {
                    probeNpcDead = true;
                    session.SendGameMessage(nightcoop_mac::net::WriteNpcDespawn({probeNpcNetId, 3, message.FromPeer}));
                    std::printf("probe: our npc died, killed by peer %u\n", message.FromPeer);
                }
                continue;
            }
            nightcoop_mac::net::HitResult claimAnswer;
            if (nightcoop_mac::net::ReadHitResult(message.Message, claimAnswer))
            {
                ++hostClaimResults;
                std::printf("probe: host answered claim #%u: outcome %u applied %.1f %% health after %.1f %%\n", claimAnswer.ClaimNumber, claimAnswer.Outcome,
                            claimAnswer.AppliedPercent, claimAnswer.HealthAfter);
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
            nightcoop_mac::net::DoorSync hostDoor;
            if (nightcoop_mac::net::ReadDoorSync(message.Message, hostDoor))
            {
                std::printf("probe: peer %u door %#llx kind %u state %u changed %u cause %u quest ops %#x actor %u version %llu request %u\n",
                            message.FromPeer, static_cast<unsigned long long>(hostDoor.DoorKey), hostDoor.Kind, hostDoor.State, hostDoor.Changed,
                            hostDoor.Cause, hostDoor.QuestOps, hostDoor.Peer, static_cast<unsigned long long>(hostDoor.Version), hostDoor.RequestNumber);
                std::fflush(stdout);
                if (hostDoor.Kind == nightcoop_mac::net::kDoorShared && hostDoor.DoorKey == probeDoorKey)
                {
                    probeDoorVersionSeen = std::max(probeDoorVersionSeen, hostDoor.Version);
                }
                continue;
            }
            nightcoop_mac::net::DialogBallot ballot;
            nightcoop_mac::net::DialogTally tally;
            nightcoop_mac::net::DialogLine dialogLine;
            nightcoop_mac::net::VoteModeWord voteMode;
            if (nightcoop_mac::net::ReadDialogBallot(message.Message, ballot))
            {
                PrintBallot(message.FromPeer, ballot);
                hostBallot = ballot;
                voteAtStep = step + 25;
                continue;
            }
            if (nightcoop_mac::net::ReadDialogTally(message.Message, tally))
            {
                PrintTally(message.FromPeer, tally, session.SessionClockMs());
                continue;
            }
            if (nightcoop_mac::net::ReadDialogLine(message.Message, dialogLine))
            {
                std::printf("probe: peer %u line %#llx type %u %.1f s '%s': '%s' (%s)\n", message.FromPeer, static_cast<unsigned long long>(dialogLine.LineId),
                            dialogLine.LineType, dialogLine.Seconds, dialogLine.Speaker.c_str(), dialogLine.Text.c_str(), dialogLine.Language.c_str());
                std::fflush(stdout);
                continue;
            }
            if (nightcoop_mac::net::ReadVoteModeWord(message.Message, voteMode))
            {
                std::printf("probe: peer %u decides choices by %s\n", message.FromPeer, voteMode.Mode ? "everyone" : "majority");
                continue;
            }
            if (PrintSceneSkip(message.FromPeer, message.Message))
            {
                continue;
            }
            nightcoop_mac::net::PlayerCarry hostCarry;
            if (nightcoop_mac::net::ReadPlayerCarry(message.Message, hostCarry))
            {
                std::printf("probe: peer %u carry %u kind %u anim %u flags %#x net id %#010x entity %#llx record %#llx look %#llx at %.1f %.1f %.1f\n",
                            message.FromPeer, hostCarry.CarryNumber, hostCarry.Kind, hostCarry.PickupAnimation, hostCarry.Flags, hostCarry.NetId,
                            static_cast<unsigned long long>(hostCarry.EntityHash), static_cast<unsigned long long>(hostCarry.Record & 0xFFFFFFFFFFULL),
                            static_cast<unsigned long long>(hostCarry.Look), hostCarry.Position[0], hostCarry.Position[1], hostCarry.Position[2]);
                continue;
            }
            nightcoop_mac::net::BuildStats hostBuild;
            if (nightcoop_mac::net::ReadBuildStats(message.Message, hostBuild))
            {
                if (hostBuild.Revision != hostBuildRevision)
                {
                    hostBuildRevision = hostBuild.Revision;
                    std::printf("probe: peer %u build rev %u flags %#x:", message.FromPeer, hostBuild.Revision, hostBuild.Flags);
                    for (const auto& stat : hostBuild.Stats)
                    {
                        std::printf(" %s %.0f", nightcoop_mac::net::BuildStatName(stat.Index), stat.Value);
                    }
                    std::printf("\n");
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
        std::printf("probe: %u npc snapshot(s) from the host, %u with a right hand weapon (%zu npcs), %u with states, %zu high level state(s), "
                    "attitude to us friendly %u neutral %u hostile %u\n",
                    hostNpcSnapshots, hostNpcsArmed, hostNpcsArmedSeen.size(), hostNpcsStated, hostNpcHighLevels.size(), hostNpcAttitudesToUs[0],
                    hostNpcAttitudesToUs[1], hostNpcAttitudesToUs[2]);
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
            WalkStartX = static_cast<float>(std::atof(argv[4])) + (std::getenv("PROBE_FAR") ? static_cast<float>(std::atof(std::getenv("PROBE_FAR"))) : 0.0f);
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
