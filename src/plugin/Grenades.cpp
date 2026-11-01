#include "plugin/Grenades.hpp"

#include "net/GrenadeMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <sstream>

namespace nightcoop_mac::grenades
{
namespace
{
constexpr uint64_t LookEveryMs = 100;
constexpr size_t RememberedBlasts = 64;

uint64_t NextLookMs = 0;
std::deque<uint64_t> SeenBlastIds;

std::string TakeBlastLines()
{
    static auto* takeBlasts = game::FindFunction("NightCoopGrenades", "TakeBlasts");
    auto service = game::ScriptService("NightCoopGrenades");
    if (!takeBlasts || !service)
    {
        return {};
    }
    RED4ext::CString lines;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, takeBlasts, &lines, noArgs);
    return lines.c_str();
}

bool PushAt(const net::Blast& blast)
{
    static auto* pushAt = game::FindFunction("NightCoopGrenades", "PushAt");
    auto service = game::ScriptService("NightCoopGrenades");
    if (!pushAt || !service)
    {
        return false;
    }
    float x = blast.Position[0];
    float y = blast.Position[1];
    float z = blast.Position[2];
    float radius = blast.Radius;
    bool pushed = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &x);
    args.emplace_back(nullptr, &y);
    args.emplace_back(nullptr, &z);
    args.emplace_back(nullptr, &radius);
    game::CallMethod(service.instance, pushAt, &pushed, args);
    return pushed;
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::PlayerBlast);
}

void Tick(uint64_t nowMs, bool inSave, bool partnerAround, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave || nowMs < NextLookMs)
    {
        return;
    }
    NextLookMs = nowMs + LookEveryMs;
    const std::string lines = TakeBlastLines();
    if (lines.empty() || !partnerAround || !session.GetStatus().InSession)
    {
        return;
    }
    std::vector<net::Blast> blasts;
    std::istringstream input(lines);
    net::Blast blast;
    while (input >> blast.Position[0] >> blast.Position[1] >> blast.Position[2] >> blast.Radius >> blast.Id)
    {
        blast.Radius = std::min(std::max(blast.Radius, 0.0f), 100.0f);
        blasts.push_back(blast);
    }
    for (size_t first = 0; first < blasts.size(); first += net::kMostBlastsAtOnce)
    {
        const std::vector<net::Blast> part(blasts.begin() + static_cast<std::ptrdiff_t>(first),
                                           blasts.begin() + static_cast<std::ptrdiff_t>(std::min(blasts.size(), first + net::kMostBlastsAtOnce)));
        session.SendGameMessage(net::WritePlayerBlast(part));
    }
    if (!blasts.empty())
    {
        char line[160];
        std::snprintf(line, sizeof(line), "grenades: our grenade went off at %.1f %.1f %.1f (radius %.1f m), the partners get the push", blasts[0].Position[0],
                      blasts[0].Position[1], blasts[0].Position[2], blasts[0].Radius);
        log(line);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool sameWorld, const LogFunction& log)
{
    std::vector<net::Blast> blasts;
    if (!net::ReadPlayerBlast(message, blasts) || !sameWorld)
    {
        return;
    }
    for (const auto& blast : blasts)
    {
        if (blast.Id && std::find(SeenBlastIds.begin(), SeenBlastIds.end(), blast.Id) != SeenBlastIds.end())
        {
            continue;
        }
        SeenBlastIds.push_back(blast.Id);
        if (SeenBlastIds.size() > RememberedBlasts)
        {
            SeenBlastIds.pop_front();
        }
        const bool pushed = PushAt(blast);
        char line[160];
        std::snprintf(line, sizeof(line), "grenades: peer %u blew something up at %.1f %.1f %.1f (radius %.1f m)%s", fromPeer, blast.Position[0], blast.Position[1],
                      blast.Position[2], blast.Radius, pushed ? ", pushed things here" : ", the push did not start");
        log(line);
    }
}
}
