/*
 grenade messages nightcoops grenade copies and blast pushes rebuilt from the 0.1.2 dll reports
 PlayerGrenade (99) and NpcGrenade (24) evryone reliable (stage 2 unreliable evry 33 ms) 61 bytes: u32 owner (99: sender peer
   + 1. 24: net id of the throwing npc) | u32 grenade number | u8 stage (1 held 2 flying 3 exploded 4 gone) | u64 item record |
   u64 second record (a slot or the newer record) | f32 x3 place | f32 x4 orientation | u64 senders session clock
 PlayerBlast (131) evryone reliable 1 + 24 n bytes: u8 n (1..8) | n x (f32 x3 blast centre | f32 radius 0..100 | u64 id)
 a blast only pushes things around no damage. readers drop not finite floats places past 100 km and bytes left over
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
struct GrenadeState
{
    uint32_t Owner = 0;
    uint32_t Number = 0;
    uint8_t Stage = 0;
    uint64_t ItemRecord = 0;
    uint64_t SecondRecord = 0;
    float Position[3] = {0, 0, 0};
    float Orientation[4] = {0, 0, 0, 1};
    uint64_t SenderClockMs = 0;
};

struct Blast
{
    float Position[3] = {0, 0, 0};
    float Radius = 0;
    uint64_t Id = 0;
};

inline constexpr uint8_t kGrenadeHeld = 1;
inline constexpr uint8_t kGrenadeFlying = 2;
inline constexpr uint8_t kGrenadeExploded = 3;
inline constexpr uint8_t kGrenadeGone = 4;
inline constexpr size_t kMostBlastsAtOnce = 8;

std::vector<uint8_t> WriteGrenade(bool playerGrenade, const GrenadeState& grenade);
bool ReadGrenade(const std::vector<uint8_t>& message, bool& playerGrenade, GrenadeState& grenade);
std::vector<uint8_t> WritePlayerBlast(const std::vector<Blast>& blasts);
bool ReadPlayerBlast(const std::vector<uint8_t>& message, std::vector<Blast>& blasts);
}
