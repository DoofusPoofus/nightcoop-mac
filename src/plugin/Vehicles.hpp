/*
 vehicles a first cut of nightcoops vehicles module (VehicleSpawn 60 / State 61 / Despawn 62 / Seat 63)
 ours: the car our player sits in (PlayerPuppet.GetMountedVehicle) gets a netId (our peer << 24 | counter) and goes out as
 VehicleSpawn evry 5 s VehicleState evry 50 ms while it moves (0.3 m/s or more) and once a second standing with our
 player as its occupant plus our VehicleSeat evry 200 ms (reliable when it changes). getting out sends an empty seat at
 once and VehicleDespawn 2 s later like nightcoops grace. our seat slot comes from the mounting facility
 (GetMountingInfoSingleWithIds) seat_front_left when it says nothing
 a partners: a mirror car from codewares DynamicEntitySystem with their record and appearance (tag NightCoopVehicleMirror)
 placed on their track: snapshots of their states shown 100 ms late lerped between two or pushed on by the velocity past the
 last one (500 ms at most) put there by the games teleport at most evry 32 ms like nightcoops teleport mode. nightcoop makes
 its mirrors kinematic thru a native physics flag and moves them with native ForceMoveTo neither has a mac address yet.
 their body sits in the mirror with the workspot systems MountToVehicle (OccupantSlots their seat the seat bone and its
 passenger animset as anim variables instant entry 4 tries 250 ms apart) and is left alone by RemoteBodies while seated
 UnmountFromVehicle when they get out. a mirror nobody heard of for 10 s or a despawn takes the car away
 riding together: sitting in a partners mirror sends only our VehicleSeat for their netId (evry 200 ms reliable when it changes
 an empty one when we get out) and a partners seat in our own shared car seats their body in our real car (FindEntityByID)
 handover (VehicleHandover 64): sitting at the wheel of a partners mirror nobody drives makes it our shared car under the same
 netId (reason 1). the old owner turns its real car into a mirror of ours. an owner still at the wheel refuses (reason 3) and
 a handover to us of a mirror we have adopts it as ours. a real car that became a mirror is never deleted
 dents (VehicleDamage 132): a partners car damage grid (2 layers of 15) and flat tire go onto our mirror. the first damage we put
 on a mirror tells us where the grid lives (a pointer in the car object whose +0x100 holds those 30 floats, read with
 vm_read_overwrite so a wrong guess never crashes) and from then on our own cars grid goes out evry 500 ms on change (5 s
 refresh while damaged). on mac 2.5.0 the grid is known to sit at [car + 0x538] + 0x100 so ours goes out from the start
 scene cars (VehicleSceneSeat 123): a car a scene holds us in (an npc drives and our player or the car is in a scene) is not
 shared: we tell the others our seat in our own copy of it and a partner in theirs gets its body seated in our copy of that car
 (the same static entity id or the nearest car of that record within 30 m)
 radio (CarsWord 181 kinds 0 / 1): the car we drive tells each partner sitting in it its radio (on station index names) on
 change and evry 2 s and we play that on our mirror of their car. switching the station in a partners car asks its owner
 car hits (VehicleHit 65): when the car we drive hits a partner body (script wrap on gameObject.OnVehicleHit) that partner gets
 the hit (at most one per 1.5 s per partner from 1.5 m/s). a partners car hitting us plays as the games OnCarHitPlayer on our
 player unless we sit in a car ourselfs
 not done yet: handing a car back to its home peer when we get out (reason 2) crowd / npc / quest cars doors lights damage steering
 testing only: RunTestCar spawns a car next to our player (tag NightCoopTestCar) and mounts us at its wheel with the
 mounting facility (MountingRequest instant) so the sending side runs with nobody at the keyboard. when a partner car mirror
 is already here it puts us in its front right seat instead
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
    void RunTestCar(uint64_t nowMs);
    void OnCarHit(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs, uint32_t ourPeer);

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
        bool SomeoneAtWheel = true;
        bool Adopted = false;
        uint32_t DentsShown = 0;
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
        uint64_t NextDentsMs = 0;
        uint64_t NextDentsRefreshMs = 0;
        float LastGrid[30] = {};
        int32_t LastFlatTire = -1;
    };

    struct RideSeat
    {
        uint32_t NetId = 0;
        uint64_t Slot = 0;
        uint64_t NextSeatMs = 0;
    };

    bool Resolve();
    bool FetchSystems();
    RED4ext::Handle<RED4ext::IScriptable> EntityOf(uint64_t entityHash);
    RED4ext::Handle<RED4ext::IScriptable> VehicleOf(uint32_t netId);
    uint64_t OurSeatSlot(RED4ext::Handle<RED4ext::IScriptable>& player);
    void ShareOurCar(uint64_t nowMs, net::RelaySession& session);
    void SpawnMirror(uint32_t netId, Mirror& mirror, uint64_t nowMs);
    void PlaceMirror(uint32_t netId, Mirror& mirror, uint64_t showMs, uint64_t nowMs);
    void SeatPartners(uint64_t nowMs, RemoteBodies& bodies);
    bool Mount(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& vehicle, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot);
    void Unmount(RED4ext::Handle<RED4ext::IScriptable>& vehicle, RED4ext::Handle<RED4ext::IScriptable>& body);
    void RemoveMirror(uint32_t netId, Mirror& mirror, RemoteBodies* bodies, const char* why);
    void SendCarHits(uint64_t nowMs, net::RelaySession& session);
    bool ReadRadio(RED4ext::Handle<RED4ext::IScriptable>& vehicle, net::CarRadio& radio);
    void SetRadio(RED4ext::Handle<RED4ext::IScriptable>& vehicle, bool on, uint32_t station);
    void ShareRadio(uint64_t nowMs, net::RelaySession& session);
    bool InScene(uint64_t entityHash);
    void SendSceneSeat(RED4ext::Handle<RED4ext::IScriptable>& vehicle, uint64_t vehicleHash, uint64_t slot, uint64_t nowMs, net::RelaySession& session);
    RED4ext::Handle<RED4ext::IScriptable> FindSceneCar(const net::VehicleSceneSeat& seat);
    void SeatSceneSeats(uint64_t nowMs, RemoteBodies& bodies);
    void FindDentGrid(RED4ext::Handle<RED4ext::IScriptable>& vehicle, const float* wanted);
    bool ReadDentGrid(RED4ext::Handle<RED4ext::IScriptable>& vehicle, float* grid);
    static uint32_t KnownGridOffset();
    void SendHandover(uint32_t netId, uint32_t newOwner, uint8_t reason, RED4ext::Handle<RED4ext::IScriptable>& vehicle, uint64_t seatSlot, net::RelaySession& session);
    void OnHandover(uint32_t fromPeer, const net::VehicleHandover& handover, uint64_t nowMs, uint32_t ourPeer, bool weAreAtTheWheel);

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
    std::map<uint32_t, uint64_t> m_lastCarHitMs;
    std::vector<uint32_t> m_handoverRefusals;
    uint32_t m_ourPeer = 0;
    std::map<uint32_t, uint64_t> m_radioSentMs;
    struct PartnerSceneSeat
    {
        net::VehicleSceneSeat Seat;
        uint64_t HeardMs = 0;
        uint64_t MountedEntity = 0;
        uint32_t Tries = 0;
        uint64_t NextTryMs = 0;
    };
    std::map<uint32_t, PartnerSceneSeat> m_sceneSeats;
    net::VehicleSceneSeat m_ourSceneSeat;
    uint64_t m_nextSceneSeatMs = 0;
    uint64_t m_sceneCheckedMs = 0;
    bool m_sceneCar = false;
    net::CarRadio m_ownRadio;
    net::CarRadio m_appliedRadio;
    uint64_t m_nextRadioMs = 0;
    uint32_t m_gridOffset = 0;
    bool m_gridSearched = false;
    bool m_gridBroken = false;
    uint64_t m_gridSearchAtMs = 0;
    uint32_t m_gridSearchNetId = 0;
    float m_gridWanted[30] = {};
    static constexpr uint32_t VehicleObjectSize = 2976;
    bool m_atWheel = false;
    uint64_t m_lastHitOnUsMs = 0;
    RideSeat m_ride;
    RED4ext::CBaseFunction* m_findEntity = nullptr;
    uint32_t m_netIdCounter = 0;
    RED4ext::CBaseFunction* m_getMountingFacility = nullptr;
    RED4ext::CBaseFunction* m_getMountingInfo = nullptr;
    RED4ext::CBaseFunction* m_mount = nullptr;
    uint64_t m_testCarHash = 0;
    uint64_t m_testCarSpawnedMs = 0;
    uint64_t m_testStartMs = 0;
    bool m_testMounted = false;
    bool m_testRidesMirror = false;
    bool m_testTakesWheel = false;
    bool m_testDented = false;
    bool m_testRadioOn = false;
    uint64_t m_testDentAtMs = 0;
    uint32_t m_sharesStarted = 0;
};
}
