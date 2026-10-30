/*
 nightcoop game layer messages rebuilt from the 0.1.2 dll
 Game (25) is the envelope evry game message rides in: i32 to peer (-1 evryone) | u32 from peer | u8 reliable | inner message.
   a guest only talks to the host, the host takes it itself, forwards it to the one it is for or to evryone but the sender,
   and checks that the from peer is really the guest that sent it
 PlayerAnim (29) is one batch of anim graph inputs:
   u32 | u8 flags (bit0 full state, bit1 entries carry an extra byte) | u64 | u32 | u16 count (max 4096) | entries
   entry: u8 slot | u8 kind | u64 name | payload | [u8 extra, always for features, else only with flag bit1]
   kinds: 0 feature (u64 type | u8 fields (max 48) | per field u64 name, u8 size (max 32), bytes)
          1 float, 7 wrapper weight (f32) | 2 int, 3 bool (i32) | 4 vector, 5 quaternion (4 x f32) | 6 event (nothing)
          8 attach, 9 detach (u64 not zero, u64)
 PlayerAnimWant (30): u8 | u8, a partner asks for our full anim state (sent to one peer, once a second at most)
 PlayerAnimSetup (34): u64 rig | u16 sets (max 512) | the sets. only rig and count are read here
 the u32 / u64 / u32 batch header words are passed along as they came, wat they mean is not pinned down yet
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr int32_t kEveryPeer = -1;

struct GameEnvelope
{
    int32_t ToPeer = kEveryPeer;
    uint32_t FromPeer = 0;
    bool Reliable = true;
    std::vector<uint8_t> Inner;
};

enum class AnimKind : uint8_t
{
    Feature = 0,
    Float = 1,
    Int = 2,
    Bool = 3,
    Vector = 4,
    Quaternion = 5,
    Event = 6,
    Wrapper = 7,
    Attach = 8,
    Detach = 9,
};

struct AnimFeatureField
{
    uint64_t Name = 0;
    std::vector<uint8_t> Bytes;
};

struct AnimEntry
{
    uint8_t Slot = 0;
    AnimKind Kind = AnimKind::Event;
    uint64_t Name = 0;
    float Values[4] = {0, 0, 0, 0};
    int32_t IntValue = 0;
    uint64_t FeatureType = 0;
    std::vector<AnimFeatureField> Fields;
    uint64_t AttachFirst = 0;
    uint64_t AttachSecond = 0;
    uint8_t Extra = 0;
};

struct AnimBatch
{
    uint32_t Unknown00 = 0;
    bool FullState = false;
    bool EntriesHaveExtra = false;
    uint64_t Unknown08 = 0;
    uint32_t Unknown10 = 0;
    std::vector<AnimEntry> Entries;
};

struct AnimWant
{
    bool First = true;
    bool Second = true;
};

struct AnimSetup
{
    uint64_t Rig = 0;
    uint16_t SetCount = 0;
};

uint8_t MessageTypeOf(const std::vector<uint8_t>& message);

std::vector<uint8_t> WriteGameEnvelope(const GameEnvelope& envelope);
bool ReadGameEnvelope(const std::vector<uint8_t>& message, GameEnvelope& envelope);

std::vector<uint8_t> WriteAnimBatch(const AnimBatch& batch);
bool ReadAnimBatch(const std::vector<uint8_t>& message, AnimBatch& batch);

std::vector<uint8_t> WriteAnimWant(const AnimWant& want);
bool ReadAnimWant(const std::vector<uint8_t>& message, AnimWant& want);

bool ReadAnimSetup(const std::vector<uint8_t>& message, AnimSetup& setup);
}
