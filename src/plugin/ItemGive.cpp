#include "plugin/ItemGive.hpp"

#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/LootMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Items.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <map>
#include <random>
#include <tuple>

namespace nightcoop_mac::item_give
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t ResendEveryMs = 2000;
constexpr uint64_t GiveUpAfterMs = 300000;
constexpr uint64_t ForgetAfterMs = 600000;

enum class Outcome
{
    Done,
    Failed,
    NotReady,
};

enum class Stage : uint8_t
{
    WaitingForDebit,
    Debited,
    Complete,
    Failed,
};

struct OurOffer
{
    net::ItemTransfer Transfer;
    std::string RecordName;
    uint64_t OfferedMs = 0;
};

struct LedgerEntry
{
    net::ItemTransfer Transfer;
    uint32_t Sender = 0;
    Stage Progress = Stage::WaitingForDebit;
    uint64_t MovedMs = 0;
    uint64_t SentMs = 0;
};

struct Counters
{
    uint32_t Offers = 0;
    uint32_t Debits = 0;
    uint32_t Grants = 0;
    uint32_t Complete = 0;
    uint32_t Rejected = 0;
    uint32_t SafelyLost = 0;
    uint32_t Resent = 0;
    uint32_t WaitedForWorld = 0;
};

struct ScriptCalls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Ask = nullptr;
    RED4ext::CBaseFunction* ClearAsks = nullptr;
    RED4ext::CBaseFunction* SetStatus = nullptr;
    RED4ext::CProperty* AskRecords = nullptr;
    RED4ext::CProperty* AskQuantities = nullptr;
    RED4ext::CProperty* AskPeers = nullptr;
};

ScriptCalls Script;
Counters Count;
uint64_t NextTickMs = 0;
uint64_t CurrentMs = 0;
bool InSaveNow = false;
uint32_t TransferCounter = 0;
std::map<uint64_t, OurOffer> OurOffers;
std::map<uint64_t, uint64_t> AlreadyDebited;
std::map<uint64_t, uint64_t> AlreadyReceived;
std::map<uint64_t, LedgerEntry> Ledger;

std::string Hex(uint64_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%#llx", static_cast<unsigned long long>(value));
    return text;
}

items::ItemId ToGameItem(const net::LootItemId& item)
{
    items::ItemId gameItem;
    gameItem.Record = net::RecordWithoutOffset(item.Record);
    gameItem.RngSeed = item.RngSeed;
    gameItem.UniqueCounter = item.UniqueCounter;
    gameItem.Structure = item.Structure;
    gameItem.Flags = item.Flags;
    return gameItem;
}

net::LootItemId FromGameItem(const items::ItemId& gameItem)
{
    return {gameItem.Record, gameItem.RngSeed, gameItem.UniqueCounter, gameItem.Structure, gameItem.Flags};
}

RED4ext::Handle<RED4ext::IScriptable> OurPlayer()
{
    static auto* getPlayer = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    if (getPlayer)
    {
        game::CallStatic(getPlayer, &player, args);
    }
    return player;
}

bool InventoryReady(const LogFunction& log)
{
    return items::Ready(log) && items::CanCountAndRemove();
}

bool ResolveScript()
{
    if (Script.Resolved)
    {
        return Script.Usable;
    }
    Script.Resolved = true;
    auto* giveClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopItemGive");
    Script.Ask = game::FindFunction("NightCoopItemGive", "Ask");
    Script.ClearAsks = game::FindFunction("NightCoopItemGive", "ClearAsks");
    Script.SetStatus = game::FindFunction("NightCoopItemGive", "SetStatus");
    Script.AskRecords = giveClass ? giveClass->GetProperty("askRecords") : nullptr;
    Script.AskQuantities = giveClass ? giveClass->GetProperty("askQuantities") : nullptr;
    Script.AskPeers = giveClass ? giveClass->GetProperty("askPeers") : nullptr;
    auto typeIs = [](RED4ext::CProperty* property, const char* typeName) {
        return property && property->type && std::string(property->type->GetName().ToString()) == typeName;
    };
    Script.Usable = game::ParamsAre(Script.Ask, {"String", "Int32", "Int32"}) && game::ParamsAre(Script.ClearAsks, {}) &&
                    game::ParamsAre(Script.SetStatus, {"String", "String"}) && typeIs(Script.AskRecords, "array:String") &&
                    typeIs(Script.AskQuantities, "array:Int32") && typeIs(Script.AskPeers, "array:Int32");
    return Script.Usable;
}

