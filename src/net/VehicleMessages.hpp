/*
 shared vehicles, the first four of nightcoops vehicle messages rebuilt from the 0.1.2 dll. all ride in Game envelopes
 netId = creator peer << 24 | a 24 bit counter. the owner is whoever drives, it sends:
 VehicleSpawn (60), reliable, when first shared then evry 5 s, 68 bytes:
   u32 netId | u32 home peer | u64 vehicle record | u64 appearance cname | u64 owner entity id hash | u8 kind (1..5) |
   u8 flags (0x1 static, 0x2 player vehicle, 0x4 crowd, 0x8 bike, 0x10 av, 0x20 dynamic npc car) | u8 x6 door states (< 5) |
   f32 x3 position | f32 x4 orientation i j k r
 VehicleState (61), unreliable, evry frame while moving (at least 4 Hz), once a second standing, 58 + 13 per occupant:
   u32 netId | u64 shared clock ms | f32 x3 position | f32 x4 orientation | f32 x3 velocity | u8 flags (bit0 engine on, bit1
   controller state 4, bit2 parked, bit3 someone at the wheel) | u8 headlights (0..2) | u8 open door mask | u8 health % (<= 100) |
   i8 steering x127 | u8 occupants (<= 8), each u64 seat slot cname | u8 kind (1 player, 2 npc copy) | u32 peer or npc net id
 VehicleDespawn (62), reliable: u32 netId | u8 reason (1 not shared any more, 2 gone at the owner, 3 destroyed)
 VehicleSeat (63), evry peer about its own player: reliable when the seat changes, unreliable evry 200 ms while seated, 40 bytes:
   u32 netId (0 = got out) | u64 seat slot cname (0 when out) | f32 x3 position in the vehicles axes (|v| <= 10 m) | f32 x4 orientation
 readers drop wat nightcoops readers drop: not finite floats, out of range bytes, bytes left over
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

struct VehicleDespawn
{
    uint32_t NetId = 0;
    uint8_t Reason = 1;
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
}
