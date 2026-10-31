/*
 who is in the session, rebuilt from the 0.1.2 dll (its lobby / coopmenu side). both ride in Game envelopes, reliable
 PlayerPresence (111), guest -> host when it changed (ping left out of that check) and evry 2 s:
   string name (<= 48) | u8 where (0 unknown 1 menu 2 loading 3 in a world) | u16 ping ms (0xFFFF unknown) | u8 flags
   (bit0 ready, bits 1..3 lobby bools, bit4 add-ons missing, bit6 joining, bit7 in the hosts world) | u8 creation stage
   (< 5, never 3) | u8 pause bits (0..2) | string (<= 64) | string save name (<= 160) | string (<= 32) |
   u8 count (<= 8) + that many strings (<= 32) missing add-ons | string language (<= 8)
 SessionRoster (112), host -> evryone evry 2 s and when it changed, a guest only takes it from peer 0:
   u32 revision | u8 host state (1 lobby 2 loading 3 in its world 4 creating a character 5 new game start) | u32 |
   string world name (<= 160) | string (<= 32) | u8 count (<= 8) + strings (<= 32) | u8 pause flags (bits 0..1) |
   u8 shared new game (< 2) | u8 difficulty (< 4) | u8 new game flags (bits 0..2, bit0 only with bit1) | u8 (< 5) | u64 |
   u8 entries (<= 8), each: u32 peer | string name (<= 48) | u8 where (< 4) | u16 ping | u8 role (< 6, 1 = host) | u8 flags |
   u8 stage (< 5) | u8 pause bits (0..2) | string save name (<= 160) | u8 count (<= 8) + strings (<= 32) | string language (<= 8)
 the names are wat partner markers and pins show. readers drop a message that breaks a limit or has bytes left over, like
 nightcoop does, so the fields we dont fill go out as zeros and empty strings
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kWhereUnknown = 0;
inline constexpr uint8_t kWhereMenu = 1;
inline constexpr uint8_t kWhereLoading = 2;
inline constexpr uint8_t kWhereInWorld = 3;
inline constexpr uint8_t kPresenceInHostWorld = 0x80;
inline constexpr uint8_t kHostStateInWorld = 3;
inline constexpr uint8_t kRoleHost = 1;

struct PlayerPresence
{
    std::string Name;
    uint8_t Where = kWhereUnknown;
    uint16_t PingMs = 0xFFFF;
    uint8_t Flags = 0;
    uint8_t Stage = 0;
    uint8_t PauseBits = 0;
    std::string LobbyText;
    std::string SaveName;
    std::string OtherText;
    std::vector<std::string> MissingAddons;
    std::string Language;
};

struct RosterEntry
{
    uint32_t PeerNumber = 0;
    std::string Name;
    uint8_t Where = kWhereUnknown;
    uint16_t PingMs = 0xFFFF;
    uint8_t Role = 0;
    uint8_t Flags = 0;
    uint8_t Stage = 0;
    uint8_t PauseBits = 0;
    std::string SaveName;
    std::vector<std::string> MissingAddons;
    std::string Language;
};

struct SessionRoster
{
    uint32_t Revision = 0;
    uint8_t HostState = kHostStateInWorld;
    uint32_t HostValue = 0;
    std::string WorldName;
    std::string HostText;
    std::vector<std::string> HostList;
    uint8_t PauseFlags = 0;
    uint8_t SharedNewGame = 0;
    uint8_t Difficulty = 0;
    uint8_t NewGameFlags = 0;
    uint8_t HostByte = 0;
    uint64_t HostTime = 0;
    std::vector<RosterEntry> Entries;
};

std::vector<uint8_t> WritePlayerPresence(const PlayerPresence& presence);
bool ReadPlayerPresence(const std::vector<uint8_t>& message, PlayerPresence& presence);

std::vector<uint8_t> WriteSessionRoster(const SessionRoster& roster);
bool ReadSessionRoster(const std::vector<uint8_t>& message, SessionRoster& roster);
}