bool WeAreHost(net::RelaySession& session)
{
    return session.GetStatus().MyPeerNumber == 0;
}

bool KnownPeer(uint32_t peer, net::RelaySession& session)
{
    const auto status = session.GetStatus();
    if (peer == status.MyPeerNumber || peer == 0)
    {
        return true;
    }
    if (status.MyPeerNumber != 0)
    {
        return true;
    }
    return session.PeerNicknames().count(peer) != 0;
}

std::string Describe(const net::ItemTransfer& transfer)
{
    return std::to_string(transfer.Quantity) + " x " + Hex(transfer.Item.Record);
}

Outcome RemoveLocal(const net::ItemTransfer& transfer, std::string& reason, const LogFunction& log)
{
    if (!InSaveNow || !InventoryReady(log))
    {
        return Outcome::NotReady;
    }
    auto player = OurPlayer();
    auto transactions = items::TransactionSystem();
    if (!player || !transactions)
    {
        return Outcome::NotReady;
    }
    const auto item = ToGameItem(transfer.Item);
    const int32_t before = items::Quantity(transactions, player, item);
    const auto wanted = static_cast<int32_t>(transfer.Quantity);
    if (before < wanted)
    {
        reason = "we have " + std::to_string(before) + " of it";
        return Outcome::Failed;
    }
    const bool removed = items::Remove(transactions, player, item, wanted);
    const int32_t after = items::Quantity(transactions, player, item);
    if (removed && after == before - wanted)
    {
        return Outcome::Done;
    }
    reason = std::string("RemoveItem said ") + (removed ? "yes" : "no") + ", " + std::to_string(before) + " -> " + std::to_string(after);
    return before - after >= wanted ? Outcome::Done : Outcome::Failed;
}

Outcome GiveLocal(const net::ItemTransfer& transfer, std::string& reason, const LogFunction& log)
{
    if (!InSaveNow || !InventoryReady(log))
    {
        return Outcome::NotReady;
    }
    auto player = OurPlayer();
    auto transactions = items::TransactionSystem();
    if (!player || !transactions)
    {
        return Outcome::NotReady;
    }
    if (!items::Give(transactions, player, ToGameItem(transfer.Item), static_cast<int32_t>(transfer.Quantity)))
    {
        reason = "GiveItem refused";
        return Outcome::Failed;
    }
    return Outcome::Done;
}

void HandleAck(uint32_t fromPeer, const net::ItemTransferAck& ack, net::RelaySession& session, const LogFunction& log);
void HandleDebit(uint32_t fromPeer, const net::ItemTransfer& transfer, net::RelaySession& session, const LogFunction& log);
void HandleGrant(uint32_t fromPeer, const net::ItemTransfer& transfer, net::RelaySession& session, const LogFunction& log);

void SendAck(uint64_t transferId, uint8_t phase, bool ok, net::RelaySession& session, const LogFunction& log)
{
    const net::ItemTransferAck ack {transferId, phase, ok};
    if (WeAreHost(session))
    {
        HandleAck(0, ack, session, log);
        return;
    }
    session.SendGameMessage(net::WriteItemTransferAck(ack), 0);
}

void SendDebit(LedgerEntry& entry, net::RelaySession& session, const LogFunction& log)
{
    entry.SentMs = CurrentMs;
    if (entry.Sender == session.GetStatus().MyPeerNumber)
    {
        HandleDebit(0, entry.Transfer, session, log);
        return;
    }
    session.SendGameMessage(net::WriteItemTransfer(net::kItemTransferDebit, entry.Transfer), static_cast<int32_t>(entry.Sender));
}

void SendGrant(LedgerEntry& entry, net::RelaySession& session, const LogFunction& log)
{
    entry.SentMs = CurrentMs;
    if (entry.Transfer.Recipient == session.GetStatus().MyPeerNumber)
    {
        HandleGrant(0, entry.Transfer, session, log);
        return;
    }
    session.SendGameMessage(net::WriteItemTransfer(net::kItemTransferGrant, entry.Transfer), static_cast<int32_t>(entry.Transfer.Recipient));
}

