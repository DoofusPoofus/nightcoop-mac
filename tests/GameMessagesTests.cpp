/*
 byte layout checks for the rebuilt game layer messages (Game envelope, PlayerAnim, PlayerAnimWant, PlayerAnimSetup,
 PlayerCustomization, NpcAnim, QuestFacts and friends)
*/
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/RosterMessages.hpp"
#include "net/VehicleMessages.hpp"
#include "net/WorldMessages.hpp"
#include "net/NpcMessages.hpp"
#include "net/QuestMessages.hpp"
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

    std::printf("%s\n", FailedChecks ? "some checks failed" : "all game message checks passed");
    return FailedChecks ? 1 : 0;
}
