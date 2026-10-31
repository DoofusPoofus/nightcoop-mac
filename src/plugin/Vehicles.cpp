#include "plugin/Vehicles.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/RemoteBodies.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/TweakDB.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace nightcoop_mac
{
namespace
{
constexpr uint64_t SpawnEveryMs = 5000;
constexpr uint64_t StateWhileMovingMs = 50;
constexpr uint64_t StateStandingMs = 1000;
constexpr uint64_t SeatEveryMs = 200;
constexpr uint64_t DespawnGraceMs = 2000;
constexpr float MovingAboveMetersPerSecond = 0.3f;
constexpr uint64_t ShowDelayMs = 100;
constexpr uint64_t MostExtrapolationMs = 500;
constexpr uint64_t PlaceEveryMs = 32;
constexpr size_t MostSnapshots = 32;
constexpr uint64_t ForgetMirrorAfterMs = 10000;
constexpr uint64_t RetrySpawnAfterMs = 10000;
constexpr uint32_t MostMountTries = 4;
constexpr uint64_t MountRetryMs = 250;
constexpr float DegreesPerRadian = 57.29578f;
const char* MirrorTag = "NightCoopVehicleMirror";
const char* DriverSeat = "seat_front_left";

struct EulerAnglesLayout
{
    float Roll;
    float Pitch;
    float Yaw;
};

struct QuaternionLayout
{
    float I;
    float J;
    float K;
    float R;
};

template<typename Value>
bool WriteSpecProperty(RED4ext::CClass* specClass, void* spec, const char* name, const char* typeName, const Value& value)
{
    auto* property = specClass->GetProperty(name);
    if (!property || !property->type || std::string(property->type->GetName().ToString()) != typeName)
    {
        return false;
    }
    std::memcpy(property->GetValuePtr<uint8_t>(spec), &value, sizeof(Value));
    return true;
}

std::string NetIdText(uint32_t netId)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%#010x", netId);
    return text;
}

float YawOf(const float* orientation)
{
    const float i = orientation[0];
    const float j = orientation[1];
    const float k = orientation[2];
    const float r = orientation[3];
    return std::atan2(2.0f * (r * k + i * j), 1.0f - 2.0f * (j * j + k * k)) * DegreesPerRadian;
}
}

Vehicles::Vehicles(LogFunction log)
    : m_log(std::move(log))
{
    m_netIdCounter = std::random_device {}() & 0xFFFFFF;
}

