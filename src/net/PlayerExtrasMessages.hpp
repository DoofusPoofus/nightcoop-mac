/*
 player extras messages rebuilt from the 0.1.2 dll. they ride in Game envelopes all reliable
 PlayerCarry (92) carrier -> evryone 69 bytes or 77 with the look tail: u32 carry number (new number = new carry) | u64 the
   carriers session clock | u8 kind (0 none 1 pickup 2 carry 3 drop 4 release 5 dispose 6 jump 7 aim 8 throw) | u8 spare (0
   up to 8) | u8 pickup animation (up to 16) | u8 flags (bit0 both hands bit1 instant bit2 fast mode bit3 carry active
   bit4 friendly carry bit5 was thrown bit6 quest carry in the arms bit7 must be clear) | u32 net id of the carried npc
   (0 none) | u64 its entity id hash in the carriers world | u64 its character record | u64 x2 community key | f32 x3 where it
   is (0 0 0 unknown) | u64 appearance name of it (optional). net id or entity id must be set
 BuildStats (141) evryone -> evryone at a change and evry 5 s: u32 revision | u8 flags (bit0 team build checks bit1 native
   quest checks other bits clear) | u8 n (up to 32) | n x (u8 stat index each once | f32 value 0..10000). stat index =
   nightcoops own table: 0 Strength 1 Reflexes 2 TechnicalAbility 3 Intelligence 4 Cool 5 StrengthSkillcheckBonus
   6 TechnicalAbilitySkillcheckBonus 7 IntelligenceSkillcheckBonus 8 Level 9 StreetCred 10 Hacking
 PhoneSync (133): u8 kind (1 request guest -> host 2 committed host -> evryone 3 the hosts call state again 4 refused) |
   u8 action (1 call phase 2 pickup answer) | u32 the asking peer | u32 request number | u32 commit number | u32 call id |
   u64 the hosts session clock | string contact name (1..96 printable ascii never player) | u8 call phase (1 incoming
   2 start 3 end) | u8 call mode | u8 visuals | u8 flags (bit0 player calling bit1 player triggered bit2 rejectable
   bit3 show avatar bit4 audio call bit5 should be rejected)
 PlayerFx (138) evryone -> evryone: u8 n (1..24) | n x (u32 effect number (not 0) | u64 effect resource path hash (not 0) | f32 x3
   world place | f32 x4 orientation i j k r (norm^2 0.5..1.5) | u8 flags (bit0 only: the third SpawnEffect argument))
 readers drop wrong sizes broken limits and not finite floats like the dll does
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kCarryNone = 0;
inline constexpr uint8_t kCarryPickup = 1;
inline constexpr uint8_t kCarryHolding = 2;
inline constexpr uint8_t kCarryDrop = 3;
inline constexpr uint8_t kCarryRelease = 4;
inline constexpr uint8_t kCarryDispose = 5;
inline constexpr uint8_t kCarryJump = 6;
inline constexpr uint8_t kCarryAim = 7;
inline constexpr uint8_t kCarryThrow = 8;

inline constexpr uint8_t kCarryBothHands = 0x01;
inline constexpr uint8_t kCarryInstant = 0x02;
inline constexpr uint8_t kCarryFastMode = 0x04;
inline constexpr uint8_t kCarryActive = 0x08;
inline constexpr uint8_t kCarryFriendly = 0x10;
inline constexpr uint8_t kCarryThrown = 0x20;
inline constexpr uint8_t kCarryInArms = 0x40;

struct PlayerCarry
{
    uint32_t CarryNumber = 0;
    uint64_t SenderClockMs = 0;
    uint8_t Kind = kCarryNone;
    uint8_t Spare = 0;
    uint8_t PickupAnimation = 0;
    uint8_t Flags = 0;
    uint32_t NetId = 0;
    uint64_t EntityHash = 0;
    uint64_t Record = 0;
    uint64_t CommunityKeyLow = 0;
    uint64_t CommunityKeyHigh = 0;
    float Position[3] = {0, 0, 0};
    bool HasLook = true;
    uint64_t Look = 0;
};

bool CarryKindIsActive(uint8_t kind);
std::vector<uint8_t> WritePlayerCarry(const PlayerCarry& carry);
bool ReadPlayerCarry(const std::vector<uint8_t>& message, PlayerCarry& carry);

inline constexpr uint8_t kBuildChecksAny = 0x01;
inline constexpr uint8_t kBuildChecksNative = 0x02;
inline constexpr size_t kMostBuildStats = 32;

enum BuildStatIndex : uint8_t
{
    kBuildStrength = 0,
    kBuildReflexes = 1,
    kBuildTechnicalAbility = 2,
    kBuildIntelligence = 3,
    kBuildCool = 4,
    kBuildStrengthBonus = 5,
    kBuildTechnicalAbilityBonus = 6,
    kBuildIntelligenceBonus = 7,
    kBuildLevel = 8,
    kBuildStreetCred = 9,
    kBuildHacking = 10,
    kBuildStatCount = 11
};

const char* BuildStatName(uint8_t index);

struct BuildStat
{
    uint8_t Index = 0;
    float Value = 0;
};

struct BuildStats
{
    uint32_t Revision = 0;
    uint8_t Flags = kBuildChecksAny;
    std::vector<BuildStat> Stats;
};

std::vector<uint8_t> WriteBuildStats(const BuildStats& stats);
bool ReadBuildStats(const std::vector<uint8_t>& message, BuildStats& stats);

inline constexpr uint8_t kPhoneRequest = 1;
inline constexpr uint8_t kPhoneCommitted = 2;
inline constexpr uint8_t kPhoneHostState = 3;
inline constexpr uint8_t kPhoneRefused = 4;
inline constexpr uint8_t kPhoneActionCall = 1;
inline constexpr uint8_t kPhoneActionPickup = 2;

inline constexpr uint8_t kPhonePlayerCalling = 0x01;
inline constexpr uint8_t kPhonePlayerTriggered = 0x02;
inline constexpr uint8_t kPhoneRejectable = 0x04;
inline constexpr uint8_t kPhoneShowAvatar = 0x08;
inline constexpr uint8_t kPhoneAudioCall = 0x10;
inline constexpr uint8_t kPhoneShouldBeRejected = 0x20;

struct PhoneSync
{
    uint8_t Kind = kPhoneCommitted;
    uint8_t Action = kPhoneActionCall;
    uint32_t AskingPeer = 0;
    uint32_t RequestNumber = 0;
    uint32_t CommitNumber = 0;
    uint32_t CallId = 0;
    uint64_t HostClockMs = 0;
    std::string Contact;
    uint8_t CallPhase = 1;
    uint8_t CallMode = 0;
    uint8_t Visuals = 0;
    uint8_t Flags = 0;
};

bool PhoneContactNameOk(const std::string& contact);
std::vector<uint8_t> WritePhoneSync(const PhoneSync& phone);
bool ReadPhoneSync(const std::vector<uint8_t>& message, PhoneSync& phone);
struct PlayerEffect
{
    uint32_t Number = 0;
    uint64_t Resource = 0;
    float Position[3] = {0, 0, 0};
    float Orientation[4] = {0, 0, 0, 1};
    uint8_t Flags = 0;
};

inline constexpr size_t kMostEffectsAtOnce = 24;

std::vector<uint8_t> WritePlayerFx(const std::vector<PlayerEffect>& effects);
bool ReadPlayerFx(const std::vector<uint8_t>& message, std::vector<PlayerEffect>& effects);
}
