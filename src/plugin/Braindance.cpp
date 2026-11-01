#include "plugin/Braindance.hpp"

#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>

namespace nightcoop_mac::braindance
{
namespace
{
constexpr uint64_t SendEveryMs = 500;
constexpr uint64_t OurTapeKey = 0x4E43424400000001ULL;

uint64_t NextSendMs = 0;
uint32_t WordNumber = 0;
bool WasInBraindance = false;

int32_t CallInt(const char* name)
{
    auto* function = game::FindFunction("NightCoopBraindance", name);
    auto service = game::ScriptService("NightCoopBraindance");
    int32_t value = 0;
    RED4ext::StackArgs_t noArgs;
    if (function && service)
    {
        game::CallMethod(service.instance, function, &value, noArgs);
    }
    return value;
}

bool InBraindance()
{
    static auto* inBraindance = game::FindFunction("NightCoopBraindance", "InBraindance");
    auto service = game::ScriptService("NightCoopBraindance");
    bool active = false;
    RED4ext::StackArgs_t noArgs;
    if (inBraindance && service)
    {
        game::CallMethod(service.instance, inBraindance, &active, noArgs);
    }
    return active;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::Braindance);
}

void Tick(uint64_t nowMs, bool inSave, const std::vector<uint32_t>& peersInOurWorld, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || nowMs < NextSendMs || session.GetStatus().MyPeerNumber != 0)
    {
        return;
    }
    NextSendMs = nowMs + SendEveryMs;
    const bool active = InBraindance();
    if (active != WasInBraindance)
    {
        WasInBraindance = active;
        log(active ? "braindance: we are in a braindance, the partners follow our tape" : "braindance: we left the braindance");
    }
    if (!active || peersInOurWorld.empty())
    {
        return;
    }
    const auto tapeMs = static_cast<uint32_t>(std::clamp(CallInt("TapeMs"), 0, 3600000));
    const auto lengthMs = static_cast<uint32_t>(std::clamp(CallInt("LengthMs"), 0, 3600000));
    nc::proto::Writer writer(nc::proto::MsgType::Braindance);
    writer.PutU8(1);
    writer.PutU32(++WordNumber);
    writer.PutU64(OurTapeKey);
    writer.PutU32(0);
    writer.PutU32(0);
    writer.PutU32(0);
    writer.PutU64(0);
    writer.PutU32(tapeMs);
    writer.PutU32(lengthMs);
    writer.PutU8(static_cast<uint8_t>(std::clamp(CallInt("Speed"), 0, 4)));
    writer.PutU8(static_cast<uint8_t>(std::clamp(CallInt("Direction"), 0, 1)));
    writer.PutU8(1);
    writer.PutU8(0);
    writer.PutF32(1.0f);
    const auto message = writer.Data();
    for (const uint32_t peer : peersInOurWorld)
    {
        session.SendGameMessage(message, static_cast<int32_t>(peer));
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool takeHostTape, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (fromPeer != 0 || !takeHostTape || reader.ReadHeader() != nc::proto::MsgType::Braindance)
    {
        return;
    }
    const uint8_t kind = reader.GetU8();
    reader.GetU32();
    reader.GetU64();
    reader.GetU32();
    reader.GetU32();
    reader.GetU32();
    reader.GetU64();
    int32_t tapeMs = static_cast<int32_t>(reader.GetU32());
    reader.GetU32();
    int32_t speed = reader.GetU8();
    int32_t direction = reader.GetU8();
    reader.GetU8();
    reader.GetU8();
    reader.GetF32();
    if (!reader.Ok() || reader.Remaining() != 0 || kind != 1 || tapeMs < 0 || tapeMs > 3600000 || speed > 4 || direction > 1)
    {
        return;
    }
    static auto* follow = game::FindFunction("NightCoopBraindance", "Follow");
    auto service = game::ScriptService("NightCoopBraindance");
    if (!follow || !service)
    {
        return;
    }
    int32_t result = 0;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &tapeMs);
    args.emplace_back(nullptr, &speed);
    args.emplace_back(nullptr, &direction);
    game::CallMethod(service.instance, follow, &result, args);
    if (result == 2)
    {
        log("braindance: our tape jumped to the hosts at " + std::to_string(tapeMs) + " ms");
    }
}
}
