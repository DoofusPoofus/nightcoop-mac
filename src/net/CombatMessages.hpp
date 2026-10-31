/*
 npc combat messages rebuilt from the 0.1.2 dll (nightcoops damageclaims) they ride in Game envelopes
 HitDetails: u64 attack record | u8 attack type (0..14 0xFF unknown) | u8 hit type | f32 weapon charge | f32 x3 hit place minus
   the targets place ((0 0 1.3) when past 2.5 m) | f32 x3 hit direction | u8 n (max 24) | n x u32 hit flags. 39 + 4n bytes
 HitClaim (18) attacker -> owner of the npc reliable: u32 claim number | u64 npc net id (low 32 bits used) | f32 percent of its
   max health our game took (refused when not finite or not above 0) | u8 health writes | u8 flags (bit0 lethal) | u64 claim time
   ms (attackers clock) | HitDetails. 26 + HitDetails bytes. a claim only keeps the flags 18 19 22 26 28 29 35 42
 HitResult (19) owner -> claimer reliable 13 bytes: u32 claim number | u8 outcome (1 applied 2 duplicate 3 too old 4 no
   target 5 target dead 6 blocked) | f32 applied percent | f32 npc health percent after
 NpcAction (197) attacker -> owner right before a HitClaim reliable 6 bytes: u8 kind (1) | u32 claim number | u8 hit zone 0..10
 HackSync (140) reliable 23 bytes: u8 kind (1 hack on an npc 2 pvp 3 npc netrunner on a partner body) | u8 stage (1 start 2
   completion) | u8 flags (bit0 the players own hack bit1 visual mirror only with kind 1) | u32 serial (not 0) | u32 hacker peer |
   u32 npc net id (kind 2 has 0) | u64 ObjectAction record (not 0)
 PlayerHit (32) the world an npc hit a partners body in -> that partner reliable: u32 hit number | u32 attacker npc net id (0
   unknown) | u64 senders session clock ms | HitDetails (evry flag but 0 9 10 11 12 13 23 31 34 45 49)
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kHitApplied = 1;
inline constexpr uint8_t kHitDuplicate = 2;
inline constexpr uint8_t kHitTooOld = 3;
inline constexpr uint8_t kHitNoTarget = 4;
inline constexpr uint8_t kHitTargetDead = 5;
inline constexpr uint8_t kHitBlocked = 6;
inline constexpr uint8_t kHitClaimLethal = 0x01;

struct HitDetails
{
    uint64_t AttackRecord = 0;
    uint8_t AttackType = 0xFF;
    uint8_t HitType = 0;
    float WeaponCharge = 0;
    float Offset[3] = {0, 0, 1.3f};
    float Direction[3] = {0, 0, 0};
    std::vector<uint32_t> Flags;
};

struct HitClaim
{
    uint32_t ClaimNumber = 0;
    uint64_t NetId = 0;
    float HealthPercent = 0;
    uint8_t HealthWrites = 1;
    uint8_t Flags = 0;
    uint64_t ClaimTimeMs = 0;
    HitDetails Details;
};

struct HitResult
{
    uint32_t ClaimNumber = 0;
    uint8_t Outcome = kHitApplied;
    float AppliedPercent = 0;
    float HealthAfter = 0;
};

struct NpcAction
{
    uint8_t Kind = 1;
    uint32_t ClaimNumber = 0;
    uint8_t Zone = 0;
};

struct PlayerHit
{
    uint32_t HitNumber = 0;
    uint32_t AttackerNetId = 0;
    uint64_t SessionTimeMs = 0;
    HitDetails Details;
};

inline constexpr uint8_t kHackOnNpc = 1;
inline constexpr uint8_t kHackStageStart = 1;
inline constexpr uint8_t kHackStageCompletion = 2;
inline constexpr uint8_t kHackFlagOwnHack = 0x01;
inline constexpr uint8_t kHackFlagVisual = 0x02;

struct HackSync
{
    uint8_t Kind = kHackOnNpc;
    uint8_t Stage = kHackStageStart;
    uint8_t Flags = kHackFlagOwnHack;
    uint32_t Serial = 0;
    uint32_t HackerPeer = 0;
    uint32_t NetId = 0;
    uint64_t ActionRecord = 0;
};

bool KeepClaimFlag(uint32_t flag);
bool KeepPlayerHitFlag(uint32_t flag);

std::vector<uint8_t> WriteHitClaim(const HitClaim& claim);
bool ReadHitClaim(const std::vector<uint8_t>& message, HitClaim& claim);
std::vector<uint8_t> WriteHitResult(const HitResult& result);
bool ReadHitResult(const std::vector<uint8_t>& message, HitResult& result);
std::vector<uint8_t> WriteNpcAction(const NpcAction& action);
bool ReadNpcAction(const std::vector<uint8_t>& message, NpcAction& action);
std::vector<uint8_t> WriteHackSync(const HackSync& hack);
bool ReadHackSync(const std::vector<uint8_t>& message, HackSync& hack);
std::vector<uint8_t> WritePlayerHit(const PlayerHit& hit);
bool ReadPlayerHit(const std::vector<uint8_t>& message, PlayerHit& hit);
}