bool Vehicles::Resolve()
{
    if (m_resolved)
    {
        return m_usable;
    }
    m_resolved = true;
    m_getPlayer = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
    m_getMountedVehicle = game::FindFunction("PlayerPuppet", "GetMountedVehicle;");
    m_getRecordId = game::FindFunction("vehicleBaseObject", "GetRecordID");
    m_getAppearance = game::FindFunction("entEntity", "GetCurrentAppearanceName");
    m_getEntityId = game::FindFunction("entEntity", "GetEntityID");
    m_getWorldPosition = game::FindFunction("entEntity", "GetWorldPosition");
    m_getWorldOrientation = game::FindFunction("entEntity", "GetWorldOrientation");
    m_getLinearVelocity = game::FindFunction("vehicleBaseObject", "GetLinearVelocity");
    m_isEngineOn = game::FindFunction("vehicleBaseObject", "IsEngineTurnedOn");
    m_getDynamicEntitySystem = game::FindFunction("ScriptGameInstance", "GetDynamicEntitySystem");
    m_createEntity = game::FindFunction("DynamicEntitySystem", "CreateEntity");
    m_getEntity = game::FindFunction("DynamicEntitySystem", "GetEntity");
    m_deleteEntity = game::FindFunction("DynamicEntitySystem", "DeleteEntity");
    m_getTeleportationFacility = game::FindFunction("ScriptGameInstance", "GetTeleportationFacility");
    m_teleport = game::FindFunction("gameTeleportationFacility", "Teleport");
    m_getWorkspotSystem = game::FindFunction("ScriptGameInstance", "GetWorkspotSystem");
    m_mountToVehicle = game::FindFunction("gameWorkspotGameSystem", "MountToVehicle");
    m_unmountFromVehicle = game::FindFunction("gameWorkspotGameSystem", "UnmountFromVehicle");
    m_boneFromSlot = game::FindFunction("vehicleBaseObject", "GetBoneNameFromSlot");
    m_animsetForPassenger = game::FindFunction("vehicleBaseObject", "GetAnimsetOverrideForPassenger");
    m_specClass = RED4ext::CRTTISystem::Get()->GetClass("DynamicEntitySpec");
    m_usable = m_getPlayer && game::ParamsAre(m_getMountedVehicle, {}) && game::ParamsAre(m_getRecordId, {}) &&
               game::ParamsAre(m_getAppearance, {}) && game::ParamsAre(m_getEntityId, {}) && game::ParamsAre(m_getWorldPosition, {}) &&
               game::ParamsAre(m_getWorldOrientation, {}) && game::ParamsAre(m_getLinearVelocity, {}) && game::ParamsAre(m_isEngineOn, {}) &&
               game::ParamsAre(m_getDynamicEntitySystem, {}) && game::ParamsAre(m_createEntity, {"handle:DynamicEntitySpec"}) &&
               game::ParamsAre(m_getEntity, {"entEntityID"}) && game::ParamsAre(m_deleteEntity, {"entEntityID"}) &&
               game::ParamsAre(m_getTeleportationFacility, {"ScriptGameInstance"}) &&
               game::ParamsAre(m_teleport, {"handle:gameObject", "Vector4", "EulerAngles"}) && m_specClass;
    m_seatsUsable = game::ParamsAre(m_getWorkspotSystem, {"ScriptGameInstance"}) &&
                    game::ParamsAre(m_mountToVehicle, {"handle:gameObject", "handle:gameObject", "Float", "Float", "CName", "CName",
                                                       "array:entEntityID", "CName", "array:CName"}) &&
                    game::ParamsAre(m_unmountFromVehicle, {"handle:gameObject", "handle:gameObject", "Bool", "Vector4", "Quaternion", "CName"}) &&
                    game::ParamsAre(m_boneFromSlot, {"CName"}) && game::ParamsAre(m_animsetForPassenger, {"CName"});
    if (!m_usable)
    {
        m_log("vehicles: the vehicle, codeware or teleport calls look different than expected, no shared cars");
    }
    else
    {
        m_log(std::string("vehicles: the car we sit in goes to partners and theirs show up here as mirrors") +
              (m_seatsUsable ? ", partner bodies sit in them thru the workspot system" : ", but the seat workspot calls look different (bodies wont sit)"));
    }
    return m_usable;
}

bool Vehicles::FetchSystems()
{
    RED4ext::ScriptGameInstance gameInstance;
    if (!m_dynamicEntitySystem)
    {
        RED4ext::StackArgs_t args;
        game::CallStatic(m_getDynamicEntitySystem, &m_dynamicEntitySystem, args);
    }
    if (!m_teleportationFacility)
    {
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &gameInstance);
        game::CallStatic(m_getTeleportationFacility, &m_teleportationFacility, args);
    }
    if (m_seatsUsable && !m_workspotSystem)
    {
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &gameInstance);
        game::CallStatic(m_getWorkspotSystem, &m_workspotSystem, args);
    }
    return m_dynamicEntitySystem && m_teleportationFacility;
}

RED4ext::Handle<RED4ext::IScriptable> Vehicles::EntityOf(uint64_t entityHash)
{
    RED4ext::Handle<RED4ext::IScriptable> entity;
    if (!entityHash || !m_dynamicEntitySystem)
    {
        return entity;
    }
    RED4ext::ent::EntityID entityId(entityHash);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &entityId);
    game::CallMethod(m_dynamicEntitySystem.instance, m_getEntity, &entity, args);
    return entity;
}

