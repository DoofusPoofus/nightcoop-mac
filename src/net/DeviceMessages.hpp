/*
 device messages rebuilt from the 0.1.2 dll (nightcoops devicesync and barrels). they ride in Game envelopes, all reliable
 DeviceSync (139), variable size, min 80 bytes after the type: u8 kind (1 guest -> host i did this, 2 host -> guests state +
   the action to do again, 3 host -> peers state only or the answer) | u32 peer (kind 1 the sender, else whose action it was) |
   u64 ps entity hash (not 0) | u64 ps component name | u64 ps class name (not 0) | u8 action kind (0 state only, 1 the same
   action, 2 the same action at the host only, 3 a skill check, 4 destroyed, 5 not shared and refused) | u8 skill (1 hacking,
   2 engineering, 3 demolition, set only for action kind 3) | i8 state before | i8 state after (EDeviceStatus -2..2 or 0x7f
   unknown) | u8 flags (bit0 timed, bit1 quickhack, bit2 a lasting state) | u64 version (kind 2 and 3 not 0) | u16 request
   number (kind 1 not 0) | u32 ms since the action | u64 action class name (action kind 1 and 2 not 0) | f32 duration s
   (0..3600) | u8 has prop | u64 prop name | u64 prop type name | u8 prop flags | u8 n fields (up to 48) | n x (u64 field name |
   u64 field type name | u8 len 1..8 | len bytes) | u8 m prop values (up to 3) | m x (u64 index 0..2 | u64 type name | u8 len
   1..8 | len bytes)
 TurretWord (214), 10 bytes after the type: u8 kind (1 guest asks, 2 host answers) | u64 turret entity hash | u8 state (0 the
   host has no such turret, 1 calm, 2 in combat; an ask sends 0). the reader takes extra bytes like the dll does
 DeviceBlast (130): u8 kind (0 a live blast, 1 a snapshot of devices that blew before) | u8 n (up to 32) | n x (u64 device
   entity hash (bits above 24 set, spawned ids differ between machines) | f32 x3 position | u8 blown by the senders own player)
 readers drop what the dll drops and bytes left over
*/
#pragma once

#include <cstdint>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kDeviceRequest = 1;
inline constexpr uint8_t kDeviceReplay = 2;
inline constexpr uint8_t kDeviceAnswer = 3;

inline constexpr uint8_t kDeviceStateOnly = 0;
inline constexpr uint8_t kDeviceSameAction = 1;
inline constexpr uint8_t kDeviceSameActionHostOnly = 2;
inline constexpr uint8_t kDeviceSkillCheck = 3;
inline constexpr uint8_t kDeviceDestroyed = 4;
inline constexpr uint8_t kDeviceNotShared = 5;

inline constexpr int8_t kDeviceDisabled = -2;
inline constexpr int8_t kDeviceUnpowered = -1;
inline constexpr int8_t kDeviceOff = 0;
inline constexpr int8_t kDeviceOn = 1;
inline constexpr int8_t kDeviceStateUnknown = 0x7F;

inline constexpr uint8_t kDeviceTimed = 0x01;
inline constexpr uint8_t kDeviceQuickHack = 0x02;
inline constexpr uint8_t kDeviceLasting = 0x04;

inline constexpr size_t kMostDeviceFields = 48;
inline constexpr size_t kMostDevicePropValues = 3;
inline constexpr size_t kMostDeviceValueBytes = 8;

struct DeviceField
{
    uint64_t Name = 0;
    uint64_t TypeName = 0;
    std::vector<uint8_t> Bytes;
};

struct DevicePropValue
{
    uint64_t Index = 0;
    uint64_t TypeName = 0;
    std::vector<uint8_t> Bytes;
};

struct DeviceSync
{
    uint8_t Kind = kDeviceRequest;
    uint32_t Peer = 0;
    uint64_t EntityHash = 0;
    uint64_t ComponentName = 0;
    uint64_t PsClass = 0;
    uint8_t ActionKind = kDeviceStateOnly;
    uint8_t Skill = 0;
    int8_t StateBefore = kDeviceStateUnknown;
    int8_t StateAfter = kDeviceStateUnknown;
    uint8_t Flags = 0;
    uint64_t Version = 0;
    uint16_t RequestNumber = 0;
    uint32_t MsSinceAction = 0;
    uint64_t ActionClass = 0;
    float Duration = 0;
    bool HasProp = false;
    uint64_t PropName = 0;
    uint64_t PropTypeName = 0;
    uint8_t PropFlags = 0;
    std::vector<DeviceField> Fields;
    std::vector<DevicePropValue> PropValues;
};

inline constexpr uint8_t kTurretAsk = 1;
inline constexpr uint8_t kTurretAnswer = 2;
inline constexpr uint8_t kTurretNotThere = 0;
inline constexpr uint8_t kTurretCalm = 1;
inline constexpr uint8_t kTurretInCombat = 2;

struct TurretWord
{
    uint8_t Kind = kTurretAsk;
    uint64_t TurretKey = 0;
    uint8_t State = kTurretNotThere;
};

inline constexpr uint8_t kBlastLive = 0;
inline constexpr uint8_t kBlastSnapshot = 1;
inline constexpr size_t kMostBlastsPerMessage = 32;

struct BlastedDevice
{
    uint64_t DeviceKey = 0;
    float Position[3] = {0, 0, 0};
    uint8_t ByOurPlayer = 0;
};

struct DeviceBlast
{
    uint8_t Kind = kBlastLive;
    std::vector<BlastedDevice> Devices;
};

bool DeviceStateKnown(int8_t state);
std::vector<uint8_t> WriteDeviceSync(const DeviceSync& sync);
bool ReadDeviceSync(const std::vector<uint8_t>& message, DeviceSync& sync);
std::vector<uint8_t> WriteTurretWord(const TurretWord& word);
bool ReadTurretWord(const std::vector<uint8_t>& message, TurretWord& word);
std::vector<uint8_t> WriteDeviceBlast(const DeviceBlast& blast);
bool ReadDeviceBlast(const std::vector<uint8_t>& message, DeviceBlast& blast);
bool BlastKeyIsShared(uint64_t deviceKey);
}