void HandleOffer(uint32_t fromPeer, const net::ItemTransfer& transfer, net::RelaySession& session, const LogFunction& log)
{
    if (!WeAreHost(session) || transfer.Recipient == fromPeer || (transfer.TransferId >> 32) != fromPeer || !KnownPeer(transfer.Recipient, session))
    {
        ++Count.Rejected;
        log("inventory: host: transfer " + Hex(transfer.TransferId) + " from peer " + std::to_string(fromPeer) + " to peer " +
            std::to_string(transfer.Recipient) + " refused (unknown recipient or foreign id)");
        return;
    }
    const auto [where, isNew] = Ledger.try_emplace(transfer.TransferId);
    if (!isNew)
    {
        ++Count.Rejected;
        return;
    }
    auto& entry = where->second;
    entry.Transfer = transfer;
    entry.Sender = fromPeer;
    entry.Progress = Stage::WaitingForDebit;
    entry.MovedMs = CurrentMs;
    ++Count.Offers;
    log("inventory: host: transfer " + Hex(transfer.TransferId) + ": " + Describe(transfer) + " from peer " + std::to_string(fromPeer) +
        " to peer " + std::to_string(transfer.Recipient));
    SendDebit(entry, session, log);
}

void HandleDebit(uint32_t fromPeer, const net::ItemTransfer& transfer, net::RelaySession& session, const LogFunction& log)
{
    if ((transfer.TransferId >> 32) != session.GetStatus().MyPeerNumber)
    {
        ++Count.Rejected;
        return;
    }
    if (AlreadyDebited.count(transfer.TransferId))
    {
        SendAck(transfer.TransferId, net::kTransferPhaseDebit, true, session, log);
        return;
    }
    const auto offer = OurOffers.find(transfer.TransferId);
    if (offer == OurOffers.end() || !(offer->second.Transfer == transfer))
    {
        ++Count.Rejected;
        log("inventory: transfer " + Hex(transfer.TransferId) + ": a debit for " + Describe(transfer) + " we never offered, nothing taken");
        SendAck(transfer.TransferId, net::kTransferPhaseDebit, false, session, log);
        return;
    }
    std::string reason;
    const Outcome outcome = RemoveLocal(transfer, reason, log);
    if (outcome == Outcome::NotReady)
    {
        ++Count.WaitedForWorld;
        return;
    }
    const std::string recordName = offer->second.RecordName;
    OurOffers.erase(offer);
    if (outcome == Outcome::Failed)
    {
        ++Count.Rejected;
        log("inventory: transfer " + Hex(transfer.TransferId) + ": could not take " + std::to_string(transfer.Quantity) + " x '" + recordName +
            "' from our inventory (" + reason + "); nothing is sent");
        SendAck(transfer.TransferId, net::kTransferPhaseDebit, false, session, log);
        return;
    }
    ++Count.Debits;
    AlreadyDebited[transfer.TransferId] = CurrentMs;
    log("inventory: transfer " + Hex(transfer.TransferId) + ": took " + std::to_string(transfer.Quantity) + " x '" + recordName +
        "' from our inventory for peer " + std::to_string(transfer.Recipient) + (reason.empty() ? "" : " (" + reason + ")"));
    SendAck(transfer.TransferId, net::kTransferPhaseDebit, true, session, log);
}

void HandleGrant(uint32_t fromPeer, const net::ItemTransfer& transfer, net::RelaySession& session, const LogFunction& log)
{
    if (transfer.Recipient != session.GetStatus().MyPeerNumber)
    {
        ++Count.Rejected;
        return;
    }
    if (AlreadyReceived.count(transfer.TransferId))
    {
        SendAck(transfer.TransferId, net::kTransferPhaseGrant, true, session, log);
        return;
    }
    std::string reason;
    const Outcome outcome = GiveLocal(transfer, reason, log);
    if (outcome == Outcome::NotReady)
    {
        ++Count.WaitedForWorld;
        return;
    }
    const auto sender = static_cast<uint32_t>(transfer.TransferId >> 32);
    if (outcome == Outcome::Failed)
    {
        ++Count.Rejected;
        log("inventory: transfer " + Hex(transfer.TransferId) + ": could not receive " + Describe(transfer) + " from peer " + std::to_string(sender) +
            " (" + reason + "); the sender has already given it away");
        SendAck(transfer.TransferId, net::kTransferPhaseGrant, false, session, log);
        return;
    }
    ++Count.Grants;
    AlreadyReceived[transfer.TransferId] = CurrentMs;
    log("inventory: transfer " + Hex(transfer.TransferId) + ": received " + Describe(transfer) + " from peer " + std::to_string(sender));
    SendAck(transfer.TransferId, net::kTransferPhaseGrant, true, session, log);
}

