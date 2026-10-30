/*
 anim mirror, the mac take on nightcoops AnimMirror receive side
 partners send their anim graph inputs as PlayerAnim batches (net/GameMessages.hpp). this queues them per partner and plays
 them on that partners body thru its entAnimationControllerComponent, like nightcoops replay does with the native setters:
   feature -> ApplyFeature (the feature class and its fields are found by their name hashes), float -> SetInputFloat,
   int -> SetInputInt, bool -> SetInputBool, vector -> SetInputVector, quaternion -> SetInputQuaternion, event -> PushEvent
 wrapper weights and graph attach / detach have no script reachable setter, they only get counted
 only slot 0 (the player graph) gets played, the body has no weapon to play slot 1 on
 when a partners body shows up it asks them for their full state with PlayerAnimWant, at most once a second like nightcoop
 evry 10 s it logs the stream per partner the way nightcoop does (batches, inputs, full states, undecodable, dropped)
 our own stream is not sent: recording it needs nightcoops native AnimTap hooks, so a PlayerAnimWant to us only gets logged
*/
#pragma once

#include "net/GameMessages.hpp"

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nightcoop_mac
{
class RemoteBodies;
}
namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac
{
class AnimMirror
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    explicit AnimMirror(LogFunction log)
        : m_log(std::move(log))
    {
    }

    void OnGameMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs);
    void Tick(uint64_t nowMs, RemoteBodies& bodies, net::RelaySession& session);
    void ForgetAll();

private:
    struct PartnerStream
    {
        std::vector<net::AnimBatch> Pending;
        uint32_t Batches = 0;
        uint32_t Inputs = 0;
        uint32_t FullStates = 0;
        uint32_t Undecodable = 0;
        uint32_t Dropped = 0;
        uint32_t Applied = 0;
        uint32_t Failed = 0;
        uint32_t Unsupported = 0;
        uint32_t OtherSlots = 0;
        uint32_t WithoutBody = 0;
        bool BodyWasThere = false;
        uint64_t LastWantMs = 0;
        uint64_t NextReportMs = 0;
        uint64_t Rig = 0;
    };

    bool Resolve();
    void Play(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry, PartnerStream& stream);
    bool ApplyFeature(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry);
    void Report(uint32_t peerNumber, PartnerStream& stream, uint64_t nowMs);

    LogFunction m_log;
    bool m_resolved = false;
    bool m_available = false;
    RED4ext::CBaseFunction* m_getAnimationController = nullptr;
    RED4ext::CBaseFunction* m_pushEvent = nullptr;
    RED4ext::CBaseFunction* m_setInputFloat = nullptr;
    RED4ext::CBaseFunction* m_setInputInt = nullptr;
    RED4ext::CBaseFunction* m_setInputBool = nullptr;
    RED4ext::CBaseFunction* m_setInputVector = nullptr;
    RED4ext::CBaseFunction* m_setInputQuaternion = nullptr;
    RED4ext::CBaseFunction* m_applyFeature = nullptr;
    RED4ext::CClass* m_scriptedPuppetClass = nullptr;
    RED4ext::CClass* m_animFeatureClass = nullptr;

    std::map<uint32_t, PartnerStream> m_streams;
    std::set<uint32_t> m_wantLogged;
};
}