void Vehicles::OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs)
{
    const uint8_t type = net::MessageTypeOf(message);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::VehicleSpawn))
    {
        net::VehicleSpawn spawn;
        if (!net::ReadVehicleSpawn(message, spawn) || (spawn.NetId >> 24) == 0xFE)
        {
            return;
        }
        auto& mirror = m_mirrors[spawn.NetId];
        const bool fresh = mirror.LastHeardMs == 0;
        mirror.OwnerPeer = fromPeer;
        mirror.Spawn = spawn;
        mirror.LastHeardMs = nowMs;
        if (fresh)
        {
            Snapshot first;
            std::memcpy(first.Position, spawn.Position, sizeof(first.Position));
            std::memcpy(first.Orientation, spawn.Orientation, sizeof(first.Orientation));
            mirror.Snapshots.push_back(first);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::VehicleState))
    {
        net::VehicleState state;
        if (!net::ReadVehicleState(message, state))
        {
            return;
        }
        auto mirror = m_mirrors.find(state.NetId);
        if (mirror == m_mirrors.end() || mirror->second.OwnerPeer != fromPeer)
        {
            return;
        }
        Snapshot snapshot;
        snapshot.StampMs = state.StampMs;
        std::memcpy(snapshot.Position, state.Position, sizeof(snapshot.Position));
        std::memcpy(snapshot.Orientation, state.Orientation, sizeof(snapshot.Orientation));
        std::memcpy(snapshot.Velocity, state.Velocity, sizeof(snapshot.Velocity));
        auto& snapshots = mirror->second.Snapshots;
        if (!snapshots.empty() && snapshots.back().StampMs >= snapshot.StampMs && snapshots.back().StampMs != 0)
        {
            return;
        }
        if (!snapshots.empty() && snapshots.back().StampMs == 0)
        {
            snapshots.clear();
        }
        snapshots.push_back(snapshot);
        while (snapshots.size() > MostSnapshots)
        {
            snapshots.pop_front();
        }
        mirror->second.LastHeardMs = nowMs;
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::VehicleDespawn))
    {
        net::VehicleDespawn despawn;
        if (!net::ReadVehicleDespawn(message, despawn))
        {
            return;
        }
        auto mirror = m_mirrors.find(despawn.NetId);
        if (mirror != m_mirrors.end() && mirror->second.OwnerPeer == fromPeer)
        {
            mirror->second.LastHeardMs = 1;
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::VehicleSeat))
    {
        net::VehicleSeat seat;
        if (!net::ReadVehicleSeat(message, seat) || (seat.NetId >> 24) == 0xFE)
        {
            return;
        }
        auto& partnerSeat = m_seats[fromPeer];
        if (partnerSeat.NetId != seat.NetId || partnerSeat.Slot != seat.Slot)
        {
            partnerSeat.NetId = seat.NetId;
            partnerSeat.Slot = seat.Slot;
            partnerSeat.Tries = 0;
            partnerSeat.NextTryMs = 0;
        }
    }
}