void HandleAck(uint32_t fromPeer, const net::ItemTransferAck& ack, net::RelaySession& session, const LogFunction& log)
{
    const auto found = Ledger.find(ack.TransferId);
    if (!WeAreHost(session) || found == Ledger.end())
    {
        ++Count.Rejected;
        return;
    }
    auto& entry = found->second;
    if (ack.Phase == net::kTransferPhaseDebit && fromPeer == entry.Sender && entry.Progress == Stage::WaitingForDebit)
    {
        entry.MovedMs = CurrentMs;
        if (!ack.Ok)
        {
            entry.Progress = Stage::Failed;
            ++Count.Rejected;
            log("inventory: host: transfer " + Hex(ack.TransferId) + " refused by its sender, nothing was taken");
            return;
        }
        entry.Progress = Stage::Debited;
        SendGrant(entry, session, log);
    }
    else if (ack.Phase == net::kTransferPhaseGrant && fromPeer == entry.Transfer.Recipient && entry.Progress == Stage::Debited)
    {
        entry.MovedMs = CurrentMs;
        if (ack.Ok)
        {
            entry.Progress = Stage::Complete;
            ++Count.Complete;
            log("inventory: host: transfer " + Hex(ack.TransferId) + " complete");
            return;
        }
        entry.Progress = Stage::Failed;
        ++Count.SafelyLost;
        log("inventory: host: transfer " + Hex(ack.TransferId) + ": the recipient could not take " + Describe(entry.Transfer) +
            ", the item is lost, not duplicated");
    }
}

void DropLedger(const char* why, const LogFunction& log)
{
    uint32_t lostAfterDebit = 0;
    uint32_t unfinished = 0;
    for (const auto& [transferId, entry] : Ledger)
    {
        if (entry.Progress == Stage::Debited)
        {
            ++lostAfterDebit;
            log("inventory: host: transfer " + Hex(transferId) + " abandoned after the debit (" + why + "): " + Describe(entry.Transfer) + " for peer " +
                std::to_string(entry.Transfer.Recipient) + " is lost");
        }
        unfinished += entry.Progress == Stage::Debited || entry.Progress == Stage::WaitingForDebit ? 1 : 0;
    }
    Count.SafelyLost += lostAfterDebit;
    if (unfinished)
    {
        log("inventory: host: " + std::to_string(unfinished) + " unfinished transfer(s) dropped (" + why + "), " + std::to_string(lostAfterDebit) +
            " of them lost after the debit");
    }
    Ledger.clear();
}

void RunLedger(net::RelaySession& session, const LogFunction& log)
{
    for (auto entry = Ledger.begin(); entry != Ledger.end();)
    {
        auto& transfer = entry->second;
        const bool open = transfer.Progress == Stage::WaitingForDebit || transfer.Progress == Stage::Debited;
        if (!open)
        {
            entry = CurrentMs - transfer.MovedMs >= ForgetAfterMs ? Ledger.erase(entry) : std::next(entry);
            continue;
        }
        if (CurrentMs - transfer.MovedMs >= GiveUpAfterMs)
        {
            if (transfer.Progress == Stage::Debited)
            {
                ++Count.SafelyLost;
                log("inventory: host: transfer " + Hex(entry->first) + " abandoned after the debit (no answer for 300 s): " + Describe(transfer.Transfer) +
                    " for peer " + std::to_string(transfer.Transfer.Recipient) + " is lost");
            }
            else
            {
                log("inventory: host: transfer " + Hex(entry->first) + " from peer " + std::to_string(transfer.Sender) +
                    " dropped after 300 s without an answer, nothing taken");
            }
            transfer.Progress = Stage::Failed;
            transfer.MovedMs = CurrentMs;
        }
        else if (CurrentMs - transfer.SentMs >= ResendEveryMs)
        {
            ++Count.Resent;
            if (transfer.Progress == Stage::WaitingForDebit)
            {
                SendDebit(transfer, session, log);
            }
            else
            {
                SendGrant(transfer, session, log);
            }
        }
        ++entry;
    }
}

