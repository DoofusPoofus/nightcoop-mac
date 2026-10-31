#include "plugin/Loot.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/ItemGive.hpp"
#include "plugin/PartnerRoster.hpp"
#include "plugin/WorldHandoff.hpp"
#include "plugin/WorldLoot.hpp"

#include <ProtocolCore.hpp>

namespace nightcoop_mac::loot
{
namespace
{
constexpr uint64_t StatusEveryMs = 2000;
constexpr uint64_t StatusLogEveryMs = 20000;
constexpr uint64_t TestGiveAfterMs = 10000;
constexpr uint64_t TestDropAfterMs = 20000;

bool ForcedSharing = false;
uint64_t NextStatusMs = 0;
uint64_t NextStatusLogMs = 0;
std::string LastLoggedStatus;
uint64_t TestStartedMs = 0;
bool TestGiveDone = false;
bool TestDropDone = false;

bool WeAreHost(net::RelaySession& session)
{
    return session.GetStatus().MyPeerNumber == 0;
}

bool SharingWith(uint32_t peer, net::RelaySession& session)
{
    if (ForcedSharing)
    {
        return true;
    }
    return WeAreHost(session) ? partner_roster::PeerInHostWorld(peer) : world_handoff::InHostWorld();
}

bool SharingAtAll(net::RelaySession& session)
{
    if (ForcedSharing)
    {
        return true;
    }
    if (!WeAreHost(session))
    {
        return world_handoff::InHostWorld();
    }
    for (const auto& [peer, nickname] : session.PeerNicknames())
    {
        if (partner_roster::PeerInHostWorld(peer))
        {
            return true;
        }
    }
    return false;
}

bool FirstPartner(net::RelaySession& session, uint32_t& partner)
{
    if (!WeAreHost(session))
    {
        partner = 0;
        return true;
    }
    const auto nicknames = session.PeerNicknames();
    if (nicknames.empty())
    {
        return false;
    }
    partner = nicknames.begin()->first;
    return true;
}

void RunTest(uint64_t nowMs, bool inSave, const std::string& record, int32_t quantity, net::RelaySession& session, const LogFunction& log)
{
    uint32_t partner = 0;
    if (!inSave || !FirstPartner(session, partner))
    {
        TestStartedMs = 0;
        return;
    }
    if (!TestStartedMs)
    {
        TestStartedMs = nowMs;
        log("test loot: partner " + std::to_string(partner) + " is here, giving " + std::to_string(quantity) + " x '" + record + "' in 10 s");
    }
    if (!TestGiveDone && nowMs - TestStartedMs >= TestGiveAfterMs)
    {
        TestGiveDone = true;
        const int32_t have = item_give::TopUpForTest(record, quantity + 1, log);
        log("test loot: we have " + std::to_string(have) + " x '" + record + "' now");
        if (!item_give::AskThruScript(record, quantity, partner, log))
        {
            log("test loot: no NightCoopItemGive script service, giving without it: " + item_give::GiveByRecord(record, quantity, partner, session, log));
        }
    }
    if (!TestDropDone && nowMs - TestStartedMs >= TestDropAfterMs)
    {
        TestDropDone = true;
        log(world_loot::TestDrop(record, log) ? "test loot: dropped one '" + record + "' at our feet" : "test loot: the test drop did not work");
    }
}

void PublishStatus(uint64_t nowMs, const LogFunction& log)
{
    if (nowMs < NextStatusMs)
    {
        return;
    }
    NextStatusMs = nowMs + StatusEveryMs;
    const std::string lootStatus = world_loot::Status();
    item_give::PublishStatus(lootStatus);
    const std::string both = item_give::Status() + " | " + lootStatus;
    if (both != LastLoggedStatus && nowMs >= NextStatusLogMs)
    {
        NextStatusLogMs = nowMs + StatusLogEveryMs;
        LastLoggedStatus = both;
        log(item_give::Status());
        log(lootStatus);
    }
}
}

void Tick(uint64_t nowMs, bool inSave, const net::PlayerState* ourPlayer, bool testLoot, const std::string& testRecord, int32_t testQuantity,
          net::RelaySession& session, RemoteBodies& bodies, const LogFunction& log)
{
    ForcedSharing = testLoot;
    if (!session.GetStatus().InSession)
    {
        item_give::Tick(nowMs, false, session, log);
        world_loot::Tick(nowMs, false, false, ourPlayer, session, bodies, log);
        return;
    }
    item_give::Tick(nowMs, inSave, session, log);
    world_loot::Tick(nowMs, inSave, SharingAtAll(session), ourPlayer, session, bodies, log);
    if (testLoot)
    {
        RunTest(nowMs, inSave, testRecord, testQuantity, session, log);
    }
    if (inSave)
    {
        PublishStatus(nowMs, log);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    const uint8_t type = net::MessageTypeOf(message);
    if (type >= static_cast<uint8_t>(nc::proto::MsgType::ItemTransferOffer) && type <= static_cast<uint8_t>(nc::proto::MsgType::ItemTransferAck))
    {
        item_give::OnMessage(fromPeer, message, session, log);
    }
    else if (type >= static_cast<uint8_t>(nc::proto::MsgType::LootState) && type <= static_cast<uint8_t>(nc::proto::MsgType::LootTakeResult))
    {
        world_loot::OnMessage(fromPeer, message, SharingWith(fromPeer, session), session, log);
    }
}
}
