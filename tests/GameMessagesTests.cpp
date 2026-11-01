/*
 byte layout checks for the rebuilt game layer messages (Game envelope PlayerAnim PlayerAnimWant PlayerAnimSetup
 PlayerCustomization NpcAnim DoorSync the dialog votes QuestFacts the device messages and friends)
*/
#include "net/DoorMessages.hpp"
#include "net/DialogMessages.hpp"
#include "net/DeviceMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/LootMessages.hpp"
#include "net/RosterMessages.hpp"
#include "net/VehicleMessages.hpp"
#include "net/WorldMessages.hpp"
#include "net/NpcMessages.hpp"
#include "net/CombatMessages.hpp"
#include "net/QuestMessages.hpp"
#include "net/DroppedMessages.hpp"
#include "net/PlayerExtrasMessages.hpp"
#include "net/SessionMessages.hpp"
#include "net/GrenadeMessages.hpp"

#include <cstdio>
#include <string>

namespace
{
int FailedChecks = 0;

void Check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL  %s\n", what);
        ++FailedChecks;
    }
}
}

int main()
{
    using namespace nightcoop_mac::net;

    const std::vector<uint8_t> inner = {0x50, 0x4F, 0x43, 0x4E, 30, 1, 1};
    const auto envelopeBytes = WriteGameEnvelope({kEveryPeer, 2, true, inner});
    Check(envelopeBytes.size() == 5 + 4 + 4 + 1 + inner.size(), "envelope size");
    Check(envelopeBytes[5] == 0xFF && envelopeBytes[8] == 0xFF, "envelope to everyone is -1");
    GameEnvelope envelope;
    Check(ReadGameEnvelope(envelopeBytes, envelope) && envelope.ToPeer == kEveryPeer && envelope.FromPeer == 2 && envelope.Reliable &&
              envelope.Inner == inner,
          "envelope round trip");
    Check(MessageTypeOf(envelope.Inner) == 30, "inner type");

    AnimBatch batch;
    batch.Unknown00 = 7;
    batch.FullState = true;
    batch.Unknown08 = 123456789;
    batch.Unknown10 = 3;
    AnimEntry speed;
    speed.Kind = AnimKind::Float;
    speed.Name = 0x1111;
    speed.Values[0] = 4.5f;
    AnimEntry crouch;
    crouch.Kind = AnimKind::Bool;
    crouch.Name = 0x2222;
    crouch.IntValue = 1;
    AnimEntry aim;
    aim.Kind = AnimKind::Vector;
    aim.Name = 0x3333;
    aim.Values[0] = 1;
    aim.Values[3] = 1;
    AnimEntry jump;
    jump.Kind = AnimKind::Event;
    jump.Name = 0x4444;
    AnimEntry feature;
    feature.Kind = AnimKind::Feature;
    feature.Name = 0x5555;
    feature.FeatureType = 0x6666;
    feature.Fields.push_back({0x7777, {1, 0, 0, 0}});
    feature.Extra = 9;
    batch.Entries = {speed, crouch, aim, jump, feature};
    const auto batchBytes = WriteAnimBatch(batch);
    const size_t expected = 5 + 4 + 1 + 8 + 4 + 2 + (10 + 4) + (10 + 4) + (10 + 16) + 10 + (10 + 8 + 1 + (8 + 1 + 4) + 1);
    Check(batchBytes.size() == expected, "anim batch size");
    AnimBatch parsed;
    Check(ReadAnimBatch(batchBytes, parsed), "anim batch parses");
    Check(parsed.FullState && !parsed.EntriesHaveExtra && parsed.Unknown08 == 123456789 && parsed.Entries.size() == 5, "anim batch header");
    Check(parsed.Entries[0].Values[0] == 4.5f && parsed.Entries[1].IntValue == 1 && parsed.Entries[2].Values[3] == 1 &&
              parsed.Entries[3].Kind == AnimKind::Event,
          "anim batch inputs");
    Check(parsed.Entries[4].FeatureType == 0x6666 && parsed.Entries[4].Fields.size() == 1 && parsed.Entries[4].Fields[0].Bytes.size() == 4 &&
              parsed.Entries[4].Extra == 9,
          "anim batch feature");

    AnimBatch withExtra = batch;
    withExtra.EntriesHaveExtra = true;
    withExtra.Entries[0].Extra = 5;
    AnimBatch parsedExtra;
    Check(ReadAnimBatch(WriteAnimBatch(withExtra), parsedExtra) && parsedExtra.Entries[0].Extra == 5 && parsedExtra.Entries[4].Extra == 9,
          "anim batch with per entry extra bytes");

    auto broken = batchBytes;
    broken.resize(broken.size() - 3);
    AnimBatch brokenBatch;
    Check(!ReadAnimBatch(broken, brokenBatch), "a cut batch is refused");

    AnimBatch attach;
    AnimEntry attachEntry;
    attachEntry.Kind = AnimKind::Attach;
    attachEntry.Name = 1;
    attach.Entries = {attachEntry};
    AnimBatch attachParsed;
    Check(!ReadAnimBatch(WriteAnimBatch(attach), attachParsed), "an attach without a graph is refused like the dll does");

    AnimWant want;
    Check(ReadAnimWant(WriteAnimWant({true, false}), want) && want.First && !want.Second, "anim want round trip");

    AnimBatch npcBatch = withExtra;
    npcBatch.Unknown00 = 0x01000007;
    npcBatch.Unknown08 = 123456;
    npcBatch.Unknown10 = 42;
    const auto npcBytes = WriteNpcAnim(npcBatch);
    AnimBatch npcParsed;
    Check(npcBytes.size() == batchBytes.size() + npcBatch.Entries.size() - 1 && npcBytes[4] == 23, "npc anim is type 23 with the player batch layout");
    Check(ReadNpcAnim(npcBytes, npcParsed) && npcParsed.Unknown00 == 0x01000007 && npcParsed.Unknown10 == 42 && npcParsed.Entries.size() == npcBatch.Entries.size() &&
              npcParsed.Entries[0].Extra == 5,
          "npc anim round trip");
    Check(!ReadAnimBatch(npcBytes, npcParsed), "a npc batch is not a player batch");
    npcBatch.Unknown00 = 0;
    Check(!ReadNpcAnim(WriteNpcAnim(npcBatch), npcParsed), "a npc batch without net id is refused");
    NpcAnimWant npcWant;
    const auto npcWantBytes = WriteNpcAnimWant({0x02000009, true, false});
    Check(npcWantBytes.size() == 11 && ReadNpcAnimWant(npcWantBytes, npcWant) && npcWant.NetId == 0x02000009 && npcWant.FullState && !npcWant.Reset,
          "npc anim want round trip (6 bytes after the type)");

    PlayerPresence presence;
    presence.Name = "macbook";
    presence.Where = kWhereInWorld;
    presence.PingMs = 42;
    presence.Flags = kPresenceInHostWorld;
    presence.MissingAddons = {"EP1"};
    const auto presenceBytes = WritePlayerPresence(presence);
    PlayerPresence parsedPresence;
    Check(presenceBytes[4] == 111 && ReadPlayerPresence(presenceBytes, parsedPresence) && parsedPresence.Name == "macbook" &&
              parsedPresence.Where == kWhereInWorld && parsedPresence.PingMs == 42 && parsedPresence.Flags == kPresenceInHostWorld &&
              parsedPresence.MissingAddons.size() == 1 && parsedPresence.MissingAddons[0] == "EP1",
          "presence round trip");
    auto presenceWithExtra = presenceBytes;
    presenceWithExtra.push_back(0);
    Check(!ReadPlayerPresence(presenceWithExtra, parsedPresence), "presence with a byte left over is refused");
    PlayerPresence longName;
    longName.Name = std::string(60, 'x');
    Check(ReadPlayerPresence(WritePlayerPresence(longName), parsedPresence) && parsedPresence.Name.size() == 48, "presence name is cut to 48");

    SessionRoster roster;
    roster.Revision = 7;
    roster.WorldName = "ManualSave-47";
    roster.Entries.push_back({0, "host mac", kWhereInWorld, 0, kRoleHost, kPresenceInHostWorld, 0, 0, "", {}, ""});
    roster.Entries.push_back({1, "guest", kWhereLoading, 80, 0, 0, 0, 1, "", {"EP1", "x"}, "en"});
    const auto rosterBytes = WriteSessionRoster(roster);
    SessionRoster parsedRoster;
    Check(rosterBytes[4] == 112 && ReadSessionRoster(rosterBytes, parsedRoster) && parsedRoster.Revision == 7 &&
              parsedRoster.HostState == kHostStateInWorld && parsedRoster.WorldName == "ManualSave-47" && parsedRoster.Entries.size() == 2 &&
              parsedRoster.Entries[0].Role == kRoleHost && parsedRoster.Entries[1].Name == "guest" && parsedRoster.Entries[1].PingMs == 80 &&
              parsedRoster.Entries[1].MissingAddons.size() == 2 && parsedRoster.Entries[1].Language == "en",
          "roster round trip");
    auto badRoster = rosterBytes;
    badRoster[9] = 9;
    Check(!ReadSessionRoster(badRoster, parsedRoster), "roster with a host state over 5 is refused");

    VehicleSpawn spawn;
    spawn.NetId = (2u << 24) | 5;
    spawn.HomePeer = 2;
    spawn.Record = 0x1234567890ULL;
    spawn.Doors[3] = 4;
    spawn.Position[0] = -1091.5f;
    const auto spawnBytes = WriteVehicleSpawn(spawn);
    VehicleSpawn parsedSpawn;
    Check(spawnBytes.size() == 5 + 68 && ReadVehicleSpawn(spawnBytes, parsedSpawn) && parsedSpawn.NetId == spawn.NetId && parsedSpawn.Record == spawn.Record &&
              parsedSpawn.Doors[3] == 4 && parsedSpawn.Position[0] == -1091.5f && parsedSpawn.Orientation[3] == 1.0f,
          "vehicle spawn is 68 bytes and round trips");
    VehicleState carState;
    carState.NetId = spawn.NetId;
    carState.StampMs = 99999;
    carState.Velocity[1] = 12.5f;
    carState.Flags = kVehicleEngineOn | kVehicleSomeoneDrives;
    carState.Steering = -100;
    carState.Occupants.push_back({0xABCDEFULL, kOccupantPlayer, 2});
    const auto carStateBytes = WriteVehicleState(carState);
    VehicleState parsedCarState;
    Check(carStateBytes.size() == 5 + 58 + 13 && ReadVehicleState(carStateBytes, parsedCarState) && parsedCarState.StampMs == 99999 &&
              parsedCarState.Velocity[1] == 12.5f && parsedCarState.Steering == -100 && parsedCarState.Occupants.size() == 1 &&
              parsedCarState.Occupants[0].Id == 2,
          "vehicle state is 58 + 13 per occupant and round trips");
    VehicleSeat seat;
    seat.NetId = spawn.NetId;
    seat.Slot = 0x55;
    seat.Position[2] = 0.4f;
    VehicleSeat parsedSeat;
    Check(WriteVehicleSeat(seat).size() == 5 + 40 && ReadVehicleSeat(WriteVehicleSeat(seat), parsedSeat) && parsedSeat.Slot == 0x55, "vehicle seat round trip");
    seat.Slot = 0;
    Check(!ReadVehicleSeat(WriteVehicleSeat(seat), parsedSeat), "a seat in a car without a slot is refused");
    VehicleDespawn despawn;
    Check(ReadVehicleDespawn(WriteVehicleDespawn({spawn.NetId, 2}), despawn) && despawn.Reason == 2, "vehicle despawn round trip");

    PlayerRevive revive;
    revive.TargetPeer = 1;
    revive.HelperPosition[0] = -1090.0f;
    PlayerRevive parsedRevive;
    Check(WritePlayerRevive(revive).size() == 21 && ReadPlayerRevive(WritePlayerRevive(revive), parsedRevive) && parsedRevive.TargetPeer == 1 &&
              parsedRevive.HelperPosition[0] == -1090.0f,
          "player revive is 21 bytes and round trips");
    uint8_t downedKind = 0;
    Check(WriteDownedNotice(2).size() == 6 && ReadDownedNotice(WriteDownedNotice(2), downedKind) && downedKind == 2 &&
              !ReadDownedNotice(WriteDownedNotice(3), downedKind),
          "downed notice is 6 bytes, kind 1 or 2");

    LiftSync departure;
    departure.Kind = kLiftDeparture;
    departure.Peer = 0;
    departure.LiftKey = 0xF938D43FBBC7E743ULL;
    departure.FloorFrom = 0;
    departure.FloorTo = 3;
    departure.DepartureMs = 123456;
    departure.DepartureNumber = 9;
    departure.Flags = 0x40;
    LiftSync parsedLift;
    Check(WriteLiftSync(departure).size() == 58 && ReadLiftSync(WriteLiftSync(departure), parsedLift) && parsedLift.LiftKey == departure.LiftKey &&
              parsedLift.FloorTo == 3 && parsedLift.DepartureNumber == 9,
          "lift sync is 58 bytes and round trips");
    departure.FloorTo = -1;
    Check(!ReadLiftSync(WriteLiftSync(departure), parsedLift), "a lift departure without a target floor is refused");
    LiftSync request;
    request.Kind = kLiftRequest;
    request.FloorTo = 2;
    Check(!ReadLiftSync(WriteLiftSync(request), parsedLift), "a lift request without a request number is refused");

    DoorSync doorTell;
    doorTell.Kind = kDoorTell;
    doorTell.Peer = 2;
    doorTell.DoorKey = 0x8D2C1E0F5A3B7701ULL;
    doorTell.State = kDoorOpen;
    doorTell.Changed = kDoorOpen | kDoorLocked;
    doorTell.Cause = kDoorCauseQuest;
    doorTell.QuestOps = (1 << kDoorOpForceOpen) | (1 << kDoorOpForceUnlock);
    doorTell.Side = 2;
    doorTell.Version = 0;
    doorTell.RequestNumber = 0x4321;
    doorTell.Position[0] = -1300.5f;
    doorTell.Position[1] = 1200.25f;
    doorTell.Position[2] = 18.0f;
    const auto doorTellBytes = WriteDoorSync(doorTell);
    DoorSync parsedDoor;
    Check(doorTellBytes.size() == 46 && doorTellBytes[4] == 88 && ReadDoorSync(doorTellBytes, parsedDoor) && parsedDoor.Kind == kDoorTell &&
              parsedDoor.Peer == 2 && parsedDoor.DoorKey == doorTell.DoorKey && parsedDoor.State == kDoorOpen && parsedDoor.Changed == 3 &&
              parsedDoor.Cause == kDoorCauseQuest && parsedDoor.QuestOps == 0x82 && parsedDoor.Side == 2 && parsedDoor.RequestNumber == 0x4321 &&
              parsedDoor.Position[1] == 1200.25f,
          "door sync is 46 bytes and round trips");
    Check(doorTellBytes[5] == kDoorTell && doorTellBytes[6] == 2 && doorTellBytes[10] == 0x01 && doorTellBytes[17] == 0x8D && doorTellBytes[18] == kDoorOpen &&
              doorTellBytes[19] == 3 && doorTellBytes[20] == kDoorCauseQuest && doorTellBytes[21] == 0x82 && doorTellBytes[22] == 0 &&
              doorTellBytes[23] == 2 && doorTellBytes[32] == 0x21 && doorTellBytes[33] == 0x43,
          "door sync fields sit at the dll offsets");
    DoorSync doorShared = doorTell;
    doorShared.Kind = kDoorShared;
    doorShared.Version = 1730000000123ULL;
    doorShared.RequestNumber = 0;
    Check(ReadDoorSync(WriteDoorSync(doorShared), parsedDoor) && parsedDoor.Version == 1730000000123ULL && parsedDoor.RequestNumber == 0,
          "a shared door state may carry no request number");
    doorShared.Version = 0;
    Check(!ReadDoorSync(WriteDoorSync(doorShared), parsedDoor), "a shared door state without a version is refused");
    DoorSync badDoor = doorTell;
    badDoor.RequestNumber = 0;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "a door tell without a request number is refused");
    badDoor = doorTell;
    badDoor.DoorKey = 0;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "a door without a key is refused");
    badDoor = doorTell;
    badDoor.State = 0x08;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "door state bits past sealed are refused");
    badDoor = doorTell;
    badDoor.Cause = kDoorCausePlayerOrNpc;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "quest ops without the quest cause are refused");
    badDoor = doorTell;
    badDoor.QuestOps = 1;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "quest op bit 0 is refused");
    badDoor = doorTell;
    badDoor.QuestOps = 0;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "the quest cause without ops is refused");
    badDoor = doorTell;
    badDoor.Side = 3;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "door side 3 is refused");
    badDoor = doorTell;
    badDoor.Kind = 3;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "door kind 3 is refused");
    badDoor = doorTell;
    badDoor.Position[2] = 100001.0f;
    Check(!ReadDoorSync(WriteDoorSync(badDoor), parsedDoor), "a door position past 100 km is refused");
    auto longDoor = doorTellBytes;
    longDoor.push_back(0);
    Check(!ReadDoorSync(longDoor, parsedDoor), "a door message of 47 bytes is refused");

    WorldWant worldWant;
    Check(WriteWorldWant({1, 7}).size() == 10 && ReadWorldWant(WriteWorldWant({1, 7}), worldWant) && worldWant.WorldNumber == 7 &&
              !ReadWorldWant(WriteWorldWant({6, 7}), worldWant),
          "world want is 10 bytes, reason 1..5");
    WorldFile chunk;
    chunk.TransferId = 3;
    chunk.Kind = kWorldFileMetadata;
    chunk.TotalSize = 10;
    chunk.Offset = 4;
    chunk.Bytes = {1, 2, 3, 4, 5, 6};
    chunk.Hash = WorldFileHash(chunk.Bytes);
    WorldFile parsedChunk;
    Check(WriteWorldFile(chunk).size() == 31 + 6 && ReadWorldFile(WriteWorldFile(chunk), parsedChunk) && parsedChunk.Offset == 4 &&
              parsedChunk.Bytes.size() == 6 && parsedChunk.Hash == chunk.Hash,
          "world file chunk is 31 bytes + its bytes and round trips");
    chunk.Offset = 5;
    Check(!ReadWorldFile(WriteWorldFile(chunk), parsedChunk), "a world file chunk past the file end is refused");
    WorldStatus worldStatus;
    Check(ReadWorldStatus(WriteWorldStatus({3, 3, "ManualSave-12"}), worldStatus) && worldStatus.Text == "ManualSave-12" &&
              !ReadWorldStatus(WriteWorldStatus({10, 0, ""}), worldStatus) && ReadWorldReload(WriteWorldReload({5, 0, ""}), worldStatus) &&
              !ReadWorldReload(WriteWorldReload({6, 0, ""}), worldStatus),
          "world status 1..9 and world reload 1..5");
    Check(WorldFileHash({}) == 0xcbf29ce484222325ULL, "world file hash is fnv1a64");

    NpcSpawn npcSpawn;
    npcSpawn.NetId = (2u << 24) | 9;
    npcSpawn.Record = 0x1234;
    npcSpawn.Position[0] = -1080.0f;
    npcSpawn.Yaw = 45.0f;
    NpcSpawn parsedNpcSpawn;
    Check(WriteNpcSpawn(npcSpawn).size() == 5 + 62 && ReadNpcSpawn(WriteNpcSpawn(npcSpawn), parsedNpcSpawn) && parsedNpcSpawn.Record == 0x1234 &&
              parsedNpcSpawn.Yaw == 45.0f && parsedNpcSpawn.HighLevelState == 0xFF,
          "npc spawn is 62 bytes and round trips");
    NpcSnapshot npcSnapshot;
    npcSnapshot.NetId = npcSpawn.NetId;
    npcSnapshot.Health = 55.0f;
    npcSnapshot.PartnerAttitudes = {{1, 2}, {3, 0}};
    npcSnapshot.OwnerClockMs = 777;
    NpcSnapshot parsedNpcSnapshot;
    Check(WriteNpcSnapshot(npcSnapshot).size() == 5 + 68 + 4 && ReadNpcSnapshot(WriteNpcSnapshot(npcSnapshot), parsedNpcSnapshot) &&
              parsedNpcSnapshot.Health == 55.0f && parsedNpcSnapshot.PartnerAttitudes.size() == 2 && parsedNpcSnapshot.PartnerAttitudes[1].Peer == 3 &&
              parsedNpcSnapshot.OwnerClockMs == 777,
          "npc snapshot is 68 + 2 per partner and round trips");
    npcSnapshot.Position[1] = 200000.0f;
    Check(!ReadNpcSnapshot(WriteNpcSnapshot(npcSnapshot), parsedNpcSnapshot), "an npc snapshot past 100 km is refused");
    NpcDespawn npcDespawn;
    Check(WriteNpcDespawn({npcSpawn.NetId, 3, 1}).size() == 5 + 9 && ReadNpcDespawn(WriteNpcDespawn({npcSpawn.NetId, 3, 1}), npcDespawn) &&
              npcDespawn.Killer == 1,
          "npc despawn is 9 bytes");

    ItemTransfer transfer;
    transfer.TransferId = (2ull << 32) | 77;
    transfer.Recipient = 0;
    transfer.Item = {ItemRecordFromName("Items.money"), 0, 0, 0, 0};
    transfer.Quantity = 250;
    const auto offerBytes = WriteItemTransfer(kItemTransferOffer, transfer);
    uint8_t transferType = 0;
    ItemTransfer parsedTransfer;
    Check(offerBytes.size() == 37 && offerBytes[4] == 38 && ReadItemTransfer(offerBytes, transferType, parsedTransfer) &&
              transferType == kItemTransferOffer && parsedTransfer == transfer,
          "item transfer offer is 37 bytes and round trips");
    Check(transfer.Item.Record == 0x0BF5E188ECull, "record from name is crc32 | length << 32");
    Check(offerBytes[5 + 0x0C] == 0xEC && offerBytes[5 + 0x1C] == 250 && offerBytes[5 + 0x08] == 0, "item transfer field offsets");
    Check(ReadItemTransfer(WriteItemTransfer(kItemTransferGrant, transfer), transferType, parsedTransfer) && transferType == kItemTransferGrant,
          "item transfer grant is type 40 with the same body");
    transfer.Quantity = 10000;
    Check(!ReadItemTransfer(WriteItemTransfer(kItemTransferDebit, transfer), transferType, parsedTransfer), "an item transfer over 9999 is refused");
    transfer.Quantity = 1;
    transfer.Recipient = 0xFFFFFFFF;
    Check(!ReadItemTransfer(WriteItemTransfer(kItemTransferDebit, transfer), transferType, parsedTransfer), "an item transfer to nobody is refused");
    ItemTransferAck transferAck;
    const auto ackBytes = WriteItemTransferAck({transfer.TransferId, kTransferPhaseGrant, true});
    Check(ackBytes.size() == 15 && ackBytes[4] == 41 && ReadItemTransferAck(ackBytes, transferAck) && transferAck.TransferId == transfer.TransferId &&
              transferAck.Phase == kTransferPhaseGrant && transferAck.Ok,
          "item transfer ack is 15 bytes and round trips");
    Check(!ReadItemTransferAck(WriteItemTransferAck({transfer.TransferId, 3, true}), transferAck), "an ack phase over 2 is refused");

    LootState lootState;
    lootState.ContainerId = (1ull << 24) | 5;
    lootState.Revision = 3;
    lootState.Bag = true;
    lootState.Position[0] = -1300.0f;
    lootState.Orientation[2] = 0.5f;
    lootState.Entries.push_back({{0x1519DBB192ull, 1234, 0, 1, 0}, 2, 0});
    lootState.Entries.push_back({{0x0BF5E188ECull, 0, 0, 0, 0}, 500, kLootQualityUnknown});
    const auto lootStateBytes = WriteLootState(lootState);
    LootState parsedLootState;
    Check(lootStateBytes.size() == 78 + 2 * 21 && lootStateBytes[4] == 42 && ReadLootState(lootStateBytes, parsedLootState) &&
              parsedLootState.ContainerId == lootState.ContainerId && parsedLootState.Bag && parsedLootState.Entries == lootState.Entries &&
              parsedLootState.Orientation[2] == 0.5f && parsedLootState.Orientation[3] == 1.0f,
          "loot state is 78 + 21 per entry and round trips");
    Check(lootStateBytes[5 + 0x47] == 2 && lootStateBytes[5 + 0x49 + 16] == 2 && lootStateBytes[5 + 0x49 + 21 + 20] == 0xFF, "loot state field offsets");
    lootState.State = kLootClosed;
    Check(WriteLootState(lootState).size() == 78 && ReadLootState(WriteLootState(lootState), parsedLootState) && parsedLootState.Entries.empty(),
          "a closed loot state carries no entries");
    lootState.State = kLootOpen;
    lootState.Entries.assign(65, lootState.Entries[0]);
    Check(WriteLootState(lootState).size() == 78 + 64 * 21, "a loot state is cut to 64 entries");
    auto tooManyEntries = WriteLootState(lootState);
    tooManyEntries[5 + 0x47] = 65;
    Check(!ReadLootState(tooManyEntries, parsedLootState), "a loot state with 65 entries is refused");
    lootState.Entries.resize(1);
    lootState.Entries[0].Quality = 11;
    Check(!ReadLootState(WriteLootState(lootState), parsedLootState), "a loot entry quality of 11 is refused");
    lootState.Entries[0].Quality = 3;
    lootState.ContainerId = 0;
    Check(!ReadLootState(WriteLootState(lootState), parsedLootState), "a loot state without container id is refused");
    LootTake take;
    take.ContainerId = (1ull << 24) | 5;
    take.TakeNumber = 9;
    take.Item = {{0x1519DBB192ull, 1234, 0, 1, 0}, 1, 4};
    LootTake parsedTake;
    Check(WriteLootTake(take).size() == 39 && ReadLootTake(WriteLootTake(take), parsedTake) && parsedTake.TakeNumber == 9 && parsedTake.Item == take.Item,
          "loot take is 39 bytes and round trips");
    take.TakeNumber = 0;
    Check(!ReadLootTake(WriteLootTake(take), parsedTake), "a loot take without take number is refused");
    LootTakeResult takeResult;
    Check(WriteLootTakeResult({kLootDrop, take.ContainerId, 9, true}).size() == 19 &&
              ReadLootTakeResult(WriteLootTakeResult({kLootDrop, take.ContainerId, 9, true}), takeResult) && takeResult.Granted &&
              !ReadLootTakeResult(WriteLootTakeResult({4, take.ContainerId, 9, true}), takeResult),
          "loot take result is 19 bytes, kind 1..3");
    {
        DeviceSync deviceRequest;
        deviceRequest.Kind = kDeviceRequest;
        deviceRequest.Peer = 2;
        deviceRequest.EntityHash = 0x1122334455667788ULL;
        deviceRequest.ComponentName = 0x99;
        deviceRequest.PsClass = 0xAABB;
        deviceRequest.ActionKind = kDeviceSameAction;
        deviceRequest.StateBefore = kDeviceOff;
        deviceRequest.StateAfter = kDeviceOn;
        deviceRequest.Flags = kDeviceQuickHack;
        deviceRequest.RequestNumber = 77;
        deviceRequest.ActionClass = 0xCCDD;
        deviceRequest.Duration = 12.5f;
        const auto plainDeviceBytes = WriteDeviceSync(deviceRequest);
        Check(plainDeviceBytes.size() == 5 + 80 && plainDeviceBytes[4] == 139, "device sync without fields is 80 bytes after the type");
        deviceRequest.HasProp = true;
        deviceRequest.PropName = 0x1234;
        deviceRequest.PropTypeName = 0x5678;
        deviceRequest.PropFlags = 3;
        deviceRequest.Fields.push_back({0x1111, 0x2222, {1}});
        deviceRequest.Fields.push_back({0x3333, 0x4444, {1, 2, 3, 4, 5, 6, 7, 8}});
        deviceRequest.PropValues.push_back({1, 0x5555, {9, 9, 9, 9}});
        const auto deviceBytes = WriteDeviceSync(deviceRequest);
        DeviceSync parsedDevice;
        Check(deviceBytes.size() == 5 + 80 + (17 + 1) + (17 + 8) + (17 + 4) && ReadDeviceSync(deviceBytes, parsedDevice) &&
                  parsedDevice.EntityHash == deviceRequest.EntityHash && parsedDevice.StateBefore == kDeviceOff && parsedDevice.StateAfter == kDeviceOn &&
                  parsedDevice.RequestNumber == 77 && parsedDevice.Duration == 12.5f && parsedDevice.HasProp && parsedDevice.PropFlags == 3 &&
                  parsedDevice.Fields.size() == 2 && parsedDevice.Fields[1].Bytes.size() == 8 && parsedDevice.Fields[1].Bytes[7] == 8 &&
                  parsedDevice.PropValues.size() == 1 && parsedDevice.PropValues[0].Index == 1 && parsedDevice.PropValues[0].Bytes.size() == 4,
              "device sync with fields and a prop round trips");
        Check(deviceBytes[5 + 0x1F] == 0 && deviceBytes[5 + 0x20] == 1 && deviceBytes[5 + 0x2A] == 77 && deviceBytes[5 + 0x3C] == 1 && deviceBytes[5 + 0x4E] == 2,
              "device sync offsets match the dll (states at 0x1f, request at 0x2a, has prop at 0x3c, field count at 0x4e)");
        auto deviceWithExtra = deviceBytes;
        deviceWithExtra.push_back(0);
        Check(!ReadDeviceSync(deviceWithExtra, parsedDevice), "device sync with a byte left over is refused");
        DeviceSync noRequest = deviceRequest;
        noRequest.RequestNumber = 0;
        Check(!ReadDeviceSync(WriteDeviceSync(noRequest), parsedDevice), "a device request without a request number is refused");
        DeviceSync hostAnswer = deviceRequest;
        hostAnswer.Kind = kDeviceAnswer;
        hostAnswer.Version = 0;
        Check(!ReadDeviceSync(WriteDeviceSync(hostAnswer), parsedDevice), "a host answer without a version is refused");
        hostAnswer.Version = 5;
        hostAnswer.StateAfter = kDeviceStateUnknown;
        Check(ReadDeviceSync(WriteDeviceSync(hostAnswer), parsedDevice) && parsedDevice.StateAfter == kDeviceStateUnknown, "unknown state 0x7f passes");
        DeviceSync hostOnlyReplay = hostAnswer;
        hostOnlyReplay.Kind = kDeviceReplay;
        hostOnlyReplay.ActionKind = kDeviceSameActionHostOnly;
        Check(!ReadDeviceSync(WriteDeviceSync(hostOnlyReplay), parsedDevice), "a replay of a host only action is refused");
        DeviceSync skillWithoutSkill = hostAnswer;
        skillWithoutSkill.ActionKind = kDeviceSkillCheck;
        Check(!ReadDeviceSync(WriteDeviceSync(skillWithoutSkill), parsedDevice), "a skill check needs its skill");
        skillWithoutSkill.Skill = 2;
        Check(ReadDeviceSync(WriteDeviceSync(skillWithoutSkill), parsedDevice) && parsedDevice.Skill == 2, "a skill check with its skill passes");
        DeviceSync replayWithoutClass = hostAnswer;
        replayWithoutClass.ActionClass = 0;
        Check(!ReadDeviceSync(WriteDeviceSync(replayWithoutClass), parsedDevice), "the same action without an action class is refused");
        DeviceSync longTimer = hostAnswer;
        longTimer.Duration = 4000.0f;
        Check(!ReadDeviceSync(WriteDeviceSync(longTimer), parsedDevice), "a duration past an hour is refused");
        DeviceSync badState = hostAnswer;
        badState.StateBefore = 5;
        Check(!ReadDeviceSync(WriteDeviceSync(badState), parsedDevice), "a device state outside -2..2 is refused");
        DeviceSync manyFields = hostAnswer;
        manyFields.Fields.assign(60, {0x1, 0x2, {1}});
        Check(ReadDeviceSync(WriteDeviceSync(manyFields), parsedDevice) && parsedDevice.Fields.size() == 48, "fields past 48 are not sent");

        TurretWord turretAsk;
        turretAsk.Kind = kTurretAsk;
        turretAsk.TurretKey = 0xABCDEF0123ULL;
        TurretWord parsedTurret;
        const auto turretBytes = WriteTurretWord(turretAsk);
        Check(turretBytes.size() == 15 && turretBytes[4] == 214 && ReadTurretWord(turretBytes, parsedTurret) && parsedTurret.TurretKey == turretAsk.TurretKey,
              "turret word is 15 bytes and round trips");
        turretAsk.State = kTurretCalm;
        Check(!ReadTurretWord(WriteTurretWord(turretAsk), parsedTurret), "a turret ask with a state is refused");
        Check(ReadTurretWord(WriteTurretWord({kTurretAnswer, 0x55, kTurretInCombat}), parsedTurret) && parsedTurret.State == kTurretInCombat,
              "a turret answer in combat passes");

        DeviceBlast blast;
        blast.Kind = kBlastLive;
        blast.Devices.push_back({0xF938D43FBBC7E743ULL, {-1080.0f, 1200.5f, 18.0f}, 1});
        blast.Devices.push_back({0x0000000123000000ULL, {1, 2, 3}, 0});
        const auto blastBytes = WriteDeviceBlast(blast);
        DeviceBlast parsedBlast;
        Check(blastBytes.size() == 5 + 2 + 2 * 21 && blastBytes[4] == 130 && ReadDeviceBlast(blastBytes, parsedBlast) && parsedBlast.Devices.size() == 2 &&
                  parsedBlast.Devices[0].Position[1] == 1200.5f && parsedBlast.Devices[0].ByOurPlayer == 1,
              "device blast is 2 + 21 per device and round trips");
        blast.Devices.push_back({0x0000000000FFFFFFULL, {0, 0, 0}, 0});
        Check(!ReadDeviceBlast(WriteDeviceBlast(blast), parsedBlast), "a blast of a spawned device (no bits above 24) is refused");
        blast.Devices.pop_back();
        blast.Kind = 2;
        Check(!ReadDeviceBlast(WriteDeviceBlast(blast), parsedBlast), "a blast kind over 1 is refused");
        blast.Kind = kBlastSnapshot;
        blast.Devices.assign(40, {0x0100000000000001ULL, {0, 0, 0}, 0});
        Check(ReadDeviceBlast(WriteDeviceBlast(blast), parsedBlast) && parsedBlast.Devices.size() == 32, "a blast carries at most 32 devices");
    }

    const auto clock = WriteGameClock(123456);
    uint32_t gameSeconds = 0;
    Check(clock.size() == 9 && clock[4] == 46 && ReadGameClock(clock, gameSeconds) && gameSeconds == 123456, "game clock is magic | 46 | u32 seconds");

    AnimSetup ourSetup;
    ourSetup.Rig = 0x0807060504030201ULL;
    ourSetup.Sets.push_back({0x1111, 3, {0xAA, 0xBB}});
    ourSetup.Sets.push_back({0x2222, 0, {}});
    const auto setupBytes = WriteAnimSetup(ourSetup);
    AnimSetup setup;
    Check(setupBytes.size() == 5 + 8 + 2 + (8 + 1 + 1 + 16) + (8 + 1 + 1) && ReadAnimSetup(setupBytes, setup) &&
              setup.Rig == 0x0807060504030201ULL && setup.SetCount == 2 && setup.Sets.size() == 2 && setup.Sets[0].Priority == 3 &&
              setup.Sets[0].VariableNames.size() == 2 && setup.Sets[0].VariableNames[1] == 0xBB && setup.Sets[1].AnimSet == 0x2222,
          "anim setup round trip");
    const std::vector<uint8_t> cutShort = {0x50, 0x4F, 0x43, 0x4E, 34, 1, 2, 3, 4, 5, 6, 7, 8, 2, 0};
    Check(!ReadAnimSetup(cutShort, setup), "anim setup without its sets is refused");

    PlayerCustomization look;
    look.DataVersion = 7;
    look.BodyMale = true;
    look.BrainMale = false;
    LookGroup hair;
    hair.Name = 0x1111;
    hair.Parts.push_back({0xAAAA, 0xBBBB, 0xCCCC, 2, 1});
    hair.Morphs.push_back({0xDDDD, 0xEEEE, 0, 0});
    look.Head.push_back(hair);
    look.Body.push_back({0x2222, {}, {}});
    look.Perspectives.push_back({0x3333, 0x4444, 0x5555});
    look.Tags = {0x6666, 0x7777};
    const auto lookBytes = WritePlayerCustomization(look);
    Check(lookBytes.size() == 5 + 1 + 4 + 1 + 1 + (2 + 8 + 1 + 29 + 1 + 21) + (2 + 8 + 1 + 1) + 2 + (1 + 24) + (1 + 16), "look size");
    Check(lookBytes[4] == 33 && lookBytes[5] == 1, "look is message 33 version 1");
    PlayerCustomization lookBack;
    Check(ReadPlayerCustomization(lookBytes, lookBack), "look reads back");
    Check(lookBack.DataVersion == 7 && lookBack.BodyMale && !lookBack.BrainMale, "look header fields");
    Check(lookBack.Head.size() == 1 && lookBack.Head[0].Parts.size() == 1 && lookBack.Head[0].Parts[0].Definition == 0xBBBB &&
              lookBack.Head[0].Parts[0].CensorAction == 1 && lookBack.Head[0].Morphs[0].TargetName == 0xEEEE,
          "look head group");
    Check(lookBack.Body.size() == 1 && lookBack.Arms.empty() && lookBack.Perspectives[0].Tpp == 0x5555 && lookBack.Tags[1] == 0x7777,
          "look body, arms, perspectives and tags");
    bool genderFromLook = false;
    Check(ReadCustomizationGender(lookBytes, genderFromLook) && genderFromLook, "the gender reader still reads a full look");
    auto truncated = lookBytes;
    truncated.resize(truncated.size() - 3);
    Check(!ReadPlayerCustomization(truncated, lookBack), "a cut look is refused");
    Check(LookFingerprint(lookBytes) != LookFingerprint(truncated), "look fingerprints differ");

    PlayerClothes clothes;
    clothes.Female = true;
    clothes.Items.push_back({0x0A00000012345678ull, 7, 2, 1, 0});
    clothes.Items.push_back({0x0000000522223333ull, 0, 0, 0, 1});
    clothes.Arms = 0x0000000F14BB03F3ull;
    clothes.Slots = {0x1111, 0x2222};
    const auto clothesBytes = WritePlayerClothes(clothes);
    Check(clothesBytes.size() == 5 + 1 + 2 + 2 * 16 + 8 + 2 + 2 * 8, "clothes size");
    Check(clothesBytes[4] == 31, "clothes are message 31");
    PlayerClothes clothesBack;
    Check(ReadPlayerClothes(clothesBytes, clothesBack), "clothes read back");
    Check(clothesBack.Female && clothesBack.Items.size() == 2 && clothesBack.Items[0].RngSeed == 7 && clothesBack.Items[0].UniqueCounter == 2 &&
              clothesBack.Items[1].Flags == 1 && clothesBack.Arms == clothes.Arms && clothesBack.Slots.size() == 2 && clothesBack.Slots[1] == 0x2222,
          "clothes fields");
    Check(RecordWithoutOffset(clothesBack.Items[0].Record) == 0x12345678ull, "record offset is masked off");
    auto noTail = clothesBytes;
    noTail.resize(5 + 1 + 2 + 2 * 16 + 8);
    Check(ReadPlayerClothes(noTail, clothesBack) && clothesBack.Slots.empty() && clothesBack.Items.size() == 2, "clothes without the slot tail");
    clothes.Slots.pop_back();
    Check(ReadPlayerClothes(WritePlayerClothes(clothes), clothesBack) && clothesBack.Slots.empty(), "mismatched slots are not sent");

    HitClaim claim;
    claim.ClaimNumber = 9;
    claim.NetId = 0x00123456;
    claim.HealthPercent = 12.5f;
    claim.Flags = kHitClaimLethal;
    claim.ClaimTimeMs = 777;
    claim.Details.AttackRecord = 0xAB12345678ull;
    claim.Details.AttackType = 10;
    claim.Details.Offset[0] = 0.2f;
    claim.Details.Direction[1] = 1.0f;
    claim.Details.Flags = {19, 26};
    const auto claimBytes = WriteHitClaim(claim);
    HitClaim claimBack;
    Check(claimBytes.size() == 5 + 26 + 39 + 8, "hit claim is 26 + 39 + 4 per flag bytes after the header");
    Check(ReadHitClaim(claimBytes, claimBack) && claimBack.ClaimNumber == 9 && claimBack.NetId == 0x00123456 && claimBack.HealthPercent == 12.5f &&
              claimBack.Flags == kHitClaimLethal && claimBack.Details.AttackType == 10 && claimBack.Details.Flags.size() == 2 &&
              claimBack.Details.Flags[1] == 26 && claimBack.Details.Offset[0] == 0.2f,
          "hit claim round trip");
    HitClaim farClaim = claim;
    farClaim.Details.Offset[0] = 9.0f;
    Check(ReadHitClaim(WriteHitClaim(farClaim), claimBack) && claimBack.Details.Offset[0] == 0.0f && claimBack.Details.Offset[2] == 1.3f,
          "a hit offset past 2.5 m goes as (0, 0, 1.3)");
    HitClaim zeroClaim = claim;
    zeroClaim.HealthPercent = 0.0f;
    Check(!ReadHitClaim(WriteHitClaim(zeroClaim), claimBack), "a claim of 0 % is refused");
    HitResult result {9, kHitApplied, 12.5f, 40.0f};
    HitResult resultBack;
    const auto resultBytes = WriteHitResult(result);
    Check(resultBytes.size() == 5 + 13 && ReadHitResult(resultBytes, resultBack) && resultBack.Outcome == kHitApplied && resultBack.HealthAfter == 40.0f,
          "hit result is 13 bytes and reads back");
    NpcAction action {1, 9, 3};
    NpcAction actionBack;
    Check(WriteNpcAction(action).size() == 5 + 6 && ReadNpcAction(WriteNpcAction(action), actionBack) && actionBack.Zone == 3, "npc action round trip");
    PlayerHit playerHit;
    playerHit.HitNumber = 4;
    playerHit.AttackerNetId = 0x00000011;
    playerHit.Details.Flags = {15};
    PlayerHit playerHitBack;
    Check(WritePlayerHit(playerHit).size() == 5 + 16 + 39 + 4 && ReadPlayerHit(WritePlayerHit(playerHit), playerHitBack) &&
              playerHitBack.AttackerNetId == 0x11 && playerHitBack.Details.Flags[0] == 15,
          "player hit is 60 + 4 per flag bytes with the header");
    HackSync hack {kHackOnNpc, kHackStageCompletion, kHackFlagOwnHack, 5, 2, 0x00112233, 0x1A2B3C4D5EULL};
    HackSync hackBack;
    const auto hackBytes = WriteHackSync(hack);
    Check(hackBytes.size() == 5 + 23 && hackBytes[4] == 140 && ReadHackSync(hackBytes, hackBack) && hackBack.NetId == 0x00112233 &&
              hackBack.Stage == kHackStageCompletion && hackBack.ActionRecord == 0x1A2B3C4D5EULL,
          "hack sync is 23 bytes");
    hack.Serial = 0;
    Check(!ReadHackSync(WriteHackSync(hack), hackBack), "a hack sync without serial is refused");
    RewardWord reward {kRewardQuest, 0x18a9e8e769ULL, 3, 0};
    RewardWord rewardBack;
    const auto rewardBytes = WriteRewardWord(reward);
    Check(rewardBytes.size() == 22 && rewardBytes[4] == 238 && ReadRewardWord(rewardBytes, rewardBack) && rewardBack.Reward == 0x18a9e8e769ULL &&
              rewardBack.Serial == 3,
          "reward word is 22 bytes with the header");
    reward.Reward = 0;
    Check(!ReadRewardWord(WriteRewardWord(reward), rewardBack), "a quest reward without record is refused");
    Check(KeepClaimFlag(19) && !KeepClaimFlag(15) && KeepPlayerHitFlag(15) && !KeepPlayerHitFlag(9), "claim and player hit flag filters");
    DialogBallot ballot;
    ballot.VoteId = 3;
    ballot.SceneId = 0x1122334455667788ULL;
    ballot.Flags = kBallotTimedByGame;
    ballot.Title = "Judy";
    ballot.Options = {{"Lets go", 0x10}, {"[Leave]", 0}};
    ballot.Voters = {0, 1};
    const auto ballotBytes = WriteDialogBallot(ballot);
    DialogBallot parsedBallot;
    Check(ballotBytes.size() == 5 + 4 + 8 + 1 + (2 + 4) + 1 + (2 + 7 + 4) + (2 + 7 + 4) + 1 + 8 + 1 + 1 && ReadDialogBallot(ballotBytes, parsedBallot) &&
              parsedBallot.VoteId == 3 && parsedBallot.SceneId == ballot.SceneId && parsedBallot.Title == "Judy" && parsedBallot.Options.size() == 2 &&
              parsedBallot.Options[1].Text == "[Leave]" && parsedBallot.Options[0].ChoiceType == 0x10 && parsedBallot.Voters.size() == 2 &&
              parsedBallot.OptionKeys.empty() && parsedBallot.RowIds.empty(),
          "dialog ballot without keys and ids is 5 + 14 + title + options + voters + 2 and round trips");
    ballot.OptionKeys = {0xAA, 0xBB};
    ballot.RowIds = {{0x77, 5}, {0, 0}};
    Check(WriteDialogBallot(ballot).size() == ballotBytes.size() + 16 + 24 && ReadDialogBallot(WriteDialogBallot(ballot), parsedBallot) &&
              parsedBallot.OptionKeys[1] == 0xBB && parsedBallot.RowIds[0].Item == 5 && parsedBallot.RowIds[1].Scene == 0,
          "dialog ballot keys and row ids round trip");
    ballot.RowIds = {{0, 0}, {0, 0}};
    Check(WriteDialogBallot(ballot).size() == ballotBytes.size() + 16, "a ballot with only empty row ids sends none");
    auto badBallot = ballotBytes;
    badBallot[5] = 0;
    badBallot[6] = 0;
    badBallot[7] = 0;
    badBallot[8] = 0;
    Check(!ReadDialogBallot(badBallot, parsedBallot), "a ballot with vote id 0 is refused");
    ballot.Options.clear();
    Check(!ReadDialogBallot(WriteDialogBallot(ballot), parsedBallot), "a ballot without options is refused");
    auto longBallot = ballotBytes;
    longBallot.push_back(0);
    Check(!ReadDialogBallot(longBallot, parsedBallot), "a ballot with bytes left over is refused");

    DialogVote dialogVote;
    Check(WriteDialogVote({3, 1, 0}).size() == 14 && ReadDialogVote(WriteDialogVote({3, 1, 0}), dialogVote) && dialogVote.Option == 1 &&
              ReadDialogVote(WriteDialogVote({3, -1, 0}), dialogVote) && dialogVote.Option == -1 && !ReadDialogVote(WriteDialogVote({0, 1, 0}), dialogVote) &&
              !ReadDialogVote(WriteDialogVote({3, 16, 0}), dialogVote),
          "dialog vote is 14 bytes, option -1..15, vote id not 0");

    DialogTally tally;
    tally.VoteId = 3;
    tally.State = kTallyDecided;
    tally.Option = 1;
    tally.Votes = {{0, 1}, {1, 1}};
    tally.Voters = {0, 1};
    tally.SharedTime = 987654;
    DialogTally parsedTally;
    Check(WriteDialogTally(tally).size() == 5 + 4 + 1 + 1 + 1 + 1 + 2 * 5 + 1 + 2 * 4 + 8 && ReadDialogTally(WriteDialogTally(tally), parsedTally) &&
              parsedTally.State == kTallyDecided && parsedTally.Option == 1 && parsedTally.Votes.size() == 2 && parsedTally.Votes[1].Peer == 1 &&
              parsedTally.Voters.size() == 2 && parsedTally.SharedTime == 987654,
          "dialog tally is 5 + 9 + 5 per vote + 4 per voter + 8 and round trips");
    tally.State = kTallyOpen;
    tally.Option = -1;
    Check(ReadDialogTally(WriteDialogTally(tally), parsedTally) && parsedTally.SharedTime == 0, "an open tally goes out without a shared time");
    tally.State = kTallyDecidedByTime;
    Check(!ReadDialogTally(WriteDialogTally(tally), parsedTally), "a decided tally without an option is refused");
    tally.State = 5;
    tally.Option = 0;
    Check(!ReadDialogTally(WriteDialogTally(tally), parsedTally), "a tally state past 4 is refused");

    DialogLine dialogLine;
    dialogLine.LineId = 0x0102030405060708ULL;
    dialogLine.LineType = 1;
    dialogLine.Seconds = 2.5f;
    dialogLine.Speaker = "Jackie";
    dialogLine.Text = "Hey, choom";
    dialogLine.Language = "en-us";
    DialogLine parsedLine;
    Check(WriteDialogLine(dialogLine).size() == 5 + 8 + 8 + 1 + 4 + (2 + 6) + (2 + 10) + 8 + 8 + (2 + 5) && ReadDialogLine(WriteDialogLine(dialogLine), parsedLine) &&
              parsedLine.LineId == dialogLine.LineId && parsedLine.Seconds == 2.5f && parsedLine.Speaker == "Jackie" && parsedLine.Text == "Hey, choom" &&
              parsedLine.Language == "en-us",
          "dialog line round trips");
    dialogLine.Seconds = 0.0f;
    Check(ReadDialogLine(WriteDialogLine(dialogLine), parsedLine) && parsedLine.Seconds == 4.0f, "a line without seconds goes out with 4 s");
    dialogLine.LineId = 0;
    Check(!ReadDialogLine(WriteDialogLine(dialogLine), parsedLine), "a line with id 0 is refused");
    dialogLine.LineId = 1;
    dialogLine.Text = std::string(1500, 'a');
    Check(ReadDialogLine(WriteDialogLine(dialogLine), parsedLine) && parsedLine.Text.size() == 1024, "a long line is cut to 1024 bytes");

    VoteModeWord modeWord;
    const auto modeBytes = WriteVoteModeWord({1, kVoteModeEveryone, 1});
    Check(modeBytes.size() == 8 && modeBytes[4] == 249 && ReadVoteModeWord(modeBytes, modeWord) && modeWord.Mode == kVoteModeEveryone && modeWord.Flag == 1 &&
              !ReadVoteModeWord(WriteVoteModeWord({1, 2, 0}), modeWord),
          "vote mode word is magic | 249 | sub | mode | flag");

    SceneLineSkip lineSkip;
    lineSkip.Peer = 1;
    lineSkip.SceneId = 0x55;
    lineSkip.Milliseconds = 1200;
    lineSkip.SharedTime = 4444;
    lineSkip.Flags = kSkipPress;
    lineSkip.Places = {{1, 0x99, 2}};
    SceneLineSkip parsedSkip;
    Check(WriteSceneLineSkip(lineSkip).size() == 5 + 4 + 8 + 4 + 8 + 1 + 1 + 11 && ReadSceneLineSkip(WriteSceneLineSkip(lineSkip), parsedSkip) &&
              parsedSkip.Milliseconds == 1200 && parsedSkip.Places.size() == 1 && parsedSkip.Places[0].Nth == 2,
          "scene line skip is 31 + 11 per place and round trips");
    lineSkip.Flags = kSkipPress | kSkipByLine;
    Check(!ReadSceneLineSkip(WriteSceneLineSkip(lineSkip), parsedSkip), "a press skip by line is refused");
    lineSkip.Flags = kSkipPress;
    lineSkip.Places.clear();
    Check(!ReadSceneLineSkip(WriteSceneLineSkip(lineSkip), parsedSkip), "a line skip without places is refused");
    Check(FactIdOf("nightcoop_host_world") == 0xe0a056e8u && FactIdOf("nightcoop_sync_save") == 0xc921e81eu, "fact ids are fnv1a32 of the name");
    QuestFacts factBatch;
    factBatch.Batch = kQuestSnapshotPart;
    factBatch.Number = 4;
    factBatch.Total = 2;
    QuestFact plainFact;
    plainFact.FactId = FactIdOf("q000_done");
    plainFact.Value = -3;
    QuestFact factWithWriter;
    factWithWriter.Table = 2;
    factWithWriter.FactId = 0x12345678;
    factWithWriter.Value = 7;
    factWithWriter.Writer = {true, 0xAABBCCDD, 0x0102};
    factBatch.Facts = {plainFact, factWithWriter};
    const auto factBytes = WriteQuestFacts(factBatch);
    QuestFacts parsedFacts;
    Check(factBytes.size() == 5 + 1 + 4 + 4 + 2 + 9 + (9 + 6) && factBytes[4] == 70 && factBytes[16] == 1 && factBytes[25] == (2 | 0x80),
          "quest facts are 16 bytes + 9 per fact + 6 per writer, the table byte carries the writer bit");
    Check(ReadQuestFacts(factBytes, parsedFacts) && parsedFacts.Batch == kQuestSnapshotPart && parsedFacts.Number == 4 && parsedFacts.Total == 2 &&
              parsedFacts.Facts.size() == 2 && parsedFacts.Facts[0].Value == -3 && !parsedFacts.Facts[0].Writer.Known &&
              parsedFacts.Facts[1].Table == 2 && parsedFacts.Facts[1].Writer.Graph == 0xAABBCCDD && parsedFacts.Facts[1].Writer.Node == 0x0102,
          "quest facts round trip");
    auto factsWithExtra = factBytes;
    factsWithExtra.push_back(0);
    Check(!ReadQuestFacts(factsWithExtra, parsedFacts), "quest facts with a byte left over are refused");
    QuestFacts badBatch = factBatch;
    badBatch.Batch = 5;
    Check(!ReadQuestFacts(WriteQuestFacts(badBatch), parsedFacts), "quest facts batch 5 is refused");
    QuestFacts badTable = factBatch;
    badTable.Facts[0].Table = 11;
    Check(!ReadQuestFacts(WriteQuestFacts(badTable), parsedFacts), "a facts table past 10 is refused");
    badTable.Facts[0].Table = 0;
    Check(!ReadQuestFacts(WriteQuestFacts(badTable), parsedFacts), "facts table 0 is refused");
    const std::vector<uint8_t> tooManyFacts = {0x50, 0x4F, 0x43, 0x4E, 70, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x04};
    Check(!ReadQuestFacts(tooManyFacts, parsedFacts), "more than 1024 facts are refused");
    QuestFacts emptyDelta;
    Check(WriteQuestFacts(emptyDelta).size() == 16 && ReadQuestFacts(WriteQuestFacts(emptyDelta), parsedFacts) && parsedFacts.Facts.empty(),
          "an empty delta is 16 bytes and legal");

    std::vector<QuestFact> manyFacts(2500);
    for (size_t index = 0; index < manyFacts.size(); ++index)
    {
        manyFacts[index].FactId = static_cast<uint32_t>(index + 1);
        manyFacts[index].Value = static_cast<int32_t>(index);
    }
    const auto snapshotMessages = SplitQuestFacts(true, 9, manyFacts);
    Check(snapshotMessages.size() == 5, "a 2500 fact snapshot is begin + 3 parts + end");
    bool snapshotShapeOk = true;
    size_t snapshotFactsSeen = 0;
    for (size_t index = 0; index < snapshotMessages.size(); ++index)
    {
        QuestFacts part;
        const uint8_t wantedBatch = index == 0 ? kQuestSnapshotBegin : index + 1 == snapshotMessages.size() ? kQuestSnapshotEnd : kQuestSnapshotPart;
        snapshotShapeOk = snapshotShapeOk && ReadQuestFacts(snapshotMessages[index], part) && part.Batch == wantedBatch && part.Number == 9 &&
                          part.Total == 2500 && part.Facts.size() <= kQuestMostEntries;
        snapshotFactsSeen += part.Facts.size();
    }
    Check(snapshotShapeOk && snapshotFactsSeen == 2500, "snapshot parts carry number 9, total 2500 and every fact once");
    const auto deltaMessages = SplitQuestFacts(false, 9, manyFacts);
    QuestFacts firstDelta;
    Check(deltaMessages.size() == 3 && ReadQuestFacts(deltaMessages[0], firstDelta) && firstDelta.Batch == kQuestDelta && firstDelta.Number == 0 &&
              firstDelta.Facts.size() == 1024,
          "a big delta goes as batch 1 messages of 1024 with number 0");
    Check(SplitQuestFacts(false, 0, {}).size() == 1 && SplitQuestFacts(true, 3, {}).size() == 2, "an empty delta is one message, an empty snapshot begin + end");

    QuestJournal journalBatch;
    journalBatch.Batch = kQuestDelta;
    journalBatch.TrackedKnown = true;
    journalBatch.Tracked = 0xCAFEBABE;
    QuestJournalEntry loudEntry;
    loudEntry.EntryHash = 0x11111111;
    loudEntry.State = 2;
    loudEntry.Notify = kJournalNotify;
    QuestJournalEntry quietEntry;
    quietEntry.EntryHash = 0x22222222;
    quietEntry.State = 3;
    quietEntry.Notify = kJournalQuiet;
    quietEntry.Writer = {true, 5, 6};
    journalBatch.Entries = {loudEntry, quietEntry};
    const auto journalBytes = WriteQuestJournal(journalBatch);
    QuestJournal parsedJournal;
    Check(journalBytes.size() == 21 + 6 + (6 + 6) && journalBytes[4] == 71 && journalBytes[26] == 2 && journalBytes[32] == (1 | 0x80),
          "quest journal is 21 bytes + 6 per entry + 6 per writer");
    Check(ReadQuestJournal(journalBytes, parsedJournal) && parsedJournal.TrackedKnown && parsedJournal.Tracked == 0xCAFEBABE &&
              parsedJournal.Entries.size() == 2 && parsedJournal.Entries[0].Notify == kJournalNotify && parsedJournal.Entries[1].State == 3 &&
              parsedJournal.Entries[1].Writer.Known && parsedJournal.Entries[1].Writer.Node == 6,
          "quest journal round trip");
    QuestJournal badState = journalBatch;
    badState.Entries[0].State = 5;
    Check(!ReadQuestJournal(WriteQuestJournal(badState), parsedJournal), "a journal state past 4 is refused");
    QuestJournal badNotify = journalBatch;
    badNotify.Entries[0].Notify = 3;
    Check(!ReadQuestJournal(WriteQuestJournal(badNotify), parsedJournal), "a journal notify kind past 2 is refused");
    auto badTracked = WriteQuestJournal(journalBatch);
    badTracked[14] = 2;
    Check(!ReadQuestJournal(badTracked, parsedJournal), "a tracked byte past 1 is refused");
    const auto journalSnapshot = SplitQuestJournal(true, 2, true, 0xCAFEBABE, journalBatch.Entries);
    QuestJournal journalEnd;
    Check(journalSnapshot.size() == 3 && ReadQuestJournal(journalSnapshot.back(), journalEnd) && journalEnd.Batch == kQuestSnapshotEnd &&
              journalEnd.Total == 2 && journalEnd.TrackedKnown && journalEnd.Entries.empty(),
          "a journal snapshot is begin + part + end with the tracked entry");

    uint32_t wantedWorld = 0;
    const auto wantBytes = WriteQuestWant(12);
    Check(wantBytes.size() == 9 && wantBytes[4] == 73 && ReadQuestWant(wantBytes, wantedWorld) && wantedWorld == 12, "quest want is 9 bytes");
    PlayerCarry carry;
    carry.CarryNumber = 3;
    carry.SenderClockMs = 987654321;
    carry.Kind = kCarryHolding;
    carry.PickupAnimation = 2;
    carry.Flags = kCarryActive | kCarryFriendly;
    carry.NetId = (1u << 24) | 7;
    carry.EntityHash = 0x123456789ULL;
    carry.Record = 0x0A00000012345678ULL;
    carry.Position[0] = -1300.5f;
    carry.Look = 0x77;
    const auto carryBytes = WritePlayerCarry(carry);
    PlayerCarry parsedCarry;
    Check(carryBytes.size() == 77 && carryBytes[4] == 92 && carryBytes[0x11] == kCarryHolding && carryBytes[0x14] == (kCarryActive | kCarryFriendly) &&
              carryBytes[0x15] == 7 && carryBytes[0x18] == 1,
          "player carry is 77 bytes with the look and the fields where the dll reads them");
    Check(ReadPlayerCarry(carryBytes, parsedCarry) && parsedCarry.CarryNumber == 3 && parsedCarry.SenderClockMs == 987654321 && parsedCarry.NetId == carry.NetId &&
              parsedCarry.Record == carry.Record && parsedCarry.Position[0] == -1300.5f && parsedCarry.HasLook && parsedCarry.Look == 0x77,
          "player carry round trip");
    carry.HasLook = false;
    Check(WritePlayerCarry(carry).size() == 69 && ReadPlayerCarry(WritePlayerCarry(carry), parsedCarry) && !parsedCarry.HasLook,
          "player carry without the look is 69 bytes");
    carry.Kind = 9;
    Check(!ReadPlayerCarry(WritePlayerCarry(carry), parsedCarry), "a carry kind over 8 is refused");
    carry.Kind = kCarryDrop;
    carry.Flags = 0x80;
    Check(!ReadPlayerCarry(WritePlayerCarry(carry), parsedCarry), "a carry with flag bit 7 is refused");
    carry.Flags = 0;
    carry.NetId = 0;
    carry.EntityHash = 0;
    Check(!ReadPlayerCarry(WritePlayerCarry(carry), parsedCarry), "a carry of nobody is refused");
    Check(CarryKindIsActive(kCarryPickup) && CarryKindIsActive(kCarryAim) && !CarryKindIsActive(kCarryDrop) && !CarryKindIsActive(kCarryThrow),
          "pickup carry jump aim are the active kinds");

    BuildStats build;
    build.Revision = 4;
    build.Stats = {{kBuildStrength, 6.0f}, {kBuildCool, 20.0f}, {kBuildLevel, 50.0f}};
    const auto buildBytes = WriteBuildStats(build);
    BuildStats parsedBuild;
    Check(buildBytes.size() == 11 + 5 * 3 && buildBytes[4] == 141 && buildBytes[9] == kBuildChecksAny && buildBytes[10] == 3 &&
              ReadBuildStats(buildBytes, parsedBuild) && parsedBuild.Revision == 4 && parsedBuild.Stats.size() == 3 && parsedBuild.Stats[1].Index == kBuildCool &&
              parsedBuild.Stats[1].Value == 20.0f,
          "build stats are 11 + 5 per stat and round trip");
    build.Stats.push_back({kBuildCool, 3.0f});
    Check(!ReadBuildStats(WriteBuildStats(build), parsedBuild), "build stats with a stat twice are refused");
    build.Stats.pop_back();
    build.Stats[0].Value = 10001.0f;
    Check(!ReadBuildStats(WriteBuildStats(build), parsedBuild), "a build stat over 10000 is refused");
    build.Stats[0].Value = 1.0f;
    build.Flags = 0x04;
    Check(!ReadBuildStats(WriteBuildStats(build), parsedBuild), "build stats with unknown flags are refused");
    Check(std::string(BuildStatName(kBuildIntelligenceBonus)) == "IntelligenceSkillcheckBonus" && std::string(BuildStatName(kBuildHacking)) == "Hacking",
          "build stat table follows nightcoops order");

    PhoneSync phone;
    phone.Kind = kPhoneCommitted;
    phone.Action = kPhoneActionCall;
    phone.CommitNumber = 12;
    phone.CallId = 5;
    phone.HostClockMs = 4242;
    phone.Contact = "panam";
    phone.CallPhase = 2;
    phone.Flags = kPhoneShowAvatar | kPhoneRejectable;
    const auto phoneBytes = WritePhoneSync(phone);
    PhoneSync parsedPhone;
    Check(phoneBytes.size() == 0x21 + 5 + 4 && phoneBytes[4] == 133 && phoneBytes[0x1F] == 5 && phoneBytes[0x21] == 'p' && ReadPhoneSync(phoneBytes, parsedPhone) &&
              parsedPhone.Contact == "panam" && parsedPhone.CommitNumber == 12 && parsedPhone.CallId == 5 && parsedPhone.HostClockMs == 4242 &&
              parsedPhone.CallPhase == 2 && parsedPhone.Flags == (kPhoneShowAvatar | kPhoneRejectable),
          "phone sync is 0x21 + name + 4 and round trips");
    phone.Contact = "Player";
    Check(!ReadPhoneSync(WritePhoneSync(phone), parsedPhone), "a phone call with the player as contact is refused");
    phone.Contact = std::string(97, 'a');
    Check(!ReadPhoneSync(WritePhoneSync(phone), parsedPhone), "a phone contact over 96 letters is refused");
    phone.Contact = "judy";
    phone.Kind = 5;
    Check(!ReadPhoneSync(WritePhoneSync(phone), parsedPhone), "a phone kind over 4 is refused");

    phone.Kind = kPhoneRequest;
    Check(CheckDroppedMessage(WritePhoneSync(phone)) == DropCheck::LooksRight && CheckDroppedMessage(carryBytes) == DropCheck::LooksRight &&
              CheckDroppedMessage(buildBytes) == DropCheck::LooksRight,
          "the drop readers take what our writers make");
    std::vector<uint8_t> photo = {0x50, 0x4F, 0x43, 0x4E, 209, 1};
    std::vector<uint8_t> blast = {0x50, 0x4F, 0x43, 0x4E, 131, 1};
    blast.resize(6 + 24);
    Check(CheckDroppedMessage(photo) == DropCheck::LooksRight && CheckDroppedMessage(blast) == DropCheck::LooksRight, "photo together and one blast look right");
    photo[5] = 2;
    blast.push_back(0);
    Check(CheckDroppedMessage(photo) == DropCheck::Broken && CheckDroppedMessage(blast) == DropCheck::Broken, "a photo state of 2 and a blast with a byte over are broken");
    std::vector<uint8_t> unknownType = {0x50, 0x4F, 0x43, 0x4E, 251, 0, 0};
    Check(CheckDroppedMessage(unknownType) == DropCheck::NoSizeRule && DroppedMessageName(251) == "PerkDilation 251" && DroppedMessageName(138) == "PlayerFx 138" &&
              DroppedMessageName(250) == "type 250",
          "perk dilation has no size rule and names come from the protocol");

    {
        NpcHandoff handoff;
        handoff.Spawn.NetId = 0x00ABCDEF;
        handoff.Spawn.Record = 0x222a16d43eULL;
        handoff.Spawn.Position[0] = 10.0f;
        handoff.Snapshot.NetId = 0x00ABCDEF;
        handoff.Snapshot.Health = 55.0f;
        handoff.Snapshot.PartnerAttitudes = {{1, 2}};
        const auto handoffBytes = WriteNpcHandoff(handoff);
        NpcHandoff handoffBack;
        Check(handoffBytes.size() == 5 + 62 + 68 + 2 && handoffBytes[4] == 26 && ReadNpcHandoff(handoffBytes, handoffBack) &&
                  handoffBack.Spawn.Record == 0x222a16d43eULL && handoffBack.Snapshot.Health == 55.0f && handoffBack.Snapshot.PartnerAttitudes.size() == 1,
              "npc handoff is a spawn body and a snapshot body");
        handoff.Snapshot.NetId = 7;
        Check(!ReadNpcHandoff(WriteNpcHandoff(handoff), handoffBack), "a handoff with two different net ids is refused");
        NpcHandoffAck ack {0x00ABCDEF, true, 0, 0x1234};
        NpcHandoffAck ackBack;
        const auto ackBytes = WriteNpcHandoffAck(ack);
        Check(ackBytes.size() == 5 + 14 && ReadNpcHandoffAck(ackBytes, ackBack) && ackBack.Accepted && ackBack.Entity == 0x1234, "npc handoff ack is 14 bytes");
        NpcClaim npcClaim {0x00ABCDEF, kClaimAfterHandoff, 0};
        NpcClaim npcClaimBack;
        const auto npcClaimBytes = WriteNpcClaim(npcClaim);
        Check(npcClaimBytes.size() == 5 + 9 && ReadNpcClaim(npcClaimBytes, npcClaimBack) && npcClaimBack.Kind == kClaimAfterHandoff, "npc claim is 9 bytes");
        npcClaim.Kind = 4;
        Check(!ReadNpcClaim(WriteNpcClaim(npcClaim), npcClaimBack), "a claim kind past 3 is refused");
    }

    {
        DeadPerson goner {0x1234567890ABCDEFULL, 0x222a16d43eULL, 0x55, 0x66, {10.0f, -20.0f, 3.5f}, 1};
        bool fullTable = false;
        std::vector<DeadPerson> deadBack;
        const auto deadBytes = WriteNpcDead(true, {goner, goner});
        Check(deadBytes.size() == 5 + 3 + 2 * 44 && deadBytes[4] == 36 && ReadNpcDead(deadBytes, fullTable, deadBack) && fullTable && deadBack.size() == 2 &&
                  deadBack[1].Record == 0x222a16d43eULL && deadBack[1].Position[2] == 3.5f,
              "npc dead table is 3 + 44 per npc");
        uint64_t senderClock = 0;
        const auto defeatBytes = WriteNpcDefeat(777, {goner});
        Check(defeatBytes.size() == 5 + 9 + 45 && defeatBytes[4] == 98 && ReadNpcDefeat(defeatBytes, senderClock, deadBack) && senderClock == 777 &&
                  deadBack.size() == 1 && deadBack[0].Flag == 1 && deadBack[0].MatchKeyB == 0x66,
              "npc defeat is 9 + 45 per npc");
        goner.Position[0] = 1e9f;
        Check(!ReadNpcDead(WriteNpcDead(false, {goner}), fullTable, deadBack), "a dead npc past 100 km is refused");
        auto shortDead = deadBytes;
        shortDead.pop_back();
        Check(!ReadNpcDead(shortDead, fullTable, deadBack), "a dead table with a byte missing is refused");
    }

    {
        GrenadeState grenade;
        grenade.Owner = 2;
        grenade.Number = 9;
        grenade.Stage = kGrenadeFlying;
        grenade.ItemRecord = 0x1122334455ULL;
        grenade.Position[1] = 40.0f;
        grenade.SenderClockMs = 1234;
        bool playerGrenade = false;
        GrenadeState grenadeBack;
        const auto grenadeBytes = WriteGrenade(true, grenade);
        Check(grenadeBytes.size() == 66 && grenadeBytes[4] == 99 && ReadGrenade(grenadeBytes, playerGrenade, grenadeBack) && playerGrenade &&
                  grenadeBack.Stage == kGrenadeFlying && grenadeBack.Position[1] == 40.0f && grenadeBack.Orientation[3] == 1.0f && grenadeBack.SenderClockMs == 1234,
              "player grenade is 66 bytes");
        const auto npcGrenadeBytes = WriteGrenade(false, grenade);
        Check(npcGrenadeBytes[4] == 24 && ReadGrenade(npcGrenadeBytes, playerGrenade, grenadeBack) && !playerGrenade, "npc grenade has the same body");
        grenade.Stage = 5;
        Check(!ReadGrenade(WriteGrenade(true, grenade), playerGrenade, grenadeBack), "a grenade stage past 4 is refused");
        CarRadio radio {0, 0x01000002, 1, 4, 0, 0x55, 0x66};
        CarRadio radioBack;
        const auto radioBytes = WriteCarRadio(radio);
        Check(radioBytes.size() == 5 + 30 && radioBytes[4] == 181 && ReadCarRadio(radioBytes, radioBack) && radioBack.Station == 4 && radioBack.TrackName == 0x66,
              "car radio word is 30 bytes");
        VehicleDamage dents;
        dents.NetId = 0x01000002;
        dents.Flags = 1;
        dents.TireIndex = 2;
        dents.Grid[7] = 0.75f;
        VehicleDamage dentsBack;
        const auto dentsBytes = WriteVehicleDamage(dents);
        Check(dentsBytes.size() == 5 + 126 && dentsBytes[4] == 132 && ReadVehicleDamage(dentsBytes, dentsBack) && dentsBack.TireIndex == 2 && dentsBack.Grid[7] == 0.75f,
              "vehicle damage is 126 bytes");
        dents.Grid[3] = 20.0f;
        Check(!ReadVehicleDamage(WriteVehicleDamage(dents), dentsBack), "a dent past 16 is refused");
        VehicleHandover handover;
        handover.NetId = 0x01000002;
        handover.NewOwner = 2;
        handover.State.NetId = 0x01000002;
        handover.State.Occupants.push_back({0x1234, 1, 2});
        VehicleHandover handoverBack;
        const auto handoverBytes = WriteVehicleHandover(handover);
        Check(handoverBytes.size() == 5 + 9 + 58 + 13 && handoverBytes[4] == 64 && ReadVehicleHandover(handoverBytes, handoverBack) && handoverBack.NewOwner == 2 &&
                  handoverBack.State.Occupants.size() == 1,
              "vehicle handover is 9 bytes and a state body");
        handover.State.NetId = 7;
        Check(!ReadVehicleHandover(WriteVehicleHandover(handover), handoverBack), "a handover with another state net id is refused");
        VehicleHit carHit {0x01000002, 0, 555, {0.0f, 1.0f, 0.0f}, {0.0f, 400.0f, 50.0f}, 12.0f};
        VehicleHit carHitBack;
        const auto carHitBytes = WriteVehicleHit(carHit);
        Check(carHitBytes.size() == 5 + 44 && carHitBytes[4] == 65 && ReadVehicleHit(carHitBytes, carHitBack) && carHitBack.Speed == 12.0f && carHitBack.Impulse[1] == 400.0f,
              "vehicle hit is 44 bytes");
        carHit.Speed = 250.0f;
        Check(!ReadVehicleHit(WriteVehicleHit(carHit), carHitBack), "a car hit faster than 200 m/s is refused");
        PlayerEffect effect {7, 0x1234567890ULL, {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1};
        std::vector<PlayerEffect> effectsBack;
        const auto effectBytes = WritePlayerFx({effect, effect});
        Check(effectBytes.size() == 5 + 1 + 2 * 41 && effectBytes[4] == 138 && ReadPlayerFx(effectBytes, effectsBack) && effectsBack.size() == 2 &&
                  effectsBack[1].Resource == 0x1234567890ULL && effectsBack[1].Flags == 1,
              "player fx is 1 + 41 per effect");
        effect.Orientation[3] = 3.0f;
        Check(!ReadPlayerFx(WritePlayerFx({effect}), effectsBack), "a player fx with a broken orientation is refused");
        uint8_t tagKind = 0;
        std::vector<uint32_t> taggedBack;
        const auto tagBytes = WriteNpcTag(kTagSet, {0x01000005, 0x02000007});
        Check(tagBytes.size() == 5 + 3 + 8 && tagBytes[4] == 220 && ReadNpcTag(tagBytes, tagKind, taggedBack) && tagKind == kTagSet && taggedBack.size() == 2 &&
                  taggedBack[1] == 0x02000007,
              "npc tag is 3 + 4 per net id");
        Check(!ReadNpcTag(WriteNpcTag(4, {1}), tagKind, taggedBack), "a tag kind past 3 is refused");
        SmsReply smsAsk {kSmsReplyAsk, 0, 0, 3, 0x1111, 0x2222};
        SmsReply smsBack;
        const auto smsBytes = WriteSmsReply(smsAsk);
        Check(smsBytes.size() == 20 && smsBytes[4] == 142 && ReadSmsReply(smsBytes, smsBack) && smsBack.EntryHash == 0x1111 && smsBack.ParentHash == 0x2222,
              "sms reply is 20 bytes");
        Check(!ReadSmsReply(WriteSmsReply({kSmsVerdict, 0, 9, 3, 0x1111, 0}), smsBack), "an sms verdict past 6 is refused");
        Blast blast {{1.0f, 2.0f, 3.0f}, 8.0f, 0xABCDULL};
        std::vector<Blast> blastsBack;
        const auto blastBytes = WritePlayerBlast({blast});
        Check(blastBytes.size() == 5 + 1 + 24 && blastBytes[4] == 131 && ReadPlayerBlast(blastBytes, blastsBack) && blastsBack.size() == 1 && blastsBack[0].Radius == 8.0f &&
                  blastsBack[0].Id == 0xABCDULL,
              "player blast is 1 + 24 per blast");
        blast.Radius = 150.0f;
        Check(!ReadPlayerBlast(WritePlayerBlast({blast}), blastsBack), "a blast radius past 100 is refused");
    }

    std::printf("%s\n", FailedChecks ? "some checks failed" : "all game message checks passed");
    return FailedChecks ? 1 : 0;
}