void TakeScriptAsks(net::RelaySession& session, const LogFunction& log)
{
    if (!ResolveScript())
    {
        return;
    }
    auto service = game::ScriptService("NightCoopItemGive");
    if (!service)
    {
        return;
    }
    const auto* records = Script.AskRecords->GetValuePtr<RED4ext::DynArray<RED4ext::CString>>(service.instance);
    const auto* quantities = Script.AskQuantities->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    const auto* peers = Script.AskPeers->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    if (records->Size() == 0)
    {
        return;
    }
    std::vector<std::tuple<std::string, int32_t, int32_t>> asks;
    for (uint32_t index = 0; index < records->Size() && index < quantities->Size() && index < peers->Size(); ++index)
    {
        asks.emplace_back((*records)[index].c_str(), (*quantities)[index], (*peers)[index]);
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, Script.ClearAsks, nullptr, noArgs);
    for (const auto& [recordName, quantity, peer] : asks)
    {
        log(GiveByRecord(recordName, quantity, static_cast<uint32_t>(peer), session, log));
    }
}

void Prune(std::map<uint64_t, uint64_t>& remembered)
{
    std::erase_if(remembered, [](const auto& entry) { return CurrentMs - entry.second >= 4 * ForgetAfterMs; });
}
}

void Tick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log)
{
    if (nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    CurrentMs = nowMs;
    InSaveNow = inSave;
    const auto status = session.GetStatus();
    if ((!status.InSession || status.MyPeerNumber != 0) && !Ledger.empty())
    {
        DropLedger("the host session is over", log);
    }
    if (!status.InSession)
    {
        return;
    }
    if (inSave)
    {
        TakeScriptAsks(session, log);
    }
    std::erase_if(OurOffers, [nowMs](const auto& offer) { return nowMs - offer.second.OfferedMs >= ForgetAfterMs; });
    Prune(AlreadyDebited);
    Prune(AlreadyReceived);
    if (status.MyPeerNumber == 0)
    {
        RunLedger(session, log);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    const uint8_t type = net::MessageTypeOf(message);
    const bool weAreHost = WeAreHost(session);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::ItemTransferAck))
    {
        net::ItemTransferAck ack;
        if (weAreHost && net::ReadItemTransferAck(message, ack))
        {
            HandleAck(fromPeer, ack, session, log);
        }
        return;
    }
    uint8_t transferType = 0;
    net::ItemTransfer transfer;
    if (!net::ReadItemTransfer(message, transferType, transfer))
    {
        return;
    }
    if (transferType == net::kItemTransferOffer && weAreHost)
    {
        HandleOffer(fromPeer, transfer, session, log);
    }
    else if (transferType == net::kItemTransferDebit && !weAreHost && fromPeer == 0)
    {
        HandleDebit(fromPeer, transfer, session, log);
    }
    else if (transferType == net::kItemTransferGrant && !weAreHost && fromPeer == 0)
    {
        HandleGrant(fromPeer, transfer, session, log);
    }
}

