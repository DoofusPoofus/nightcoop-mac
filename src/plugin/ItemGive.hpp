/*
 item give nightcoops lootsync (ItemTransferOffer 38 / Debit 39 / Grant 40 / Ack 41): a player gives an item to a partner and
 the host keeps a ledger so an item never gets duped worst case it gets lost. the script half is NightCoopItemGive in
 scripts/NightCoopMac/NightCoopLoot.reds its NightCoopGiveItem(record quantity peer) queues a give and the plugin takes the
 queue evry 100 ms (GiveByRecord): the record has to make an item (ItemID.FromTDBID unique items do not go this way) and we
 need that many then the offer goes to the host with a transfer id of our peer << 32 | a counter
 host: an offer from its real sender for a known recipient goes in the ledger and the sender gets a debit. the sender takes
 the items off its player and acks phase 1 then the recipient gets a grant gives the items to its player and acks phase 2.
 debits and grants go again evry 2 s until acked a transfer that does not move for 300 s is given up (lost when it was
 already debited) and done ones are forgotten after 600 s. the host plays its own sender / recipient parts locally
 a guest takes debits and grants only from the host and remembers wat it already took or got: a resend gets the same ack
 again without touching the inventory. without a player (menu loading) it waits for the resend
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::item_give
{
using LogFunction = std::function<void(const std::string&)>;

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
std::string GiveByRecord(const std::string& recordName, int32_t quantity, uint32_t toPeer, net::RelaySession& session, const LogFunction& log);
bool AskThruScript(const std::string& recordName, int32_t quantity, uint32_t toPeer, const LogFunction& log);
int32_t TopUpForTest(const std::string& recordName, int32_t atLeast, const LogFunction& log);
void PublishStatus(const std::string& lootStatus);
std::string Status();
}
