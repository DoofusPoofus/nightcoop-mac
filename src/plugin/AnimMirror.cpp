#include "plugin/AnimMirror.hpp"

#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/RemoteBodies.hpp"

#include <ProtocolCore.hpp>

#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include <cstdio>
#include <cstring>

namespace nightcoop_mac
{
namespace
{
constexpr size_t MaxPendingBatches = 64;
constexpr uint64_t WantAtMostEveryMs = 1000;
constexpr uint64_t ReportEveryMs = 10000;
constexpr uint8_t PlayerGraphSlot = 0;

struct QuaternionLayout
{
    float I;
    float J;
    float K;
    float R;
};
}

bool AnimMirror::Resolve()
{
    m_resolved = true;
    m_getAnimationController = game::FindFunction("ScriptedPuppet", "GetAnimationControllerComponent");
    m_pushEvent = game::FindFunction("entAnimationControllerComponent", "PushEvent");
    m_setInputFloat = game::FindFunction("entAnimationControllerComponent", "SetInputFloat");
    m_setInputInt = game::FindFunction("entAnimationControllerComponent", "SetInputInt");
    m_setInputBool = game::FindFunction("entAnimationControllerComponent", "SetInputBool");
    m_setInputVector = game::FindFunction("entAnimationControllerComponent", "SetInputVector");
    m_setInputQuaternion = game::FindFunction("entAnimationControllerComponent", "SetInputQuaternion");
    m_applyFeature = game::FindFunction("entAnimationControllerComponent", "ApplyFeature");
    m_scriptedPuppetClass = RED4ext::CRTTISystem::Get()->GetClass("ScriptedPuppet");
    m_animFeatureClass = RED4ext::CRTTISystem::Get()->GetClass("animAnimFeature");
    m_available = m_scriptedPuppetClass && m_animFeatureClass && game::ParamsAre(m_getAnimationController, {}) &&
                  game::ReturnIs(m_getAnimationController, "handle:entAnimationControllerComponent") &&
                  game::ParamsAre(m_pushEvent, {"CName"}) && game::ParamsAre(m_setInputFloat, {"CName", "Float"}) &&
                  game::ParamsAre(m_setInputInt, {"CName", "Int32"}) && game::ParamsAre(m_setInputBool, {"CName", "Bool"}) &&
                  game::ParamsAre(m_setInputVector, {"CName", "Vector4"}) && game::ParamsAre(m_setInputQuaternion, {"CName", "Quaternion"}) &&
                  game::ParamsAre(m_applyFeature, {"CName", "handle:animAnimFeature"});
    m_log(m_available ? "mirror: partner anim streams play on their bodies thru the anim controller setters"
                      : "mirror: the anim controller setters look different than expected, partner anim streams only get counted");
    return m_available;
}

void AnimMirror::OnGameMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs)
{
    const uint8_t type = net::MessageTypeOf(message);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::PlayerAnim))
    {
        auto& stream = m_streams[fromPeer];
        net::AnimBatch batch;
        if (!net::ReadAnimBatch(message, batch))
        {
            ++stream.Undecodable;
            return;
        }
        ++stream.Batches;
        stream.Inputs += static_cast<uint32_t>(batch.Entries.size());
        stream.FullStates += batch.FullState ? 1 : 0;
        if (stream.Pending.size() >= MaxPendingBatches)
        {
            stream.Pending.erase(stream.Pending.begin());
            ++stream.Dropped;
        }
        stream.Pending.push_back(std::move(batch));
        if (stream.NextReportMs == 0)
        {
            stream.NextReportMs = nowMs + ReportEveryMs;
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::PlayerAnimWant))
    {
        if (m_wantLogged.insert(fromPeer).second)
        {
            m_log("mirror: partner " + std::to_string(fromPeer) + " asks for our animation stream, the mac port cant record it yet");
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::PlayerAnimSetup))
    {
        net::AnimSetup setup;
        auto& stream = m_streams[fromPeer];
        if (net::ReadAnimSetup(message, setup) && setup.Rig != stream.Rig)
        {
            stream.Rig = setup.Rig;
            char line[128];
            std::snprintf(line, sizeof(line), "mirror: partner %u rig %#llx and %u animation set(s)", fromPeer,
                          static_cast<unsigned long long>(setup.Rig), setup.SetCount);
            m_log(line);
        }
    }
}

bool AnimMirror::ApplyFeature(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry)
{
    auto* featureClass = RED4ext::CRTTISystem::Get()->GetClass(RED4ext::CName(entry.FeatureType));
    if (!featureClass || !featureClass->IsA(m_animFeatureClass))
    {
        return false;
    }
    auto* feature = static_cast<RED4ext::IScriptable*>(featureClass->CreateInstance(true));
    if (!feature)
    {
        return false;
    }
    RED4ext::Handle<RED4ext::IScriptable> featureHandle(feature);
    for (const auto& field : entry.Fields)
    {
        auto* property = featureClass->GetProperty(RED4ext::CName(field.Name));
        if (!property || !property->type || property->type->GetSize() != field.Bytes.size())
        {
            continue;
        }
        std::memcpy(property->GetValuePtr<uint8_t>(feature), field.Bytes.data(), field.Bytes.size());
    }
    RED4ext::CName inputName(entry.Name);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &inputName);
    args.emplace_back(nullptr, &featureHandle);
    return game::CallMethod(controller.instance, m_applyFeature, nullptr, args);
}

