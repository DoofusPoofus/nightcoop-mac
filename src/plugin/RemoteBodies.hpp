/*
 remote bodies, the mac take on nightcoops puppets module
 one puppet per partner that sends PlayerStates. same rules as nightcoop:
   spawn a body once the partner is within 150 m of us, take it away when they get over 200 m
   let the npc walk there with its own locomotion: a movePolicies object on its movePoliciesComponent gets the partners
   spot as destination and a point 12 m ahead along their look as strafing target, the gait comes from their speed
   (walk / run / sprint with nightcoops thresholds and hysteresis)
   if the body is over 8 m off or has no move policies it gets put there by the games teleport, and so does a body whose
   policy does nothing for 3 s while the partner is over 1 m away (the main menu world never moves npcs for example)
   (gameTeleportationFacility.Teleport, only when they moved over 2 cm or turned over 0.5 deg)
   stance like nightcoops ApplyStance: the partners crouch / aim / reload flags become an NPCStateChangeSignal
   (stance Crouch or Stand, upper body Aim / Reload / Normal) on the bodys signal table
   despawn when they leave (no state for 5 s), forget it when our world goes away (it goes with the world)
 bodies only exist while SessionFlow allows them (a loaded save): in the main menu or a session thats ending they get removed
 nightcoop spawns thru native calls on gameIDynamicEntityIDSystem / gameIEntityStubSystem / gameIPopulationSystem.
 those slots are not verified on mac so the body comes from codewares DynamicEntitySystem instead
 (CreateEntity / GetEntity / DeleteEntity), which needs codeware for mac installed
 the default body is nightcoops fallback body: the cutscene V template (player_wa_tpp_cutscene.ent or player_ma_ by the
 partners gender from their PlayerCustomization), which spawns as an NPCPuppet on mac so the npc locomotion and stance work on it.
 'body player' in session.txt gives nightcoops real body instead: a second PlayerPuppet from the template family our own player
 came from (ep1 or base, player_wa_fpp / player_ma_fpp by the partners gender) with a character record cloned for that template
 (BodyRecords). it carries the NightCoopPartnerBody tag for the script side, its id goes to the
 PartnerBodyBuilder before the game builds it, and once its in the world it gets an ActivateTPPRepresentationEvent so it shows
 its third person self. it moves by teleport and its anims come from the partners stream (AnimMirror), no npc policies or stance.
 if the builder hook or the script side (scripts/NightCoopMac, the NightCoopPartnerBodies class in rtti) is not there the
 cutscene V body is used: an unbuilt second player brings a second camera and hud, an unpatched one attaches as a local player.
 'body player_without_scripts' skips the script check, only for testing the builder at the main menu
 like nightcoop a body waits up to 5 s for the partners look before it spawns. body_record / body_template override it.
 bodies get character records like nightcoops: Character.TPP_Player_Cutscene_Male / _Female for the V body and
 Character.Player_Puppet_Base for the PlayerPuppet one. a template path alone makes codeware invent a SpawnableObject record
 (TweakDBID(hash, 1)), and the bodys character customization parts read its record as a character record, get nothing and
 crash the game a moment after the body is built (found with crash notes: HairstyleController, FeetController ...). codeware
 cant spawn from a template and a record at once (nightcoop does, natively), and writing the record into the spawn request
 from the build hook comes too late, the body keeps the invented one
 when the game closes the handles we hold get leaked on purpose, the game tears its objects down first
*/
#pragma once