std::string GiveByRecord(const std::string& recordName, int32_t quantity, uint32_t toPeer, net::RelaySession& session, const LogFunction& log)
{
    const auto status = session.GetStatus();
    auto player = OurPlayer();
    if (!status.InSession || !InSaveNow || !player)
    {
        return "inventory: no active session or player";
    }
    if (quantity < 1 || quantity > static_cast<int32_t>(net::kMostTransferQuantity) || toPeer == status.MyPeerNumber || toPeer == 0xFFFFFFFF ||
        !KnownPeer(toPeer, session))
    {
        return "inventory: invalid recipient or quantity";
    }
    auto transactions = items::TransactionSystem();
    if (!InventoryReady(log) || !transactions)
    {
        return "inventory: the game's inventory functions are not available";
    }
    const uint64_t record = RED4ext::TweakDBID(recordName).value;
    const auto gameItem = items::ItemFromRecord(record);
    if (!items::RecordExists(record) || gameItem.Record == 0)
    {
        return "inventory: unknown item record";
    }
    const int32_t have = items::Quantity(transactions, player, gameItem);
    if (have < quantity)
    {
        return "inventory: we have " + std::to_string(std::max(have, 0)) + " of '" + recordName + "', asked to give " + std::to_string(quantity) +
               " (unique items do not go this way)";
    }
    if (TransferCounter == 0)
    {
        TransferCounter = static_cast<uint32_t>(std::random_device {}()) ^
                          static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    if (++TransferCounter == 0)
    {
        ++TransferCounter;
    }
    OurOffer offer;
    offer.Transfer.TransferId = (static_cast<uint64_t>(status.MyPeerNumber) << 32) | TransferCounter;
    offer.Transfer.Recipient = toPeer;
    offer.Transfer.Item = FromGameItem(gameItem);
    offer.Transfer.Quantity = static_cast<uint32_t>(quantity);
    offer.RecordName = recordName;
    offer.OfferedMs = CurrentMs;
    const auto transfer = offer.Transfer;
    OurOffers[transfer.TransferId] = offer;
    log("inventory: transfer " + Hex(transfer.TransferId) + ": offering " + std::to_string(quantity) + " x '" + recordName + "' to peer " +
        std::to_string(toPeer));
    if (status.MyPeerNumber == 0)
    {
        HandleOffer(0, transfer, session, log);
    }
    else
    {
        session.SendGameMessage(net::WriteItemTransfer(net::kItemTransferOffer, transfer), 0);
    }
    return "inventory: transfer " + Hex(transfer.TransferId) + " requested for peer " + std::to_string(toPeer);
}

bool AskThruScript(const std::string& recordName, int32_t quantity, uint32_t toPeer, const LogFunction& log)
{
    auto service = game::ScriptService("NightCoopItemGive");
    if (!ResolveScript() || !service)
    {
        return false;
    }
    RED4ext::CString record(recordName.c_str());
    auto peer = static_cast<int32_t>(toPeer);
    RED4ext::CString answer;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &record);
    args.emplace_back(nullptr, &quantity);
    args.emplace_back(nullptr, &peer);
    if (!game::CallMethod(service.instance, Script.Ask, &answer, args))
    {
        return false;
    }
    log(std::string(answer.c_str()));
    return true;
}

int32_t TopUpForTest(const std::string& recordName, int32_t atLeast, const LogFunction& log)
{
    auto player = OurPlayer();
    auto transactions = items::TransactionSystem();
    if (!InSaveNow || !InventoryReady(log) || !player || !transactions)
    {
        return 0;
    }
    const auto gameItem = items::ItemFromRecord(RED4ext::TweakDBID(recordName).value);
    const int32_t have = items::Quantity(transactions, player, gameItem);
    if (have < atLeast)
    {
        items::Give(transactions, player, gameItem, atLeast - have);
    }
    return items::Quantity(transactions, player, gameItem);
}

void PublishStatus(const std::string& lootStatus)
{
    auto service = game::ScriptService("NightCoopItemGive");
    if (!ResolveScript() || !service)
    {
        return;
    }
    RED4ext::CString inventory(Status().c_str());
    RED4ext::CString loot(lootStatus.c_str());
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &inventory);
    args.emplace_back(nullptr, &loot);
    game::CallMethod(service.instance, Script.SetStatus, nullptr, args);
}

std::string Status()
{
    size_t openEntries = 0;
    for (const auto& [transferId, entry] : Ledger)
    {
        openEntries += entry.Progress == Stage::WaitingForDebit || entry.Progress == Stage::Debited ? 1 : 0;
    }
    return "inventory: offers " + std::to_string(Count.Offers) + ", debits " + std::to_string(Count.Debits) + ", grants " + std::to_string(Count.Grants) +
           ", complete " + std::to_string(Count.Complete) + ", rejected " + std::to_string(Count.Rejected) + ", safely lost " +
           std::to_string(Count.SafelyLost) + ", resent " + std::to_string(Count.Resent) + ", waited for the world " + std::to_string(Count.WaitedForWorld) +
           "; host ledger " + std::to_string(Ledger.size()) + " (" + std::to_string(openEntries) + " open), our offers waiting " +
           std::to_string(OurOffers.size());
}
}
