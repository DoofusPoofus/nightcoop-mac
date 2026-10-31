#include "net/DroppedMessages.hpp"

#include "net/GameMessages.hpp"

#include <ProtocolCore.hpp>

#include <cstring>
#include <map>

namespace nightcoop_mac::net
{
namespace
{
using nc::proto::MsgType;

const std::map<MsgType, const char*> KnownNames = {
    {MsgType::HitClaim, "HitClaim"}, {MsgType::HitResult, "HitResult"}, {MsgType::NpcSpawn, "NpcSpawn"}, {MsgType::NpcSnapshot, "NpcSnapshot"},
    {MsgType::NpcDespawn, "NpcDespawn"}, {MsgType::NpcAnim, "NpcAnim"}, {MsgType::NpcGrenade, "NpcGrenade"}, {MsgType::NpcHandoff, "NpcHandoff"},
    {MsgType::NpcHandoffAck, "NpcHandoffAck"}, {MsgType::NpcClaim, "NpcClaim"}, {MsgType::PlayerAnim, "PlayerAnim"},
    {MsgType::PlayerAnimWant, "PlayerAnimWant"}, {MsgType::PlayerLook, "PlayerLook"}, {MsgType::PlayerHit, "PlayerHit"},
    {MsgType::PlayerCustomization, "PlayerCustomization"}, {MsgType::PlayerAnimSetup, "PlayerAnimSetup"}, {MsgType::NpcAnimWant, "NpcAnimWant"},
    {MsgType::NpcDead, "NpcDead"}, {MsgType::NpcLookAt, "NpcLookAt"}, {MsgType::ItemTransferOffer, "ItemTransferOffer"},
    {MsgType::ItemTransferDebit, "ItemTransferDebit"}, {MsgType::ItemTransferGrant, "ItemTransferGrant"},
    {MsgType::ItemTransferAck, "ItemTransferAck"}, {MsgType::LootState, "LootState"}, {MsgType::LootTake, "LootTake"},
    {MsgType::LootTakeResult, "LootTakeResult"}, {MsgType::SessionGather, "SessionGather"}, {MsgType::GameClock, "GameClock"},
    {MsgType::PlayerRevive, "PlayerRevive"}, {MsgType::DownedNotice, "DownedNotice"}, {MsgType::VehicleSpawn, "VehicleSpawn"},
    {MsgType::VehicleState, "VehicleState"}, {MsgType::VehicleDespawn, "VehicleDespawn"}, {MsgType::VehicleSeat, "VehicleSeat"},
    {MsgType::VehicleHandover, "VehicleHandover"}, {MsgType::VehicleHit, "VehicleHit"}, {MsgType::QuestFacts, "QuestFacts"},
    {MsgType::QuestJournal, "QuestJournal"}, {MsgType::QuestScene, "QuestScene"}, {MsgType::QuestWant, "QuestWant"},
    {MsgType::DialogBallot, "DialogBallot"}, {MsgType::DialogVote, "DialogVote"}, {MsgType::DialogTally, "DialogTally"},
    {MsgType::QuestTeleport, "QuestTeleport"}, {MsgType::DialogLine, "DialogLine"}, {MsgType::DialogGather, "DialogGather"},
    {MsgType::WorldWant, "WorldWant"}, {MsgType::WorldFile, "WorldFile"}, {MsgType::WorldStatus, "WorldStatus"}, {MsgType::SceneGate, "SceneGate"},
    {MsgType::QuestRide, "QuestRide"}, {MsgType::PlayerControl, "PlayerControl"}, {MsgType::LiftSync, "LiftSync"}, {MsgType::StoryPull, "StoryPull"},
    {MsgType::DoorSync, "DoorSync"}, {MsgType::SceneSkip, "SceneSkip"}, {MsgType::PlayerWorkspot, "PlayerWorkspot"},
    {MsgType::NpcTakeRequest, "NpcTakeRequest"}, {MsgType::PlayerCarry, "PlayerCarry"}, {MsgType::NpcWorkspot, "NpcWorkspot"},
    {MsgType::QuestClock, "QuestClock"}, {MsgType::WorldReload, "WorldReload"}, {MsgType::LiftRide, "LiftRide"}, {MsgType::NpcDefeat, "NpcDefeat"},
    {MsgType::PlayerGrenade, "PlayerGrenade"}, {MsgType::Kick, "Kick"}, {MsgType::PlayerPresence, "PlayerPresence"},
    {MsgType::SessionRoster, "SessionRoster"}, {MsgType::LobbyStart, "LobbyStart"}, {MsgType::CreationHover, "CreationHover"},
    {MsgType::SceneGateGo, "SceneGateGo"}, {MsgType::NpcOwnActors, "NpcOwnActors"}, {MsgType::NpcAppearances, "NpcAppearances"},
    {MsgType::SceneLineSkip, "SceneLineSkip"}, {MsgType::SceneCutSkip, "SceneCutSkip"}, {MsgType::WorldLoad, "WorldLoad"},
    {MsgType::SceneSkipHere, "SceneSkipHere"}, {MsgType::VehicleSceneSeat, "VehicleSceneSeat"}, {MsgType::PvpRules, "PvpRules"},
    {MsgType::PvpHit, "PvpHit"}, {MsgType::PvpResult, "PvpResult"}, {MsgType::StoryTrace, "StoryTrace"}, {MsgType::LedPause, "LedPause"},
    {MsgType::Destruction, "Destruction"}, {MsgType::DeviceBlast, "DeviceBlast"}, {MsgType::PlayerBlast, "PlayerBlast"},
    {MsgType::VehicleDamage, "VehicleDamage"}, {MsgType::PhoneSync, "PhoneSync"}, {MsgType::RideAlong, "RideAlong"},
    {MsgType::ChallengeRules, "ChallengeRules"}, {MsgType::NpcShot, "NpcShot"}, {MsgType::Braindance, "Braindance"}, {MsgType::PlayerFx, "PlayerFx"},
    {MsgType::DeviceSync, "DeviceSync"}, {MsgType::HackSync, "HackSync"}, {MsgType::BuildStats, "BuildStats"}, {MsgType::SmsReply, "SmsReply"},
    {MsgType::BraindanceGhost, "BraindanceGhost"}, {MsgType::BraindanceClue, "BraindanceClue"}, {MsgType::SceneZonePassed, "SceneZonePassed"},
    {MsgType::StoryHold, "StoryHold"}, {MsgType::CrowdEachHold, "CrowdEachHold"}, {MsgType::BraindanceSource, "BraindanceSource"},
    {MsgType::HackIcon, "HackIcon"}, {MsgType::NpcTag, "NpcTag"}, {MsgType::VehicleRemote, "VehicleRemote"}, {MsgType::AvSound, "AvSound"},
    {MsgType::CrowdWalkersCover, "CrowdWalkersCover"}, {MsgType::BlackwallNpc, "BlackwallNpc"}, {MsgType::PromoScene, "PromoScene"},
    {MsgType::QuestItemDeposit, "QuestItemDeposit"}, {MsgType::PoliceFight, "PoliceFight"}, {MsgType::VehicleAutopilotEnd, "VehicleAutopilotEnd"},
    {MsgType::CarsWord, "CarsWord"}, {MsgType::DocumentRead, "DocumentRead"}, {MsgType::SenseWord, "SenseWord"}, {MsgType::SceneAway, "SceneAway"},
    {MsgType::NpcAction, "NpcAction"}, {MsgType::NpcScenePose, "NpcScenePose"}, {MsgType::FrameWitness, "FrameWitness"},
    {MsgType::LedStoryWhy, "LedStoryWhy"}, {MsgType::PhotoTogether, "PhotoTogether"}, {MsgType::CyberwareLook, "CyberwareLook"},
    {MsgType::NpcCarHit, "NpcCarHit"}, {MsgType::TurretWord, "TurretWord"}, {MsgType::VehicleRole, "VehicleRole"}
};

uint8_t ByteAt(const std::vector<uint8_t>& message, size_t offset)
{
    return offset < message.size() ? message[offset] : 0;
}

uint16_t ShortAt(const std::vector<uint8_t>& message, size_t offset)
{
    uint16_t value = 0;
    if (offset + sizeof(value) <= message.size())
    {
        std::memcpy(&value, message.data() + offset, sizeof(value));
    }
    return value;
}

DropCheck Fits(bool sizeIsRight)
{
    return sizeIsRight ? DropCheck::LooksRight : DropCheck::Broken;
}

DropCheck CountedItems(const std::vector<uint8_t>& message, size_t countOffset, size_t fixedBytes, size_t itemBytes, uint8_t fewest, uint8_t most)
{
    const uint8_t count = ByteAt(message, countOffset);
    return Fits(message.size() > countOffset && count >= fewest && count <= most && message.size() == fixedBytes + itemBytes * count);
}
}

DropCheck CheckDroppedMessage(const std::vector<uint8_t>& message)
{
    const auto size = message.size();
    if (size < 5)
    {
        return DropCheck::Broken;
    }
    const uint8_t type = MessageTypeOf(message);
    switch (static_cast<MsgType>(type))
    {
    case MsgType::PlayerCarry: return Fits(size == 69 || size == 77);
    case MsgType::PhoneSync:
    {
        const uint16_t nameLength = ShortAt(message, 0x1F);
        return Fits(size >= 0x21 && nameLength >= 1 && nameLength <= 96 && size == 0x25u + nameLength);
    }
    case MsgType::PlayerFx: return CountedItems(message, 5, 6, 41, 1, 24);
    case MsgType::BuildStats: return CountedItems(message, 10, 11, 5, 0, 32);
    case MsgType::SmsReply: return Fits(size == 20);
    case MsgType::ChallengeRules: return Fits(size == 13);
    case MsgType::Braindance: return Fits(size == 54);
    case MsgType::BraindanceGhost: return Fits(size == 55);
    case MsgType::BraindanceClue: return Fits(size == 31);
    case MsgType::BraindanceSource: return Fits(size == 35);
    case MsgType::PhotoTogether: return Fits(size == 6 && ByteAt(message, 5) < 2);
    case MsgType::PlayerBlast: return CountedItems(message, 5, 6, 24, 1, 8);
    case MsgType::PlayerGrenade:
    case MsgType::NpcGrenade: return Fits(size == 66);
    case MsgType::NpcHandoff: return CountedItems(message, 5 + 62 + 37, 5 + 62 + 68, 2, 0, 7);
    case MsgType::NpcHandoffAck: return Fits(size == 19);
    case MsgType::NpcClaim: return Fits(size == 14);
    case MsgType::NpcTakeRequest: return Fits(size == 10);
    default: break;
    }
    if (type == kPhotoPoseType)
    {
        return Fits(size == 71);
    }
    return DropCheck::NoSizeRule;
}

std::string DroppedMessageName(uint8_t type)
{
    const auto known = KnownNames.find(static_cast<MsgType>(type));
    if (known != KnownNames.end())
    {
        return std::string(known->second) + " " + std::to_string(type);
    }
    if (type == kPhotoPoseType)
    {
        return "PhotoPose 226";
    }
    if (type == kPerkDilationType)
    {
        return "PerkDilation 251";
    }
    return "type " + std::to_string(type);
}
}