void Vehicles::ShareOurCar(uint64_t nowMs, net::RelaySession& session)
{
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t playerArgs;
    playerArgs.emplace_back(nullptr, &gameInstance);
    RED4ext::Handle<RED4ext::IScriptable> vehicle;
    RED4ext::StackArgs_t noArgs;
    if (game::CallStatic(m_getPlayer, &player, playerArgs) && player)
    {
        game::CallMethod(player.instance, m_getMountedVehicle, &vehicle, noArgs);
    }
    uint64_t vehicleHash = 0;
    if (vehicle)
    {
        game::CallMethod(vehicle.instance, m_getEntityId, &vehicleHash, noArgs);
        for (const auto& [netId, mirror] : m_mirrors)
        {
            if (mirror.EntityHash == vehicleHash)
            {
                vehicleHash = 0;
            }
        }
    }
    const uint32_t ourPeer = session.GetStatus().MyPeerNumber;
    if (!vehicleHash)
    {
        if (m_share.NetId && !m_share.LeftAtMs)
        {
            session.SendGameMessage(net::WriteVehicleSeat(net::VehicleSeat {}));
            m_share.LeftAtMs = nowMs;
        }
        if (m_share.NetId && nowMs - m_share.LeftAtMs >= DespawnGraceMs)
        {
            session.SendGameMessage(net::WriteVehicleDespawn({m_share.NetId, 1}));
            m_log("vehicles: we left our car, " + NetIdText(m_share.NetId) + " is not shared any more");
            m_share = OwnShare {};
        }
        return;
    }
    if (m_share.EntityHash != vehicleHash)
    {
        m_share = OwnShare {};
        m_share.NetId = (ourPeer << 24) | (++m_netIdCounter & 0xFFFFFF);
        m_share.EntityHash = vehicleHash;
        ++m_sharesStarted;
    }
    m_share.LeftAtMs = 0;

    RED4ext::Vector4 position {};
    QuaternionLayout orientation {0, 0, 0, 1};
    RED4ext::Vector4 velocity {};
    bool engineOn = false;
    game::CallMethod(vehicle.instance, m_getWorldPosition, &position, noArgs);
    game::CallMethod(vehicle.instance, m_getWorldOrientation, &orientation, noArgs);
    game::CallMethod(vehicle.instance, m_getLinearVelocity, &velocity, noArgs);
    game::CallMethod(vehicle.instance, m_isEngineOn, &engineOn, noArgs);
    const uint64_t slot = RED4ext::CName(DriverSeat).hash;
    if (nowMs >= m_share.NextSpawnMs)
    {
        RED4ext::TweakDBID record;
        RED4ext::CName appearance;
        game::CallMethod(vehicle.instance, m_getRecordId, &record, noArgs);
        game::CallMethod(vehicle.instance, m_getAppearance, &appearance, noArgs);
        net::VehicleSpawn spawn;
        spawn.NetId = m_share.NetId;
        spawn.HomePeer = ourPeer;
        spawn.Record = record.value & 0xFFFFFFFFFFULL;
        spawn.Appearance = appearance.hash;
        spawn.OwnerEntity = vehicleHash;
        spawn.Kind = 1;
        spawn.Flags = vehicleHash > 0xFFFFFF ? 0x1 : 0;
        spawn.Position[0] = position.X;
        spawn.Position[1] = position.Y;
        spawn.Position[2] = position.Z;
        std::memcpy(spawn.Orientation, &orientation, sizeof(spawn.Orientation));
        session.SendGameMessage(net::WriteVehicleSpawn(spawn));
        if (m_share.NextSpawnMs == 0)
        {
            char recordText[32];
            std::snprintf(recordText, sizeof(recordText), "%#llx", static_cast<unsigned long long>(spawn.Record));
            m_log("vehicles: we sit in a car, sharing it as " + NetIdText(m_share.NetId) + " (record " + recordText + ")");
        }
        m_share.NextSpawnMs = nowMs + SpawnEveryMs;
    }
    const float speed = std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y + velocity.Z * velocity.Z);
    if (nowMs >= m_share.NextStateMs)
    {
        net::VehicleState state;
        state.NetId = m_share.NetId;
        state.StampMs = session.SessionClockMs();
        state.Position[0] = position.X;
        state.Position[1] = position.Y;
        state.Position[2] = position.Z;
        std::memcpy(state.Orientation, &orientation, sizeof(state.Orientation));
        state.Velocity[0] = velocity.X;
        state.Velocity[1] = velocity.Y;
        state.Velocity[2] = velocity.Z;
        state.Flags = (engineOn ? net::kVehicleEngineOn : 0) | net::kVehicleSomeoneDrives;
        state.Occupants.push_back({slot, net::kOccupantPlayer, ourPeer});
        session.SendGameMessage(net::WriteVehicleState(state), net::kEveryPeer, false);
        m_share.NextStateMs = nowMs + (speed >= MovingAboveMetersPerSecond ? StateWhileMovingMs : StateStandingMs);
    }
    if (nowMs >= m_share.NextSeatMs)
    {
        net::VehicleSeat seat;
        seat.NetId = m_share.NetId;
        seat.Slot = slot;
        session.SendGameMessage(net::WriteVehicleSeat(seat), net::kEveryPeer, !m_share.SeatSent);
        m_share.SeatSent = true;
        m_share.NextSeatMs = nowMs + SeatEveryMs;
    }
}

