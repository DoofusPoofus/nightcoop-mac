/*
 shared npcs the first three of nightcoops npc messages rebuilt from the 0.1.2 dll. they ride in Game envelopes
 net id = the sharers peer << 24 | a 24 bit counter it stays with the npc thru handoffs
 NpcSpawn (20) owner -> evryone reliable again evry 625 ms 62 bytes: u32 net id | u64 character record | u64 appearance
   cname | u64 the owners entity id hash | u64 match key a (stub ps spawner id) | u64 match key b (community entry name) |
   f32 x3 position | f32 yaw degrees | u8 high level state (0xFF unknown) | u8 1 = crowd
 NpcSnapshot (21) owner -> evryone mostly unreliable 68 + 2 per partner bytes: u32 net id | f32 x3 position | f32 yaw |
   f32 speed m/s | f32 health % | u8 x8 ai net states (stance upper body high level behavior hit reaction mode defense
   mode phase locomotion mode) | u8 attitude to the owners player | u8 n (up to 7) | n x (u8 peer u8 attitude) | u64 right
   hand item record | u64 owner clock ms | f32 time dilation | u8 dilation flags | u8 flags (0x01 in a workspot 0x02 body
   lost 0x04 calm workspot 0x08 forced 0x10 disposed 0x20 held keepalive 0x40 prelude 0x80 incapacitated) | u64 left
   hand item
 NpcDespawn (22) owner -> evryone reliable 9 bytes: u32 net id | u8 reason (1 stopped sharing 2 body left the owners
   world 3 died) | u32 killer peer (reason 3 else 0xFFFFFFFF)
 readers drop not finite floats positions past 100 km and bytes left over
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
struct NpcSpawn
{
    uint32_t NetId = 0;
    uint64_t Record = 0;
    uint64_t Appearance = 0;
    uint64_t OwnerEntity = 0;
    uint64_t MatchKeyA = 0;
    uint64_t MatchKeyB = 0;
    float Position[3] = {0, 0, 0};
    float Yaw = 0;
    uint8_t HighLevelState = 0xFF;
    uint8_t Crowd = 0;
};

struct NpcAttitude
{
    uint8_t Peer = 0;
    uint8_t Attitude = 1;
};

struct NpcSnapshot
{
    uint32_t NetId = 0;
    float Position[3] = {0, 0, 0};
    float Yaw = 0;
    float Speed = 0;
    float Health = 100;
    uint8_t States[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t Attitude = 1;
    std::vector<NpcAttitude> PartnerAttitudes;
    uint64_t RightHandItem = 0;
    uint64_t OwnerClockMs = 0;
    float TimeDilation = 1.0f;
    uint8_t DilationFlags = 0;
    uint8_t Flags = 0;
    uint64_t LeftHandItem = 0;
};

struct NpcDespawn
{
    uint32_t NetId = 0;
    uint8_t Reason = 1;
    uint32_t Killer = 0xFFFFFFFF;
};

std::vector<uint8_t> WriteNpcSpawn(const NpcSpawn& spawn);
bool ReadNpcSpawn(const std::vector<uint8_t>& message, NpcSpawn& spawn);
std::vector<uint8_t> WriteNpcSnapshot(const NpcSnapshot& snapshot);
bool ReadNpcSnapshot(const std::vector<uint8_t>& message, NpcSnapshot& snapshot);
std::vector<uint8_t> WriteNpcDespawn(const NpcDespawn& despawn);
bool ReadNpcDespawn(const std::vector<uint8_t>& message, NpcDespawn& despawn);
}
