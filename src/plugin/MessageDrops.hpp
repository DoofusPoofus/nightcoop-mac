/*
 message drops, the parse and drop readers for game messages nobody in the mac port reads yet
 evry game message no routing branch in Main took ends up here (AnimMirror reads PlayerAnim and PlayerAnimSetup on its own, those
 two are skipped). nothing gets applied: each one is checked against the sizes from the reports (net/DroppedMessages.hpp) and
 counted per type, and evry 10 s one log line says wat came in that nobody reads, how many and how many had a size the dll
 would refuse, so a windows peer spamming types we dont have shows up in the log instead of vanishing
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::message_drops
{
using LogFunction = std::function<void(const std::string&)>;

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, uint64_t nowMs);
void Tick(uint64_t nowMs, const LogFunction& log);
}