void Vehicles::SpawnMirror(uint32_t netId, Mirror& mirror, uint64_t nowMs)
{
    mirror.SpawnRequestedMs = nowMs;
    auto* tweakDb = RED4ext::TweakDB::Get();
    if (!tweakDb || !tweakDb->GetRecord(RED4ext::TweakDBID(mirror.Spawn.Record)))
    {
        m_log("vehicles: " + NetIdText(netId) + " has a vehicle record we dont know, no mirror for it");
        return;
    }
    auto* spec = static_cast<RED4ext::IScriptable*>(m_specClass->CreateInstance(true));
    if (!spec)
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> specHandle(spec);
    const Snapshot& latest = mirror.Snapshots.empty() ? Snapshot {} : mirror.Snapshots.back();
    const RED4ext::Vector4 position {latest.Position[0], latest.Position[1], latest.Position[2], 1.0f};
    QuaternionLayout orientation {};
    std::memcpy(&orientation, latest.Orientation, sizeof(orientation));
    bool written = WriteSpecProperty(m_specClass, spec, "recordID", "TweakDBID", RED4ext::TweakDBID(mirror.Spawn.Record)) &&
                   WriteSpecProperty(m_specClass, spec, "appearanceName", "CName", RED4ext::CName(mirror.Spawn.Appearance)) &&
                   WriteSpecProperty(m_specClass, spec, "position", "Vector4", position) &&
                   WriteSpecProperty(m_specClass, spec, "orientation", "Quaternion", orientation) &&
                   WriteSpecProperty(m_specClass, spec, "alwaysSpawned", "Bool", true) &&
                   WriteSpecProperty(m_specClass, spec, "spawnInView", "Bool", true) &&
                   WriteSpecProperty(m_specClass, spec, "active", "Bool", true) &&
                   WriteSpecProperty(m_specClass, spec, "persistState", "Bool", false) &&
                   WriteSpecProperty(m_specClass, spec, "persistSpawn", "Bool", false);
    auto* tags = m_specClass->GetProperty("tags");
    if (!written || !tags || !tags->type || std::string(tags->type->GetName().ToString()) != "array:CName")
    {
        m_log("vehicles: DynamicEntitySpec has other fields than expected, no mirrors");
        m_usable = false;
        return;
    }
    tags->GetValuePtr<RED4ext::DynArray<RED4ext::CName>>(spec)->PushBack(RED4ext::CName(MirrorTag));
    RED4ext::ent::EntityID entityId;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &specHandle);
    if (game::CallMethod(m_dynamicEntitySystem.instance, m_createEntity, &entityId, args) && entityId.hash)
    {
        mirror.EntityHash = entityId.hash;
        m_log("vehicles: peer " + std::to_string(mirror.OwnerPeer) + " shares car " + NetIdText(netId) + ", spawning its mirror");
    }
}

void Vehicles::PlaceMirror(uint32_t netId, Mirror& mirror, uint64_t showMs, uint64_t nowMs)
{
    if (mirror.Snapshots.empty() || nowMs < mirror.NextPlaceMs)
    {
        return;
    }
    auto vehicle = EntityOf(mirror.EntityHash);
    if (!vehicle)
    {
        return;
    }
    if (!mirror.InWorld)
    {
        mirror.InWorld = true;
        m_log("vehicles: mirror of " + NetIdText(netId) + " is in the world");
    }
    Snapshot shown = mirror.Snapshots.front();
    const auto& snapshots = mirror.Snapshots;
    if (snapshots.back().StampMs == 0 || showMs >= snapshots.back().StampMs)
    {
        shown = snapshots.back();
        const float seconds = snapshots.back().StampMs == 0 ? 0.0f
                                                            : static_cast<float>(std::min(showMs - snapshots.back().StampMs, MostExtrapolationMs)) / 1000.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            shown.Position[axis] += shown.Velocity[axis] * seconds;
        }
    }
    else if (showMs > snapshots.front().StampMs)
    {
        for (size_t index = 1; index < snapshots.size(); ++index)
        {
            const Snapshot& before = snapshots[index - 1];
            const Snapshot& after = snapshots[index];
            if (showMs <= after.StampMs)
            {
                const float blend = after.StampMs > before.StampMs
                                        ? static_cast<float>(showMs - before.StampMs) / static_cast<float>(after.StampMs - before.StampMs)
                                        : 1.0f;
                const float sign = before.Orientation[0] * after.Orientation[0] + before.Orientation[1] * after.Orientation[1] +
                                           before.Orientation[2] * after.Orientation[2] + before.Orientation[3] * after.Orientation[3] <
                                       0
                                       ? -1.0f
                                       : 1.0f;
                float length = 0;
                for (int axis = 0; axis < 3; ++axis)
                {
                    shown.Position[axis] = before.Position[axis] + (after.Position[axis] - before.Position[axis]) * blend;
                }
                for (int part = 0; part < 4; ++part)
                {
                    shown.Orientation[part] = before.Orientation[part] + (sign * after.Orientation[part] - before.Orientation[part]) * blend;
                    length += shown.Orientation[part] * shown.Orientation[part];
                }
                length = std::sqrt(length);
                for (int part = 0; part < 4 && length > 0.0001f; ++part)
                {
                    shown.Orientation[part] /= length;
                }
                break;
            }
        }
    }
    RED4ext::Vector4 position {shown.Position[0], shown.Position[1], shown.Position[2], 1.0f};
    EulerAnglesLayout angles {0.0f, 0.0f, YawOf(shown.Orientation)};
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &vehicle);
    args.emplace_back(nullptr, &position);
    args.emplace_back(nullptr, &angles);
    if (game::CallMethod(m_teleportationFacility.instance, m_teleport, nullptr, args))
    {
        ++mirror.Teleports;
    }
    mirror.NextPlaceMs = nowMs + PlaceEveryMs;
}

