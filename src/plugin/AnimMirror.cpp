#include "plugin/AnimMirror.hpp"

#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/RemoteBodies.hpp"

#include <ProtocolCore.hpp>

#include <cstdio>
#include <cstring>

namespace nightcoop_mac
{
namespace
{
constexpr size_t MaxPendingBatches = 64;
constexpr uint64_t WantAtMostEveryMs = 1000;
constexpr uint64_t ReportEveryMs = 10000;
}

bool AnimMirror::Resolve()
{
    m_resolved = true;
    m_available = m_player.Ready();
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
            m_log("mirror: partner " + std::to_string(fromPeer) + " asks for our animation stream (AnimTap answers with our full state)");
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

void AnimMirror::Play(RED4ext::Handle<RED4ext::IScriptable>& controller, const net::AnimEntry& entry, PartnerStream& stream)
{
    switch (m_player.Play(controller, entry))
    {
    case AnimPlayer::Result::Applied: ++stream.Applied; break;
    case AnimPlayer::Result::Failed: ++stream.Failed; break;
    case AnimPlayer::Result::NoSetter: ++stream.Unsupported; break;
    case AnimPlayer::Result::OtherSlot: ++stream.OtherSlots; break;
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
        const bool controllerFound = m_available && m_player.ControllerOf(body, controller);
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
