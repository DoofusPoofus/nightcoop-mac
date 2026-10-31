#include "plugin/MessageDrops.hpp"

#include "net/DroppedMessages.hpp"
#include "net/GameMessages.hpp"

#include <ProtocolCore.hpp>

#include <map>
#include <set>

namespace nightcoop_mac::message_drops
{
namespace
{
constexpr uint64_t ReportEveryMs = 10000;

struct TypeCount
{
    uint32_t Messages = 0;
    uint32_t Broken = 0;
    uint64_t Bytes = 0;
    std::set<uint32_t> Peers;
};

std::map<uint8_t, TypeCount> CountsSinceReport;
uint64_t NextReportMs = 0;
uint64_t DroppedEver = 0;

bool ReadElsewhere(uint8_t type)
{
    return type == static_cast<uint8_t>(nc::proto::MsgType::PlayerAnim) || type == static_cast<uint8_t>(nc::proto::MsgType::PlayerAnimSetup);
}
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs)
{
    const uint8_t type = net::MessageTypeOf(message);
    if (ReadElsewhere(type))
    {
        return;
    }
    auto& count = CountsSinceReport[type];
    ++count.Messages;
    count.Bytes += message.size();
    count.Peers.insert(fromPeer);
    if (net::CheckDroppedMessage(message) == net::DropCheck::Broken)
    {
        ++count.Broken;
    }
    ++DroppedEver;
    if (NextReportMs == 0)
    {
        NextReportMs = nowMs + ReportEveryMs;
    }
}

void Tick(uint64_t nowMs, const LogFunction& log)
{
    if (NextReportMs == 0 || nowMs < NextReportMs)
    {
        return;
    }
    NextReportMs = nowMs + ReportEveryMs;
    if (CountsSinceReport.empty())
    {
        return;
    }
    std::string line = "drops: in 10 s nobody here read";
    bool first = true;
    for (const auto& [type, count] : CountsSinceReport)
    {
        line += first ? " " : ", ";
        first = false;
        line += net::DroppedMessageName(type) + " x" + std::to_string(count.Messages) + " (" + std::to_string(count.Bytes) + " bytes from " +
                std::to_string(count.Peers.size()) + " peer(s)";
        if (count.Broken)
        {
            line += ", " + std::to_string(count.Broken) + " with a size the dll would refuse";
        }
        line += ")";
    }
    log(line + ", " + std::to_string(DroppedEver) + " dropped since the start");
    CountsSinceReport.clear();
}
}
