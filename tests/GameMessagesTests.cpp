/*
 byte layout checks for the rebuilt game layer messages (Game envelope, PlayerAnim, PlayerAnimWant, PlayerAnimSetup,
 PlayerCustomization, NpcAnim, DoorSync, the dialog votes and friends)
*/
#include "net/DoorMessages.hpp"
#include "net/DialogMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/RosterMessages.hpp"
#include "net/VehicleMessages.hpp"
#include "net/WorldMessages.hpp"
#include "net/NpcMessages.hpp"
#include "net/CombatMessages.hpp"
#include "net/SessionMessages.hpp"

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

    std::printf("%s\n", FailedChecks ? "some checks failed" : "all game message checks passed");
    return FailedChecks ? 1 : 0;
}
