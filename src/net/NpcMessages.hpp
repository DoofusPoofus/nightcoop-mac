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
 NpcHandoff (26) old owner -> one peer reliable: a NpcSpawn body then a NpcSnapshot body of the same net id (62 + 68 + 2n)
 NpcHandoffAck (27) receiver -> offering peer reliable 14 bytes: u32 net id | u8 1 accepted 0 refused | u8 reason (1 no body) |
   u64 the receivers entity id of the body it took
 NpcClaim (28) new owner -> evryone reliable 9 bytes: u32 net id | u8 kind (1 orphan 2 by the claim rules 3 after a handoff) | u32
   the peer it was taken from
 NpcDead (36) host -> evryone in its world reliable 3 + 44 n bytes: u8 1 full table 0 only new ones | u16 n (up to 1024) | n x
   (u64 the hosts entity id | u64 record | u64 match key a | u64 match key b | f32 x3 place) full table evry 30 s
 NpcDefeat (98) guest -> host reliable 9 + 45 n bytes: u64 senders session clock | u8 n (up to 32) | n x (the same 44 bytes |
   u8 flag)
 NpcTag (220) reliable to evryone or one peer 3 + 4 n bytes: u8 kind (1 tagged 2 untagged 3 the whole list for a partner that
   just came into the world) | u16 n (up to 1024) | n x u32 net id
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

struct NpcHandoff
{
    NpcSpawn Spawn;
    NpcSnapshot Snapshot;
};

struct NpcHandoffAck
{
    uint32_t NetId = 0;
    bool Accepted = false;
    uint8_t Reason = 0;
    uint64_t Entity = 0;
};

inline constexpr uint8_t kClaimOrphan = 1;
inline constexpr uint8_t kClaimByRules = 2;
inline constexpr uint8_t kClaimAfterHandoff = 3;
inline constexpr uint8_t kHandoffNoBody = 1;

struct DeadPerson
{
    uint64_t Entity = 0;
    uint64_t Record = 0;
    uint64_t MatchKeyA = 0;
    uint64_t MatchKeyB = 0;
    float Position[3] = {0, 0, 0};
    uint8_t Flag = 0;
};

inline constexpr size_t kMostDeadInTable = 1024;
inline constexpr size_t kMostDefeatsAtOnce = 32;

struct NpcClaim
{
    uint32_t NetId = 0;
    uint8_t Kind = kClaimAfterHandoff;
    uint32_t FromPeer = 0;
};

std::vector<uint8_t> WriteNpcSpawn(const NpcSpawn& spawn);
bool ReadNpcSpawn(const std::vector<uint8_t>& message, NpcSpawn& spawn);
std::vector<uint8_t> WriteNpcSnapshot(const NpcSnapshot& snapshot);
bool ReadNpcSnapshot(const std::vector<uint8_t>& message, NpcSnapshot& snapshot);
std::vector<uint8_t> WriteNpcDespawn(const NpcDespawn& despawn);
bool ReadNpcDespawn(const std::vector<uint8_t>& message, NpcDespawn& despawn);
std::vector<uint8_t> WriteNpcHandoff(const NpcHandoff& handoff);
bool ReadNpcHandoff(const std::vector<uint8_t>& message, NpcHandoff& handoff);
std::vector<uint8_t> WriteNpcHandoffAck(const NpcHandoffAck& ack);
bool ReadNpcHandoffAck(const std::vector<uint8_t>& message, NpcHandoffAck& ack);
std::vector<uint8_t> WriteNpcClaim(const NpcClaim& claim);
bool ReadNpcClaim(const std::vector<uint8_t>& message, NpcClaim& claim);
std::vector<uint8_t> WriteNpcDead(bool fullTable, const std::vector<DeadPerson>& people);
bool ReadNpcDead(const std::vector<uint8_t>& message, bool& fullTable, std::vector<DeadPerson>& people);
std::vector<uint8_t> WriteNpcDefeat(uint64_t senderClockMs, const std::vector<DeadPerson>& people);
bool ReadNpcDefeat(const std::vector<uint8_t>& message, uint64_t& senderClockMs, std::vector<DeadPerson>& people);
inline constexpr uint8_t kTagSet = 1;
inline constexpr uint8_t kTagCleared = 2;
inline constexpr uint8_t kTagList = 3;

std::vector<uint8_t> WriteNpcTag(uint8_t kind, const std::vector<uint32_t>& netIds);
bool ReadNpcTag(const std::vector<uint8_t>& message, uint8_t& kind, std::vector<uint32_t>& netIds);
}
