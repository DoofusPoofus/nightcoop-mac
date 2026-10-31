/*
 npc states, the game calls npc share and npc copies both need for wat NpcSnapshot carries besides the place
 the 8 ai net states (stance, upper body, high level, behavior, hit reaction mode, defense mode, phase, locomotion mode) are
 read with the GetReplicated getters of the npcs NPCStatesComponent and written with the SetReplicated setters. attitude goes
 thru the attitude agents (the native gamePuppet.GetAttitude, since a direct call of the scripted GetAttitudeAgent lands on
 the empty gameObject one, then GetAttitudeTowards / SetAttitudeTowards). hand items are the records in the
 WeaponRight / WeaponLeft slots, given and put in the slot on a copy. health is the Health stat pool in percent, read with
 GetStatPoolValue and set with RequestSettingStatPoolValue. incapacitated and time dilation come from the puppet itself
 match keys are the npcs community stub: gameEntityStubComponentPS (by GetConstAccessToPSObject with its PersistentID)
 GetSpawnerID and GetOwnerCommunityEntryName, 0 for npcs without one
 all rtti, main thread only
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <string>

namespace nightcoop_mac::npc_states
{
constexpr int StateCount = 8;
constexpr uint8_t HighLevelStateSlot = 2;
constexpr uint8_t StanceStateSlot = 0;
constexpr uint8_t HighLevelDead = 3;
constexpr uint8_t StanceCover = 1;
constexpr uint8_t StanceCrouch = 2;
constexpr uint8_t AttitudeNeutral = 1;

bool Ready();
std::string MissingCalls();
bool ReadStates(RED4ext::Handle<RED4ext::IScriptable>& npc, uint8_t (&states)[StateCount]);
bool WriteState(RED4ext::Handle<RED4ext::IScriptable>& npc, int slot, uint8_t value);
RED4ext::Handle<RED4ext::IScriptable> AttitudeAgentOf(RED4ext::Handle<RED4ext::IScriptable>& puppet);
uint8_t AttitudeTowards(RED4ext::Handle<RED4ext::IScriptable>& npcAgent, RED4ext::Handle<RED4ext::IScriptable>& otherAgent);
bool SetAttitudeTowards(RED4ext::Handle<RED4ext::IScriptable>& npcAgent, RED4ext::Handle<RED4ext::IScriptable>& otherAgent, uint8_t attitude);
uint64_t HandRecord(RED4ext::Handle<RED4ext::IScriptable>& npc, bool rightHand);
bool PutInHand(RED4ext::Handle<RED4ext::IScriptable>& npc, uint64_t record, bool rightHand);
void EmptyHand(RED4ext::Handle<RED4ext::IScriptable>& npc, bool rightHand);
float HealthPercent(uint64_t entityHash);
bool SetHealthPercent(uint64_t entityHash, float percent);
bool IsIncapacitated(RED4ext::Handle<RED4ext::IScriptable>& npc);
float TimeDilation(RED4ext::Handle<RED4ext::IScriptable>& npc, bool& ignoresGlobal);
bool MatchKeys(uint64_t entityHash, uint64_t& spawnerKey, uint64_t& communityEntryKey);
uint64_t RecordOf(RED4ext::Handle<RED4ext::IScriptable>& npc);
}
