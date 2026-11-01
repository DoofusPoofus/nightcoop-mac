/*
 shared vehicles the first four of nightcoops vehicle messages rebuilt from the 0.1.2 dll. all ride in Game envelopes
 netId = creator peer << 24 | a 24 bit counter. the owner is whoever drives it sends:
 VehicleSpawn (60) reliable when first shared then evry 5 s 68 bytes:
   u32 netId | u32 home peer | u64 vehicle record | u64 appearance cname | u64 owner entity id hash | u8 kind (1..5) |
   u8 flags (0x1 static 0x2 player vehicle 0x4 crowd 0x8 bike 0x10 av 0x20 dynamic npc car) | u8 x6 door states (< 5) |
   f32 x3 position | f32 x4 orientation i j k r
 VehicleState (61) unreliable evry frame while moving (at least 4 Hz) once a second standing 58 + 13 per occupant:
   u32 netId | u64 shared clock ms | f32 x3 position | f32 x4 orientation | f32 x3 velocity | u8 flags (bit0 engine on bit1
   controller state 4 bit2 parked bit3 someone at the wheel) | u8 headlights (0..2) | u8 open door mask | u8 health % (<= 100) |
   i8 steering x127 | u8 occupants (<= 8) each u64 seat slot cname | u8 kind (1 player 2 npc copy) | u32 peer or npc net id
 VehicleDespawn (62) reliable: u32 netId | u8 reason (1 not shared any more 2 gone at the owner 3 destroyed)
 VehicleSeat (63) evry peer about its own player: reliable when the seat changes unreliable evry 200 ms while seated 40 bytes:
   u32 netId (0 = got out) | u64 seat slot cname (0 when out) | f32 x3 position in the vehicles axes (|v| <= 10 m) | f32 x4 orientation
 VehicleHandover (64) reliable to evryone: u32 netId | u32 new owner peer | u8 reason (1 took the wheel 2 back to its home peer
   3 refused) | a VehicleState body of the same netId
 VehicleDamage (132) owner -> evryone 126 bytes: u32 netId | u8 flags (bit0 a flat tire) | u8 tire index (<= 7 with bit0) | f32 x30
   destruction grid (2 layers of 15 each -0.01..16)
 VehicleSceneSeat (123) evry peer about its own player sitting in its own copy of a scene car (not shared) reliable on change
   unreliable evry 200 ms a zero record when it got out 65 bytes: u64 its entity id | u64 car record | u64 seat slot | f32 x3 our
   place in the cars axes (|v| <= 10) | f32 x4 orientation there | f32 x3 the cars world place | u8 flags (bit0 av bit1 a quest
   ride bit2 seated by a scene)
 NpcCarHit (213) npc owner -> the car owner reliable 69 bytes: u32 car netId | u32 npc netId | u64 shared clock ms | f32 x3 car
   velocity | f32 x3 prey velocity | f32 x3 impulse place | f32 x3 impulse | f32 damage fraction (0..1.5) | u8 flags (1 lethal 2 died
   after 4 crowd 8 police 0x10 mechanical)
 CarsWord (181) radio kinds 0 / 1 reliable to one peer 30 bytes: u8 kind (0 owner -> a passenger: the radio state 1 passenger ->
   owner: set this station) | u32 netId | u8 radio on | u32 station index (< 0x10000) | u32 spare | u64 station name | u64 track name
 VehicleHit (65) car owner -> the peer whose body its car hit reliable 44 bytes: u32 netId | u32 hit peer | u64 shared clock ms |
   f32 x3 hit direction (|v|^2 <= 1e4) | f32 x3 separation impulse (|v|^2 <= 1e6) | f32 car speed m/s (0..200)
 readers drop wat nightcoops readers drop: not finite floats out of range bytes bytes left over
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
struct VehicleSpawn
{
    uint32_t NetId = 0;
    uint32_t HomePeer = 0;
    uint64_t Record = 0;
    uint64_t Appearance = 0;
    uint64_t OwnerEntity = 0;
    uint8_t Kind = 1;
    uint8_t Flags = 0;
    uint8_t Doors[6] = {0, 0, 0, 0, 0, 0};
    float Position[3] = {0, 0, 0};
    float Orientation[4] = {0, 0, 0, 1};
};

struct VehicleOccupant
{
    uint64_t Slot = 0;
    uint8_t Kind = 1;
    uint32_t Id = 0;
};

struct VehicleState
{
    uint32_t NetId = 0;
    uint64_t StampMs = 0;
    float Position[3] = {0, 0, 0};
    float Orientation[4] = {0, 0, 0, 1};
    float Velocity[3] = {0, 0, 0};
    uint8_t Flags = 0;
    uint8_t Headlights = 0;
    uint8_t OpenDoors = 0;
    uint8_t Health = 100;
    int8_t Steering = 0;
    std::vector<VehicleOccupant> Occupants;
};

struct VehicleSceneSeat
{
    uint64_t Entity = 0;
    uint64_t Record = 0;
    uint64_t Slot = 0;
    float Offset[3] = {0, 0, 0};
    float Orientation[4] = {0, 0, 0, 1};
    float CarPosition[3] = {0, 0, 0};
    uint8_t Flags = 0;
};

struct NpcCarHit
{
    uint32_t CarNetId = 0;
    uint32_t NpcNetId = 0;
    uint64_t ClockMs = 0;
    float CarVelocity[3] = {0, 0, 0};
    float PreyVelocity[3] = {0, 0, 0};
    float ImpulsePlace[3] = {0, 0, 0};
    float Impulse[3] = {0, 0, 0};
    float DamageFraction = 0;
    uint8_t Flags = 0;
};

struct CarRadio
{
    uint8_t Kind = 0;
    uint32_t NetId = 0;
    uint8_t On = 0;
    uint32_t Station = 0;
    uint32_t Spare = 0;
    uint64_t StationName = 0;
    uint64_t TrackName = 0;
};

struct VehicleDamage
{
    uint32_t NetId = 0;
    uint8_t Flags = 0;
    uint8_t TireIndex = 0;
    float Grid[30] = {};
};

struct VehicleHandover
{
    uint32_t NetId = 0;
    uint32_t NewOwner = 0;
    uint8_t Reason = 1;
    VehicleState State;
};

inline constexpr uint8_t kHandoverTookWheel = 1;
inline constexpr uint8_t kHandoverReturnHome = 2;
inline constexpr uint8_t kHandoverRefused = 3;

struct VehicleDespawn
{
    uint32_t NetId = 0;
    uint8_t Reason = 1;
};

struct VehicleHit
{
    uint32_t NetId = 0;
    uint32_t HitPeer = 0;
    uint64_t ClockMs = 0;
    float Direction[3] = {0, 0, 0};
    float Impulse[3] = {0, 0, 0};
    float Speed = 0;
};

struct VehicleSeat
{
    uint32_t NetId = 0;
    uint64_t Slot = 0;
    float Position[3] = {0, 0, 0};
    float Orientation[4] = {0, 0, 0, 1};
};

inline constexpr uint8_t kVehicleEngineOn = 0x01;
inline constexpr uint8_t kVehicleSomeoneDrives = 0x08;
inline constexpr uint8_t kOccupantPlayer = 1;

std::vector<uint8_t> WriteVehicleSpawn(const VehicleSpawn& spawn);
bool ReadVehicleSpawn(const std::vector<uint8_t>& message, VehicleSpawn& spawn);
std::vector<uint8_t> WriteVehicleState(const VehicleState& state);
bool ReadVehicleState(const std::vector<uint8_t>& message, VehicleState& state);
std::vector<uint8_t> WriteVehicleDespawn(const VehicleDespawn& despawn);
bool ReadVehicleDespawn(const std::vector<uint8_t>& message, VehicleDespawn& despawn);
std::vector<uint8_t> WriteVehicleSeat(const VehicleSeat& seat);
bool ReadVehicleSeat(const std::vector<uint8_t>& message, VehicleSeat& seat);
std::vector<uint8_t> WriteVehicleHit(const VehicleHit& hit);
std::vector<uint8_t> WriteVehicleHandover(const VehicleHandover& handover);
std::vector<uint8_t> WriteVehicleDamage(const VehicleDamage& damage);
std::vector<uint8_t> WriteCarRadio(const CarRadio& radio);
std::vector<uint8_t> WriteNpcCarHit(const NpcCarHit& hit);
bool ReadNpcCarHit(const std::vector<uint8_t>& message, NpcCarHit& hit);
std::vector<uint8_t> WriteVehicleSceneSeat(const VehicleSceneSeat& seat);
bool ReadVehicleSceneSeat(const std::vector<uint8_t>& message, VehicleSceneSeat& seat);
bool ReadCarRadio(const std::vector<uint8_t>& message, CarRadio& radio);
bool ReadVehicleDamage(const std::vector<uint8_t>& message, VehicleDamage& damage);
bool ReadVehicleHandover(const std::vector<uint8_t>& message, VehicleHandover& handover);
bool ReadVehicleHit(const std::vector<uint8_t>& message, VehicleHit& hit);
}
