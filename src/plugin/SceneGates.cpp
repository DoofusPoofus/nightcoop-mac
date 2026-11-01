#include "plugin/SceneGates.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"

#include <ProtocolCore.hpp>

#include <cmath>
#include <cstdio>
#include <map>

namespace nightcoop_mac::scene_gates
{
namespace
{
constexpr float AtGateMeters = 12.0f;
constexpr uint64_t RefreshEveryMs = 5000;
constexpr uint64_t LookEveryMs = 500;

struct Gate
{
    uint8_t HostState = 0;
    float HostPlace[3] = {0, 0, 0};
    std::vector<uint16_t> Nodes;
    uint8_t OurState = 0xFF;
    uint64_t NextSendMs = 0;
};

std::map<uint64_t, Gate> Gates;
uint64_t NextLookMs = 0;

std::vector<uint8_t> WriteGate(uint64_t scene, uint8_t state, uint32_t peer, const float* place, const std::vector<uint16_t>& nodes)
{
    nc::proto::Writer writer(nc::proto::MsgType::SceneGate);
    writer.PutU64(scene);
    writer.PutU8(state);
    writer.PutU32(peer);
    for (int axis = 0; axis < 3; ++axis)
    {
        writer.PutF32(place[axis]);
    }
    writer.PutU8(0);
    writer.PutU8(static_cast<uint8_t>(std::min<size_t>(nodes.size(), 8)));
    for (size_t index = 0; index < nodes.size() && index < 8; ++index)
    {
        writer.PutU16(nodes[index]);
    }
    return writer.Data();
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::SceneGate);
}

void Tick(uint64_t nowMs, bool inSave, bool inHostWorld, const net::PlayerState* ourPlayer, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || !inHostWorld || !ourPlayer)
    {
        Gates.clear();
        return;
    }
    if (nowMs < NextLookMs)
    {
        return;
    }
    NextLookMs = nowMs + LookEveryMs;
    const float ourPlace[3] = {ourPlayer->PositionX, ourPlayer->PositionY, ourPlayer->PositionZ};
    for (auto gate = Gates.begin(); gate != Gates.end();)
    {
        uint8_t state = gate->second.HostState;
        if (state == 1 || state == 2)
        {
            const float x = ourPlace[0] - gate->second.HostPlace[0];
            const float y = ourPlace[1] - gate->second.HostPlace[1];
            const float z = ourPlace[2] - gate->second.HostPlace[2];
            state = std::sqrt(x * x + y * y + z * z) <= AtGateMeters ? 2 : 1;
        }
        if (state != gate->second.OurState || nowMs >= gate->second.NextSendMs)
        {
            if (state != gate->second.OurState)
            {
                char line[128];
                std::snprintf(line, sizeof(line), "scene gates: we tell the host we are %s at its gate in scene %#llx",
                              state == 2 ? "there" : state == 1 ? "not yet" : state == 3 ? "past it" : "gone", static_cast<unsigned long long>(gate->first));
                log(line);
            }
            gate->second.OurState = state;
            gate->second.NextSendMs = nowMs + RefreshEveryMs;
            session.SendGameMessage(WriteGate(gate->first, state, session.GetStatus().MyPeerNumber, ourPlace, gate->second.Nodes), 0);
        }
        if (gate->second.HostState == 0 || gate->second.HostState == 3)
        {
            gate = Gates.erase(gate);
            continue;
        }
        ++gate;
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (weAreHost || fromPeer != 0 || reader.ReadHeader() != nc::proto::MsgType::SceneGate)
    {
        return;
    }
    const uint64_t scene = reader.GetU64();
    const uint8_t state = reader.GetU8();
    const uint32_t peer = reader.GetU32();
    float place[3];
    for (float& axis : place)
    {
        axis = reader.GetF32();
    }
    const uint8_t flags = reader.GetU8();
    const uint8_t count = reader.GetU8();
    std::vector<uint16_t> nodes;
    for (uint8_t index = 0; index < count && index < 8; ++index)
    {
        nodes.push_back(reader.GetU16());
    }
    if (!reader.Ok() || reader.Remaining() != 0 || scene == 0 || state > 3 || flags >= 0x80 || count > 8 || peer != 0 || !std::isfinite(place[0]) ||
        !std::isfinite(place[1]) || !std::isfinite(place[2]))
    {
        return;
    }
    auto& gate = Gates[scene];
    if (gate.HostState != state)
    {
        char line[128];
        std::snprintf(line, sizeof(line), "scene gates: the hosts scene %#llx is at state %u", static_cast<unsigned long long>(scene), state);
        log(line);
    }
    gate.HostState = state;
    std::copy(place, place + 3, gate.HostPlace);
    gate.Nodes = nodes;
}
}
