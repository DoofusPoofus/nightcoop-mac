/*
 nightcoop session messages rebuilt from the 0.1.2 dll
 Hello (1): u32 protocol | u64 client id | u16 len + nickname | u64 game version | [u64 install id]
   game version and install id are only read when bytes are left, same as the host does
 Welcome (2): u32 peer number | i64 host session clock ms (guest keeps host clock - own clock as its offset)
 PlayerState (6): 106 bytes in a fixed order, sent unreliable. guests send theirs to the host and the host
   forwards to evry other peer. fields with offset names are ones we havent pinned down yet but the bytes
   still go thru untouched so nothing breaks for windows peers
   Flags comes from the players state machine: bit0 crouch (locomotion Crouch / CrouchSprint / CrouchDodge),
   bit1 sprint, bit2 aiming (upper body Aim), bit6 shooting (a hand in Shoot), bit7 reloading, bit15 silenced weapon.
   the three state bytes after it are the raw gamePSM locomotion / upper body / RightWeapon states
   weapons ride along: right shot count (cumulative, a partner plays the new ones) and right weapon record, then in the
   tail the left weapon record (dual wield only), its shot count and its gamePSM LeftWeapon state
 PlayerCustomization (33): u8 version (1) | u32 | u8 is male | u8 | the look lists. only the gender is read here
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint32_t kProtocolVersion = 143;

struct Hello
{
    uint32_t Protocol = 0;
    uint64_t ClientId = 0;
    std::string Nickname;
    uint64_t GameVersion = 0;
    uint64_t InstallId = 0;
};

struct Welcome
{
    uint32_t PeerNumber = 0;
    int64_t HostClockMs = 0;
};

struct PlayerState
{
    uint32_t PeerNumber = 0;
    float PositionX = 0;
    float PositionY = 0;
    float PositionZ = 0;
    float Yaw = 0;
    float CameraPitch = 0;
    float Speed = 0;
    uint64_t SessionTimeMs = 0;
    uint16_t Flags = 0;
    uint8_t LocomotionState = 0;
    uint8_t UpperBodyState = 0;
    uint8_t WeaponState = 0;
    uint32_t RightShotCount = 0;
    uint64_t RightWeaponRecord = 0;
    float Health = 1.0f;
    float TimeDilation = 1.0f;
    float Unknown48 = 0;
    float Unknown4C = 0;
    float Unknown50 = 0;
    uint8_t Bytes54[14] = {0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 115};
    float AimYawOffset = 0;
    uint8_t Unknown68 = 255;
    uint8_t Unknown69 = 0;
    uint64_t LeftWeaponRecord = 0;
    uint32_t LeftShotCount = 0;
    uint8_t LeftWeaponState = 0;
};

std::vector<uint8_t> WriteHello(uint64_t clientId, const std::string& nickname, uint64_t gameVersion, uint64_t installId);
bool ReadHello(const std::vector<uint8_t>& message, Hello& hello);

std::vector<uint8_t> WriteWelcome(const Welcome& welcome);
bool ReadWelcome(const std::vector<uint8_t>& message, Welcome& welcome);

std::vector<uint8_t> WritePlayerState(const PlayerState& state);
bool ReadPlayerState(const std::vector<uint8_t>& message, PlayerState& state);
bool PlayerStateLooksSane(PlayerState& state);

bool ReadCustomizationGender(const std::vector<uint8_t>& message, bool& isMale);

std::vector<uint8_t> WriteReject(const std::string& reason);
bool ReadReject(const std::vector<uint8_t>& message, std::string& reason);
}