#include "net/SessionMessages.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/ResourcePath.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nightcoop_mac
{
struct BodySettings
{
    bool Enabled = true;
    bool PlayerBody = false;
    bool PlayerBodyWithoutScripts = false;
    bool InMenuForTesting = false;
    std::string Record;
    std::string Template;
    std::string Gender;
};

struct RemotePeerView
{
    uint32_t PeerNumber = 0;
    net::PlayerState State;
    bool LookKnown = false;
    bool IsMale = false;
    RED4ext::Handle<RED4ext::IScriptable> LookState;
};

class RemoteBodies
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    RemoteBodies(LogFunction log, BodySettings settings);

    void Tick(uint64_t nowMs, const std::vector<RemotePeerView>& peers, const net::PlayerState* localPlayer, bool bodiesAllowed);
    void RemoveAll();
    void ForgetAll(const char* why, bool gameIsClosing = false);
    bool GetBody(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body);
    void UseLook(uint32_t peerNumber, const RED4ext::Handle<RED4ext::IScriptable>& state, bool isMale);

private:
    struct Puppet
    {
        uint64_t EntityHash = 0;
        uint64_t SpawnRequestedMs = 0;
        bool InWorld = false;
        bool OutOfWorldLogged = false;
        bool Placed = false;
        float PlacedX = 0;
        float PlacedY = 0;
        float PlacedZ = 0;
        float PlacedYaw = 0;
        uint32_t Teleports = 0;
        RED4ext::Handle<RED4ext::IScriptable> MovePolicies;
        RED4ext::Handle<RED4ext::IScriptable> MovePoliciesComponent;
        bool PolicyTried = false;
        float SmoothedSpeed = 0;
        uint32_t MovementType = 0;
        uint64_t MovementTypeChangedMs = 0;
        uint32_t AppliedMovementType = 0xFFFFFFFF;
        uint64_t NextReportMs = 0;
        uint64_t PolicyIdleSinceMs = 0;
        uint32_t AppliedStance = 0xFFFFFFFF;
        uint32_t AppliedUpperBody = 0xFFFFFFFF;
        uint64_t UpperBodyChangedMs = 0;
        bool IsPlayerBody = false;
    };

    bool ResolveApi();
    void ResolveLocomotion();
    bool FetchSystems();
    bool Spawn(const RemotePeerView& peer, uint64_t nowMs);
    void Despawn(uint32_t peerNumber, Puppet& puppet, const char* why);
    void Advance(uint32_t peerNumber, Puppet& puppet, const net::PlayerState& state, uint64_t nowMs);
    void MoveTo(Puppet& puppet, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state);
    bool EnsurePolicy(uint32_t peerNumber, Puppet& puppet, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state);
    uint32_t PickMovementType(Puppet& puppet, const net::PlayerState& state, uint64_t nowMs);
    void ReportPolicy(uint32_t peerNumber, Puppet& puppet, float drift, uint64_t nowMs);
    bool PolicyIsStuck(uint32_t peerNumber, Puppet& puppet, float drift, uint64_t nowMs);
    void DropPolicy(Puppet& puppet);
    void ResolveStance();
    void ApplyStance(uint32_t peerNumber, Puppet& puppet, RED4ext::Handle<RED4ext::IScriptable>& body, const net::PlayerState& state, uint64_t nowMs);
    void ResolvePlayerBodies();
    void ShowThirdPerson(uint32_t peerNumber, RED4ext::Handle<RED4ext::IScriptable>& body);
    std::string PlayerBodyTemplate(bool male);
    bool TagAsPartnerBody(RED4ext::IScriptable* spec);

    LogFunction m_log;
    BodySettings m_settings;
    bool m_resolved = false;
    bool m_givenUp = false;

    RED4ext::CBaseFunction* m_getTeleportationFacility = nullptr;
    RED4ext::CBaseFunction* m_teleport = nullptr;
    RED4ext::CBaseFunction* m_getDynamicEntitySystem = nullptr;
    RED4ext::CBaseFunction* m_isReady = nullptr;
    RED4ext::CBaseFunction* m_createEntity = nullptr;
    RED4ext::CBaseFunction* m_getEntity = nullptr;
    RED4ext::CBaseFunction* m_deleteEntity = nullptr;
    RED4ext::CClass* m_specClass = nullptr;
    RED4ext::CClass* m_gameObjectClass = nullptr;

    bool m_locomotionAvailable = false;
    RED4ext::CBaseFunction* m_getWorldPosition = nullptr;
    RED4ext::CBaseFunction* m_getMovePoliciesComponent = nullptr;
    RED4ext::CClass* m_scriptedPuppetClass = nullptr;
    RED4ext::CClass* m_movePoliciesClass = nullptr;
    RED4ext::CBaseFunction* m_setDestinationPosition = nullptr;
    RED4ext::CBaseFunction* m_setStrafingPosition = nullptr;
    RED4ext::CBaseFunction* m_setIgnoreNavigation = nullptr;
    RED4ext::CBaseFunction* m_setUseStartStop = nullptr;
    RED4ext::CBaseFunction* m_setCollisionAvoidancePolicy = nullptr;
    RED4ext::CBaseFunction* m_setMovementType = nullptr;
    RED4ext::CBaseFunction* m_addPolicies = nullptr;
    RED4ext::CBaseFunction* m_popPolicies = nullptr;
    RED4ext::CBaseFunction* m_changeMovementType = nullptr;
    RED4ext::CBaseFunction* m_getCurrentLocomotionAction = nullptr;
    RED4ext::CBaseFunction* m_getDistanceToDestination = nullptr;
    RED4ext::CBaseFunction* m_isPathfindingFailed = nullptr;
    RED4ext::CBaseFunction* m_isPausedByDynamicCollision = nullptr;

    bool m_stanceAvailable = false;
    RED4ext::CBaseFunction* m_getSignalTable = nullptr;
    RED4ext::CBaseFunction* m_getOrCreateSignal = nullptr;
    RED4ext::CBaseFunction* m_setWithData = nullptr;
    RED4ext::CClass* m_stateSignalClass = nullptr;

    bool m_playerBodies = false;
    RED4ext::CBaseFunction* m_getPlayer = nullptr;
    RED4ext::CBaseFunction* m_getTemplatePath = nullptr;
    RED4ext::CBaseFunction* m_queueEvent = nullptr;
    RED4ext::CClass* m_activateTppEventClass = nullptr;
    int m_playerTemplateFamily = -1;

    RED4ext::Handle<RED4ext::IScriptable> m_teleportationFacility;
    RED4ext::Handle<RED4ext::IScriptable> m_dynamicEntitySystem;

    std::map<uint32_t, Puppet> m_puppets;
    std::map<uint32_t, uint64_t> m_retryAfterMs;
    std::map<uint32_t, uint64_t> m_firstSeenMs;
};
}
