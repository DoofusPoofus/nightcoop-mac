/*
 vehicles, a first cut of nightcoops vehicles module (VehicleSpawn 60 / State 61 / Despawn 62 / Seat 63)
 ours: the car our player sits in (PlayerPuppet.GetMountedVehicle) gets a netId (our peer << 24 | counter) and goes out as
 VehicleSpawn evry 5 s, VehicleState evry 50 ms while it moves (0.3 m/s or more) and once a second standing, with our
 player as its occupant, plus our VehicleSeat evry 200 ms (reliable when it changes). getting out sends an empty seat at
 once and VehicleDespawn 2 s later like nightcoops grace. the seat slot is taken as seat_front_left for now
 a partners: a mirror car from codewares DynamicEntitySystem with their record and appearance (tag NightCoopVehicleMirror),
 placed on their track: snapshots of their states shown 100 ms late, lerped between two or pushed on by the velocity past the
 last one (500 ms at most), put there by the games teleport at most evry 32 ms like nightcoops teleport mode. nightcoop makes
 its mirrors kinematic thru a native physics flag and moves them with native ForceMoveTo, neither has a mac address yet.
 their body sits in the mirror with the workspot systems MountToVehicle (OccupantSlots, their seat, the seat bone and its
 passenger animset as anim variables, instant entry, 4 tries 250 ms apart) and is left alone by RemoteBodies while seated,
 UnmountFromVehicle when they get out. a mirror nobody heard of for 10 s or a despawn takes the car away
 not done yet: handover, crowd / npc / quest cars, doors, lights, damage, steering, our player riding a partners car
*/
#pragma once

#include "net/VehicleMessages.hpp"

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class RemoteBodies;

namespace net
{
class RelaySession;
}

class Vehicles
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    explicit Vehicles(LogFunction log);

    void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs);
    void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, RemoteBodies& bodies);

private:
    struct Snapshot
    {
        uint64_t StampMs = 0;
        float Position[3] = {0, 0, 0};
        float Orientation[4] = {0, 0, 0, 1};
        float Velocity[3] = {0, 0, 0};
    };

    struct Mirror
    {
        uint32_t OwnerPeer = 0;
        net::VehicleSpawn Spawn;
        uint64_t EntityHash = 0;
        bool InWorld = false;
        uint64_t SpawnRequestedMs = 0;
        uint64_t LastHeardMs = 0;
        uint64_t NextPlaceMs = 0;
        uint32_t Teleports = 0;
        std::deque<Snapshot> Snapshots;
    };

    struct PartnerSeat
    {
        uint32_t NetId = 0;
        uint64_t Slot = 0;
        uint32_t MountedNetId = 0;
        uint64_t MountedSlot = 0;
        uint32_t Tries = 0;
        uint64_t NextTryMs = 0;
    };

    struct OwnShare
    {
        uint32_t NetId = 0;
        uint64_t EntityHash = 0;
        uint64_t NextSpawnMs = 0;
        uint64_t NextStateMs = 0;
        uint64_t NextSeatMs = 0;
        uint64_t LeftAtMs = 0;
        bool SeatSent = false;
    };

    bool Resolve();
    bool FetchSystems();
    RED4ext::Handle<RED4ext::IScriptable> EntityOf(uint64_t entityHash);
    void ShareOurCar(uint64_t nowMs, net::RelaySession& session);
    void SpawnMirror(uint32_t netId, Mirror& mirror, uint64_t nowMs);
    void PlaceMirror(uint32_t netId, Mirror& mirror, uint64_t showMs, uint64_t nowMs);
    void SeatPartners(uint64_t nowMs, RemoteBodies& bodies);
    bool Mount(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& vehicle, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot);
    void Unmount(RED4ext::Handle<RED4ext::IScriptable>& vehicle, RED4ext::Handle<RED4ext::IScriptable>& body);
    void RemoveMirror(uint32_t netId, Mirror& mirror, RemoteBodies* bodies, const char* why);

    LogFunction m_log;
    bool m_resolved = false;
    bool m_usable = false;
    bool m_seatsUsable = false;
    RED4ext::CBaseFunction* m_getPlayer = nullptr;
    RED4ext::CBaseFunction* m_getMountedVehicle = nullptr;
    RED4ext::CBaseFunction* m_getRecordId = nullptr;
    RED4ext::CBaseFunction* m_getAppearance = nullptr;
    RED4ext::CBaseFunction* m_getEntityId = nullptr;
    RED4ext::CBaseFunction* m_getWorldPosition = nullptr;
    RED4ext::CBaseFunction* m_getWorldOrientation = nullptr;
    RED4ext::CBaseFunction* m_getLinearVelocity = nullptr;
    RED4ext::CBaseFunction* m_isEngineOn = nullptr;
    RED4ext::CBaseFunction* m_getDynamicEntitySystem = nullptr;
    RED4ext::CBaseFunction* m_createEntity = nullptr;
    RED4ext::CBaseFunction* m_getEntity = nullptr;
    RED4ext::CBaseFunction* m_deleteEntity = nullptr;
    RED4ext::CBaseFunction* m_getTeleportationFacility = nullptr;
    RED4ext::CBaseFunction* m_teleport = nullptr;
    RED4ext::CBaseFunction* m_getWorkspotSystem = nullptr;
    RED4ext::CBaseFunction* m_mountToVehicle = nullptr;
    RED4ext::CBaseFunction* m_unmountFromVehicle = nullptr;
    RED4ext::CBaseFunction* m_boneFromSlot = nullptr;
    RED4ext::CBaseFunction* m_animsetForPassenger = nullptr;
    RED4ext::CClass* m_specClass = nullptr;
    RED4ext::Handle<RED4ext::IScriptable> m_dynamicEntitySystem;
    RED4ext::Handle<RED4ext::IScriptable> m_teleportationFacility;
    RED4ext::Handle<RED4ext::IScriptable> m_workspotSystem;
    std::map<uint32_t, Mirror> m_mirrors;
    std::map<uint32_t, PartnerSeat> m_seats;
    OwnShare m_share;
    uint32_t m_netIdCounter = 0;
    uint32_t m_sharesStarted = 0;
};
}