void AnimMirror::Play(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry, PartnerStream& stream)
{
    if (entry.Slot != PlayerGraphSlot)
    {
        ++stream.OtherSlots;
        return;
    }
    RED4ext::CName inputName(entry.Name);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &inputName);
    float floatValue = entry.Values[0];
    int32_t intValue = entry.IntValue;
    bool boolValue = entry.IntValue != 0;
    RED4ext::Vector4 vectorValue {entry.Values[0], entry.Values[1], entry.Values[2], entry.Values[3]};
    QuaternionLayout quaternionValue {entry.Values[0], entry.Values[1], entry.Values[2], entry.Values[3]};
    RED4ext::CBaseFunction* setter = nullptr;
    switch (entry.Kind)
    {
    case net::AnimKind::Feature:
        if (ApplyFeature(controller, entry))
        {
            ++stream.Applied;
        }
        else
        {
            ++stream.Failed;
        }
        return;
    case net::AnimKind::Float:
        setter = m_setInputFloat;
        args.emplace_back(nullptr, &floatValue);
        break;
    case net::AnimKind::Int:
        setter = m_setInputInt;
        args.emplace_back(nullptr, &intValue);
        break;
    case net::AnimKind::Bool:
        setter = m_setInputBool;
        args.emplace_back(nullptr, &boolValue);
        break;
    case net::AnimKind::Vector:
        setter = m_setInputVector;
        args.emplace_back(nullptr, &vectorValue);
        break;
    case net::AnimKind::Quaternion:
        setter = m_setInputQuaternion;
        args.emplace_back(nullptr, &quaternionValue);
        break;
    case net::AnimKind::Event: setter = m_pushEvent; break;
    case net::AnimKind::Wrapper:
    case net::AnimKind::Attach:
    case net::AnimKind::Detach: ++stream.Unsupported; return;
    }
    if (game::CallMethod(controller.instance, setter, nullptr, args))
    {
        ++stream.Applied;
    }
    else
    {
        ++stream.Failed;
    }
}

void AnimMirror::Report(uint32_t peerNumber, PartnerStream& stream, uint64_t nowMs)
{
    if (stream.NextReportMs == 0 || nowMs < stream.NextReportMs)
    {
        return;
    }
    stream.NextReportMs = nowMs + ReportEveryMs;
    char line[300];
    std::snprintf(line, sizeof(line),
                  "mirror: partner %u stream: %u batch(es), %u input(s), %u full state(s), %u undecodable, %u dropped; played %u, failed %u, "
                  "no setter %u, other slots %u, without a body %u",
                  peerNumber, stream.Batches, stream.Inputs, stream.FullStates, stream.Undecodable, stream.Dropped, stream.Applied, stream.Failed,
                  stream.Unsupported, stream.OtherSlots, stream.WithoutBody);
    m_log(line);
}

void AnimMirror::Tick(uint64_t nowMs, RemoteBodies& bodies, net::RelaySession& session)
{
    if (m_streams.empty())
    {
        return;
    }
    if (!m_resolved)
    {
        Resolve();
    }
    for (auto& [peerNumber, stream] : m_streams)
    {
        RED4ext::Handle<RED4ext::IScriptable> body;
        const bool bodyIsThere = bodies.GetBody(peerNumber, body);
        if (!bodyIsThere)
        {
            stream.BodyWasThere = false;
            for (const auto& batch : stream.Pending)
            {
                stream.WithoutBody += static_cast<uint32_t>(batch.Entries.size());
            }
            stream.Pending.clear();
            Report(peerNumber, stream, nowMs);
            continue;
        }
        if (!stream.BodyWasThere && nowMs - stream.LastWantMs >= WantAtMostEveryMs)
        {
            stream.BodyWasThere = true;
            stream.LastWantMs = nowMs;
            session.SendGameMessage(net::WriteAnimWant({true, true}), static_cast<int32_t>(peerNumber));
            m_log("mirror: body of partner " + std::to_string(peerNumber) + " is here, asking them for their full animation state");
        }

        RED4ext::Handle<RED4ext::IScriptable> controller;
        auto* bodyClass = body.instance->GetType();
        RED4ext::StackArgs_t noArgs;
        const bool controllerFound = m_available && bodyClass && bodyClass->IsA(m_scriptedPuppetClass) &&
                                     game::CallMethod(body.instance, m_getAnimationController, &controller, noArgs) && controller;
        for (const auto& batch : stream.Pending)
        {
            for (const auto& entry : batch.Entries)
            {
                if (controllerFound)
                {
                    Play(controller, entry, stream);
                }
                else
                {
                    ++stream.Failed;
                }
            }
        }
        stream.Pending.clear();
        Report(peerNumber, stream, nowMs);
    }
}

void AnimMirror::ForgetAll()
{
    m_streams.clear();
    m_wantLogged.clear();
}
}
