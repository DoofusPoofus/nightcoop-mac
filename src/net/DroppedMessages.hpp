/*
 sizes and limits of the game messages windows nightcoop peers send that the mac port does not read yet, out of the reports
 (player extras, npc copies). nothing here gets applied, a reader only says if the message looks like what the dll would
 take, so the log can tell normal traffic from broken bytes
   PlayerCarry 92: 69 or 77 | PhoneSync 133: 0x25 + name length (u16 at 0x1f, 1..96) | PlayerFx 138: 6 + 41 n (n 1..24)
   BuildStats 141: 11 + 5 n (n up to 32) | SmsReply 142: 20 | ChallengeRules 135: 13 | Braindance 137: 54
   BraindanceGhost 149: 55 | BraindanceClue 150: 31 | BraindanceSource 157: 35 | PhotoTogether 209: 6 | photo pose 226: 71
   PlayerBlast 131: 6 + 24 n (n 1..8) | PlayerGrenade 99 and NpcGrenade 24: 66 | NpcHandoff 26: 135 + 2 n (n up to 7)
   NpcHandoffAck 27: 19 | NpcClaim 28: 14 | NpcTakeRequest 91: 10 | perk dilation 251: size not known
 any other type has no size rule, it only gets a name when ProtocolCore has one
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kPhotoPoseType = 226;
inline constexpr uint8_t kPerkDilationType = 251;

enum class DropCheck
{
    NoSizeRule,
    LooksRight,
    Broken
};

DropCheck CheckDroppedMessage(const std::vector<uint8_t>& message);
std::string DroppedMessageName(uint8_t type);
}
