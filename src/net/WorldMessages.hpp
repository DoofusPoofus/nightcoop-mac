/*
 world sync messages rebuilt from the 0.1.2 dll. they ride in Game envelopes
 LiftSync (86), reliable, exactly 53 bytes: u8 kind (1 presence, 2 departure, 3 request guest -> host, 4 reply host -> guest) |
   u32 peer | u64 lift key (the LiftDevice entity id) | u8 flags (bit0 in the cab, bit1 a quest lift waits for evryone, bit2
   moved by a quest, bit3 cab z valid (needs bit0), bit4 moving, bit6 in step host departure (needs a departure number), bits 5
   and 7 zero) | i8 floor A (from / active, -1 none) | i8 floor B (to / target) | u64 departure time on the session clock |
   f32 x3 sender position | f32 cab z | u8 move mode (0 none, 1 speed m/s, 2 travel time s) | f32 move value (0..10000) |
   u32 departure number | u16 request number | u8 request kind (1 button, 2 quest move, 3 state query) | u8 result (0 state,
   1 the hosts cab goes, 2 already there, 3 on its way, 4 held for evryone, 5 quest move without the asker in it, 6 not the
   hosts lift here, 7 switched off)
 a departure needs floor B and a departure time, a request needs a request number and a request kind
 world handoff (a guest playing in the hosts world), all reliable:
 WorldWant (80) guest -> host: u8 reason (1 we join, 2 we loaded our own save, 3 asked again, 4 host push, 5 story pull) |
   u32 our world number
 WorldFile (81) host -> guest: u32 transfer id (not 0) | u8 kind (1 sav.dat, 2 metadata json) | u8 purpose (the WorldWant
   reason, 4 a host push) | u32 total size (sav.dat up to 128 MiB, metadata up to 4 MiB) | u64 fnv1a64 of the whole file |
   u32 offset | u32 length (up to 32 KiB, inside the file) | that many bytes
 WorldStatus (82) both ways: u8 code (1..9) | u32 value (a transfer id or 0) | string text (up to 512 bytes)
 WorldReload (95) host -> guests: u8 kind (1..5) | u32 value | string text (up to 128 bytes)
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kLiftPresence = 1;
inline constexpr uint8_t kLiftDeparture = 2;
inline constexpr uint8_t kLiftRequest = 3;
inline constexpr uint8_t kLiftReply = 4;

struct LiftSync
{
    uint8_t Kind = kLiftPresence;
    uint32_t Peer = 0;
    uint64_t LiftKey = 0;
    uint8_t Flags = 0;
    int8_t FloorFrom = -1;
    int8_t FloorTo = -1;
    uint64_t DepartureMs = 0;
    float Position[3] = {0, 0, 0};
    float CabZ = 0;
    uint8_t MoveMode = 0;
    float MoveValue = 0;
    uint32_t DepartureNumber = 0;
    uint16_t RequestNumber = 0;
    uint8_t RequestKind = 0;
    uint8_t Result = 0;
};

inline constexpr uint8_t kWorldFileSave = 1;
inline constexpr uint8_t kWorldFileMetadata = 2;
inline constexpr uint32_t kWorldChunkBytes = 0x8000;

struct WorldWant
{
    uint8_t Reason = 1;
    uint32_t WorldNumber = 0;
};

struct WorldFile
{
    uint32_t TransferId = 0;
    uint8_t Kind = kWorldFileSave;
    uint8_t Purpose = 1;
    uint32_t TotalSize = 0;
    uint64_t Hash = 0;
    uint32_t Offset = 0;
    std::vector<uint8_t> Bytes;
};

struct WorldStatus
{
    uint8_t Code = 1;
    uint32_t Value = 0;
    std::string Text;
};

uint64_t WorldFileHash(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> WriteWorldWant(const WorldWant& want);
bool ReadWorldWant(const std::vector<uint8_t>& message, WorldWant& want);
std::vector<uint8_t> WriteWorldFile(const WorldFile& file);
bool ReadWorldFile(const std::vector<uint8_t>& message, WorldFile& file);
std::vector<uint8_t> WriteWorldStatus(const WorldStatus& status);
bool ReadWorldStatus(const std::vector<uint8_t>& message, WorldStatus& status);
std::vector<uint8_t> WriteWorldReload(const WorldStatus& reload);
bool ReadWorldReload(const std::vector<uint8_t>& message, WorldStatus& reload);

std::vector<uint8_t> WriteLiftSync(const LiftSync& lift);
bool ReadLiftSync(const std::vector<uint8_t>& message, LiftSync& lift);
}
