/*
 weapons, the mac take on the weapon parts of nightcoops player bridge and puppets. no message of its own, it all rides in
 PlayerState (records, shot counts, the psm weapon states and the aim / shoot / reload flags)
 ours: the players equippedRightHandWeapon / equippedLeftHandWeapon (weak handles) -> GetItemID gives the records, the left
 one only goes out when it is tagged DualWield like nightcoop does. the psm blackboard (GetPlayerStateMachineBlackboard with
 GetAllBlackboardDefs().PlayerStateMachine) gives RightWeapon / LeftWeapon / UpperBody. a shot is a drop of the weapons
 MagazineAmmoCount (its GetSharedData blackboard) while the same weapon stays in that hand, so the counts only go up
 flags like nightcoops bridge: aim 0x04 when UpperBody is Aim, reload 0x80 when UpperBody is Reload or a hand is in Reload,
 shoot 0x40 when a hand is in Shoot, silenced 0x8000 when the right weapons CanSilentKill stat is above 0
 a partners: the right record goes in WeaponRight (the left one in WeaponLeft) with GiveItem + AddItemToSlot on the records
 first placementSlot, checked a second later (then evry 5 s) with GetItemInSlot and put back when it went missing (5 tries in a row). new shots (at
 most 4 per state, the first state is just the baseline) set up the weapons npc effects once (fxPackage -> npc_vfx_set ->
 actions into SetWeaponEffects, SetWeaponOwner 1), then queue an entAnimExternalEvent Shoot on the body and the weapon and
 call TriggerWeaponEffects for the muzzle flash. nightcoops native weapon sound has no mac address and is left out
 testing only: DrawTestWeapon gives our player a weapon and draws it (EquipItem + DrawItem on its EquipmentSystemPlayerData),
 FakeShot takes one round out of its MagazineAmmoCount like a real shot would, DescribeOwn tells wat the reads see
*/
#pragma once

#include "net/SessionMessages.hpp"

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace nightcoop_mac::weapons
{
using LogFunction = std::function<void(const std::string&)>;

struct OwnWeapons
{
    uint64_t RightRecord = 0;
    uint64_t LeftRecord = 0;
    uint32_t RightShots = 0;
    uint32_t LeftShots = 0;
    int32_t RightState = 0;
    int32_t LeftState = 0;
    int32_t UpperBody = 0;
    bool Silenced = false;
};

struct HandHeld
{
    uint64_t Record = 0;
    uint64_t Slot = 0;
    uint64_t NextCheckMs = 0;
    uint32_t PlaceTries = 0;
    bool GaveUp = false;
    bool Verified = false;
    bool EffectsReady = false;
    bool ShotsKnown = false;
    uint32_t ShotsSeen = 0;
};

struct Held
{
    HandHeld Right;
    HandHeld Left;
    uint32_t ShotsPlayed = 0;
};

bool ReadOwnWeapons(RED4ext::Handle<RED4ext::IScriptable>& player, OwnWeapons& weapons, const LogFunction& log);
void PutInState(const OwnWeapons& weapons, net::PlayerState& state);
bool DrawTestWeapon(RED4ext::Handle<RED4ext::IScriptable>& player, const char* recordName, const LogFunction& log);
bool FakeShot(RED4ext::Handle<RED4ext::IScriptable>& player);
std::string DescribeOwn(RED4ext::Handle<RED4ext::IScriptable>& player);
void Hold(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state, Held& held, uint64_t nowMs,
          const LogFunction& log);
}