bool Vehicles::Mount(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& vehicle, RED4ext::Handle<RED4ext::IScriptable>& body, uint64_t slot)
{
    RED4ext::CName slotName(slot);
    RED4ext::CName bone;
    RED4ext::CName animset;
    RED4ext::StackArgs_t boneArgs;
    boneArgs.emplace_back(nullptr, &slotName);
    game::CallMethod(vehicle.instance, m_boneFromSlot, &bone, boneArgs);
    RED4ext::StackArgs_t animsetArgs;
    animsetArgs.emplace_back(nullptr, &bone);
    game::CallMethod(vehicle.instance, m_animsetForPassenger, &animset, animsetArgs);
    float slideTime = 0.0f;
    float animDelay = 0.0f;
    RED4ext::CName container("OccupantSlots");
    RED4ext::DynArray<RED4ext::ent::EntityID> synchronized;
    RED4ext::CName instantEntry;
    RED4ext::DynArray<RED4ext::CName> animVariables;
    if (bone)
    {
        animVariables.PushBack(bone);
    }
    if (animset)
    {
        animVariables.PushBack(animset);
    }
    bool mounted = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &vehicle);
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &slideTime);
    args.emplace_back(nullptr, &animDelay);
    args.emplace_back(nullptr, &container);
    args.emplace_back(nullptr, &slotName);
    args.emplace_back(nullptr, &synchronized);
    args.emplace_back(nullptr, &instantEntry);
    args.emplace_back(nullptr, &animVariables);
    game::CallMethod(m_workspotSystem.instance, m_mountToVehicle, &mounted, args);
    const char* boneText = bone ? bone.ToString() : nullptr;
    const char* animsetText = animset ? animset.ToString() : nullptr;
    m_log("vehicles: peer " + std::to_string(peerNumber) + " body " + (mounted ? "sits" : "could not sit") + " in " +
          (slotName.ToString() ? slotName.ToString() : "an unknown seat") + " (bone " + (boneText ? boneText : "none") + ", animset " +
          (animsetText ? animsetText : "none") + ")");
    return mounted;
}

void Vehicles::Unmount(RED4ext::Handle<RED4ext::IScriptable>& vehicle, RED4ext::Handle<RED4ext::IScriptable>& body)
{
    bool instant = true;
    RED4ext::Vector4 noMove {};
    QuaternionLayout noTurn {0, 0, 0, 1};
    RED4ext::CName exitSlot;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &vehicle);
    args.emplace_back(nullptr, &body);
    args.emplace_back(nullptr, &instant);
    args.emplace_back(nullptr, &noMove);
    args.emplace_back(nullptr, &noTurn);
    args.emplace_back(nullptr, &exitSlot);
    game::CallMethod(m_workspotSystem.instance, m_unmountFromVehicle, nullptr, args);
}

