/*
 DoorSync (88) rebuilt from the 0.1.2 dll. it rides in Game envelopes and is always reliable
 exactly 46 bytes (5 header + 41 body) little endian no padding:
   u8 kind (1 guest -> host i saw this change here / 2 host -> guests the shared state) | u32 peer (kind 1 the sender / kind 2
   the peer whose act made this state) | u64 door key (the door entity id hash not 0) | u8 state (bit0 open bit1 locked bit2
   sealed) | u8 changed mask (same bits / 0 in a plain answer) | u8 cause (0 a player or an npc 1 closed by itself 2 the quest
   3 kept open for a player at it) | u16 quest ops (bit1 ForceOpen 2 ForceClose 3 ForceCloseImmediate 4 ForceOpenScene 5
   ForceCloseScene 6 ForceLock 7 ForceUnlock 8 ForceSeal 9 ForceUnseal 10 another quest action / only with cause 2) | u8 side
   of the actors player at the door (0 outside 1 side one 2 side two) | u64 version (kind 2 the hosts version not 0 / kind 1
   the version the guest last had) | u16 request number (kind 1 not 0 / kind 2 echoed only to the asker) | f32 x3 the actors
   player position (finite and within 100 km)
 the reader drops evry other size and evry field out of these ranges like nightcoop does
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kDoorTell = 1;
inline constexpr uint8_t kDoorShared = 2;

inline constexpr uint8_t kDoorOpen = 0x01;
inline constexpr uint8_t kDoorLocked = 0x02;
inline constexpr uint8_t kDoorSealed = 0x04;
inline constexpr uint8_t kDoorStateBits = 0x07;

inline constexpr uint8_t kDoorCausePlayerOrNpc = 0;
inline constexpr uint8_t kDoorCauseClosedByItself = 1;
inline constexpr uint8_t kDoorCauseQuest = 2;
inline constexpr uint8_t kDoorCauseKeptOpen = 3;

inline constexpr uint8_t kDoorOpForceOpen = 1;
inline constexpr uint8_t kDoorOpForceClose = 2;
inline constexpr uint8_t kDoorOpForceCloseImmediate = 3;
inline constexpr uint8_t kDoorOpForceOpenScene = 4;
inline constexpr uint8_t kDoorOpForceCloseScene = 5;
inline constexpr uint8_t kDoorOpForceLock = 6;
inline constexpr uint8_t kDoorOpForceUnlock = 7;
inline constexpr uint8_t kDoorOpForceSeal = 8;
inline constexpr uint8_t kDoorOpForceUnseal = 9;
inline constexpr uint8_t kDoorOpOtherQuestAction = 10;

struct DoorSync
{
    uint8_t Kind = kDoorTell;
    uint32_t Peer = 0;
    uint64_t DoorKey = 0;
    uint8_t State = 0;
    uint8_t Changed = 0;
    uint8_t Cause = kDoorCausePlayerOrNpc;
    uint16_t QuestOps = 0;
    uint8_t Side = 0;
    uint64_t Version = 0;
    uint16_t RequestNumber = 0;
    float Position[3] = {0, 0, 0};
};

std::vector<uint8_t> WriteDoorSync(const DoorSync& door);
bool ReadDoorSync(const std::vector<uint8_t>& message, DoorSync& door);
}
