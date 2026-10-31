/*
 byte layout checks for the rebuilt game layer messages (Game envelope, PlayerAnim, PlayerAnimWant, PlayerAnimSetup,
 PlayerCustomization, NpcAnim, the device messages and friends)
*/
#include "net/DeviceMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/RosterMessages.hpp"
#include "net/VehicleMessages.hpp"
#include "net/WorldMessages.hpp"
#include "net/NpcMessages.hpp"
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

    std::printf("%s\n", FailedChecks ? "some checks failed" : "all game message checks passed");
    return FailedChecks ? 1 : 0;
}