void Vehicles::SeatPartners(uint64_t nowMs, RemoteBodies& bodies)
{
    if (!m_seatsUsable || !m_workspotSystem)
    {
        return;
    }
    for (auto& [peerNumber, seat] : m_seats)
    {
        RED4ext::Handle<RED4ext::IScriptable> body;
        if (!bodies.GetBody(peerNumber, body))
        {
            seat.MountedNetId = 0;
            continue;
        }
        const bool wantsSeat = seat.NetId != 0 && m_mirrors.count(seat.NetId) && m_mirrors[seat.NetId].InWorld;
        if (seat.MountedNetId && (!wantsSeat || seat.MountedNetId != seat.NetId || seat.MountedSlot != seat.Slot))
        {
            auto mounted = m_mirrors.find(seat.MountedNetId);
            auto vehicle = mounted != m_mirrors.end() ? EntityOf(mounted->second.EntityHash) : RED4ext::Handle<RED4ext::IScriptable>();
            if (vehicle)
            {
                Unmount(vehicle, body);
            }
            m_log("vehicles: peer " + std::to_string(peerNumber) + " body got out of " + NetIdText(seat.MountedNetId));
            seat.MountedNetId = 0;
            seat.MountedSlot = 0;
            bodies.SetSeated(peerNumber, false);
        }
        if (!wantsSeat || seat.MountedNetId || seat.Tries >= MostMountTries || nowMs < seat.NextTryMs)
        {
            continue;
        }
        auto vehicle = EntityOf(m_mirrors[seat.NetId].EntityHash);
        if (!vehicle)
        {
            continue;
        }
        ++seat.Tries;
        seat.NextTryMs = nowMs + MountRetryMs;
        bodies.SetSeated(peerNumber, true);
        if (Mount(peerNumber, vehicle, body, seat.Slot))
        {
            seat.MountedNetId = seat.NetId;
            seat.MountedSlot = seat.Slot;
        }
        else if (seat.Tries >= MostMountTries)
        {
            bodies.SetSeated(peerNumber, false);
        }
    }
}

void Vehicles::RemoveMirror(uint32_t netId, Mirror& mirror, RemoteBodies* bodies, const char* why)
{
    for (auto& [peerNumber, seat] : m_seats)
    {
        if (seat.MountedNetId == netId)
        {
            RED4ext::Handle<RED4ext::IScriptable> body;
            auto vehicle = EntityOf(mirror.EntityHash);
            if (bodies && vehicle && bodies->GetBody(peerNumber, body))
            {
                Unmount(vehicle, body);
            }
            if (bodies)
            {
                bodies->SetSeated(peerNumber, false);
            }
            seat.MountedNetId = 0;
        }
    }
    if (mirror.EntityHash && m_dynamicEntitySystem)
    {
        RED4ext::ent::EntityID entityId(mirror.EntityHash);
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &entityId);
        game::CallMethod(m_dynamicEntitySystem.instance, m_deleteEntity, nullptr, args);
    }
    m_log("vehicles: mirror of " + NetIdText(netId) + " removed, " + why + " (" + std::to_string(mirror.Teleports) + " placement(s))");
}

void Vehicles::Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, RemoteBodies& bodies)
{
    const auto status = session.GetStatus();
    if (!inSave || !status.InSession)
    {
        if (!m_mirrors.empty())
        {
            m_mirrors.clear();
            m_seats.clear();
            m_dynamicEntitySystem.Reset();
            m_teleportationFacility.Reset();
            m_workspotSystem.Reset();
        }
        m_share = OwnShare {};
        return;
    }
    if (!Resolve() || !FetchSystems())
    {
        return;
    }
    ShareOurCar(nowMs, session);
    const uint64_t showMs = session.SessionClockMs() - ShowDelayMs;
    for (auto mirrorEntry = m_mirrors.begin(); mirrorEntry != m_mirrors.end();)
    {
        auto& [netId, mirror] = *mirrorEntry;
        if (nowMs - mirror.LastHeardMs > ForgetMirrorAfterMs)
        {
            RemoveMirror(netId, mirror, &bodies, mirror.LastHeardMs == 1 ? "its owner stopped sharing it" : "nothing heard of it for 10 s");
            mirrorEntry = m_mirrors.erase(mirrorEntry);
            continue;
        }
        if (!mirror.EntityHash && (mirror.SpawnRequestedMs == 0 || nowMs - mirror.SpawnRequestedMs > RetrySpawnAfterMs))
        {
            SpawnMirror(netId, mirror, nowMs);
        }
        else if (mirror.EntityHash)
        {
            PlaceMirror(netId, mirror, showMs, nowMs);
        }
        ++mirrorEntry;
    }
    SeatPartners(nowMs, bodies);
}
}
