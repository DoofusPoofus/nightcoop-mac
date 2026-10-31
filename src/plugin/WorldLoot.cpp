#include "plugin/WorldLoot.hpp"

#include "net/GameMessages.hpp"
#include "net/LookMessages.hpp"
#include "net/LootMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/SessionMessages.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Items.hpp"
#include "plugin/RemoteBodies.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>
#include <RED4ext/Scripting/Natives/entEntityID.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>

namespace nightcoop_mac::world_loot
{
namespace
{
constexpr uint64_t TickEveryMs = 100;
constexpr uint64_t OwnReadEveryMs = 500;
constexpr uint64_t MirrorReadEveryMs = 300;
constexpr uint64_t LookedAtReadEveryMs = 100;
constexpr uint64_t StateResendMs = 5000;
constexpr uint64_t FinishedKeptMs = 30000;
constexpr uint64_t TakeResendMs = 2000;
constexpr uint64_t TakeGiveUpMs = 60000;
constexpr uint64_t MirrorSpawnWaitMs = 10000;
constexpr uint32_t MostSpawnTries = 3;
constexpr float MirrorSpawnRange = 55.0f;
constexpr float GoneNotUnloadedRange = 30.0f;
constexpr float PickedUpRange = 4.0f;
constexpr uint64_t OwnerQuietMs = 20000;
constexpr uint64_t WatchAfterSpawnMs = 30000;

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Configure = nullptr;
    RED4ext::CBaseFunction* ClearNews = nullptr;
    RED4ext::CBaseFunction* Forget = nullptr;
    RED4ext::CBaseFunction* Watch = nullptr;
    RED4ext::CBaseFunction* Unwatch = nullptr;
    RED4ext::CBaseFunction* LookedAtRecently = nullptr;
    RED4ext::CBaseFunction* Read = nullptr;
    RED4ext::CBaseFunction* TakeFrom = nullptr;
    RED4ext::CBaseFunction* PutInto = nullptr;
    RED4ext::CBaseFunction* BeginMirror = nullptr;
    RED4ext::CBaseFunction* AddMirrorItem = nullptr;
    RED4ext::CBaseFunction* SpawnMirror = nullptr;
    RED4ext::CBaseFunction* RollBack = nullptr;
    RED4ext::CBaseFunction* TestDrop = nullptr;
    RED4ext::CProperty* NewOwnDrops = nullptr;
    RED4ext::CProperty* BoundTickets = nullptr;
    RED4ext::CProperty* BoundDrops = nullptr;
    RED4ext::CProperty* ReadItems = nullptr;
    RED4ext::CProperty* ReadQuantities = nullptr;
    RED4ext::CProperty* ReadQualities = nullptr;
    RED4ext::CProperty* ReadPosition = nullptr;
    RED4ext::CProperty* ReadOrientation = nullptr;
    RED4ext::CProperty* ReadBag = nullptr;
};

struct OwnDrop
{
    uint64_t Entity = 0;
    net::LootState State;
    uint64_t LastSentMs = 0;
    uint64_t NextReadMs = 0;
    uint64_t FinishedMs = 0;
    std::map<std::pair<uint32_t, uint32_t>, bool> Answers;
};

struct Mirror
{
    net::LootState Latest;
    int32_t Ticket = 0;
    uint64_t Entity = 0;
    bool SpawnAsked = false;
    uint64_t SpawnAskedMs = 0;
    uint32_t SpawnTries = 0;
    bool GaveUp = false;
    bool Unloaded = false;
    bool Holding = false;
    uint32_t HeldRevision = 0;
    bool WaitingForBodyLogged = false;
    bool FreshlyBound = false;
    bool Watching = false;
    uint64_t UnwatchAtMs = 0;
    std::vector<net::LootEntry> Shown;
    uint64_t NextReadMs = 0;
    uint64_t HeardMs = 0;
};

using MirrorKey = std::pair<uint32_t, uint64_t>;

struct PendingTake
{
    uint32_t Owner = 0;
    net::LootTake Take;
    uint64_t FirstSentMs = 0;
    uint64_t LastSentMs = 0;
};

struct Counters
{
    uint32_t OwnShared = 0;
    uint32_t StatesSent = 0;
    uint32_t TakesGranted = 0;
    uint32_t TakesDenied = 0;
    uint32_t MirrorsSpawned = 0;
    uint32_t TakesSent = 0;
    uint32_t RolledBack = 0;
    uint32_t TakesGivenUp = 0;
    uint32_t SkippedNotInWorld = 0;
    uint32_t SkippedNotDone = 0;
};

Calls Script;
Counters Count;
uint64_t NextTickMs = 0;
uint64_t CurrentMs = 0;
bool WasInSave = false;
bool InSaveNow = false;
uint32_t ContainerCounter = 0;
int32_t TicketCounter = 0;
uint32_t TakeCounter = 0;
std::map<uint64_t, OwnDrop> OwnDrops;
std::map<MirrorKey, Mirror> Mirrors;
std::map<int32_t, MirrorKey> MirrorByTicket;
std::map<uint32_t, PendingTake> PendingTakes;

std::string Hex(uint64_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%#010llx", static_cast<unsigned long long>(value));
    return text;
}

bool PropertyIs(RED4ext::CProperty* property, const char* typeName)
{
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName;
}

bool Resolve(const LogFunction& log)
{
    if (Script.Resolved)
    {
        return Script.Usable;
    }
    Script.Resolved = true;
    auto* lootClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopWorldLoot");
    auto property = [lootClass](const char* name) { return lootClass ? lootClass->GetProperty(name) : nullptr; };
    auto function = [](const char* name) { return game::FindFunction("NightCoopWorldLoot", name); };
    Script.Configure = function("Configure");
    Script.ClearNews = function("ClearNews");
    Script.Forget = function("Forget");
    Script.Watch = function("Watch");
    Script.Unwatch = function("Unwatch");
    Script.LookedAtRecently = function("LookedAtRecently");
    Script.Read = function("Read");
    Script.TakeFrom = function("TakeFrom");
    Script.PutInto = function("PutInto");
    Script.BeginMirror = function("BeginMirror");
    Script.AddMirrorItem = function("AddMirrorItem");
    Script.SpawnMirror = function("SpawnMirror");
    Script.RollBack = function("RollBack");
    Script.TestDrop = function("TestDrop");
    Script.NewOwnDrops = property("newOwnDrops");
    Script.BoundTickets = property("boundTickets");
    Script.BoundDrops = property("boundDrops");
    Script.ReadItems = property("readItems");
    Script.ReadQuantities = property("readQuantities");
    Script.ReadQualities = property("readQualities");
    Script.ReadPosition = property("readPosition");
    Script.ReadOrientation = property("readOrientation");
    Script.ReadBag = property("readBag");
    Script.Usable =
        game::ParamsAre(Script.Configure, {"Bool"}) && game::ParamsAre(Script.ClearNews, {}) && game::ParamsAre(Script.Forget, {}) &&
        game::ParamsAre(Script.Watch, {"Int32", "Vector4"}) &&
        game::ParamsAre(Script.Unwatch, {"Int32"}) && game::ParamsAre(Script.LookedAtRecently, {"entEntityID"}) &&
        game::ReturnIs(Script.LookedAtRecently, "Bool") && game::ParamsAre(Script.Read, {"entEntityID"}) && game::ReturnIs(Script.Read, "Bool") &&
        game::ParamsAre(Script.TakeFrom, {"entEntityID", "gameItemID", "Int32"}) && game::ReturnIs(Script.TakeFrom, "Bool") &&
        game::ParamsAre(Script.PutInto, {"entEntityID", "gameItemID", "Int32"}) && game::ParamsAre(Script.BeginMirror, {}) &&
        game::ParamsAre(Script.AddMirrorItem, {"gameItemID", "Int32"}) &&
        game::ParamsAre(Script.SpawnMirror, {"handle:gameObject", "Int32", "Bool", "Vector4", "Quaternion"}) &&
        game::ReturnIs(Script.SpawnMirror, "Bool") && game::ParamsAre(Script.RollBack, {"gameItemID", "Int32"}) &&
        game::ReturnIs(Script.RollBack, "Int32") && game::ParamsAre(Script.TestDrop, {"gameItemID"}) &&
        PropertyIs(Script.NewOwnDrops, "array:entEntityID") && PropertyIs(Script.BoundTickets, "array:Int32") &&
        PropertyIs(Script.BoundDrops, "array:entEntityID") && PropertyIs(Script.ReadItems, "array:gameItemID") &&
        PropertyIs(Script.ReadQuantities, "array:Int32") && PropertyIs(Script.ReadQualities, "array:Int32") &&
        PropertyIs(Script.ReadPosition, "Vector4") && PropertyIs(Script.ReadOrientation, "Quaternion") && PropertyIs(Script.ReadBag, "Bool");
    log(Script.Usable ? "world loot: our drops go to partners in our world and theirs show up here"
                      : "world loot: no NightCoopWorldLoot script service (or it looks different), loot stays our own");
    return Script.Usable;
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

std::string DescribeEntries(const std::vector<net::LootEntry>& entries)
{
    std::string text;
    for (size_t index = 0; index < entries.size() && index < 4; ++index)
    {
        char entry[48];
        std::snprintf(entry, sizeof(entry), "%s%u x %#llx", index ? ", " : "", entries[index].Quantity,
                      static_cast<unsigned long long>(entries[index].Item.Record));
        text += entry;
    }
    return entries.size() > 4 ? text + ", ..." : text.empty() ? "nothing" : text;
}

float DistanceTo(const net::PlayerState* ourPlayer, const float* position)
{
    if (!ourPlayer)
    {
        return 1.0e9f;
    }
    const float x = ourPlayer->PositionX - position[0];
    const float y = ourPlayer->PositionY - position[1];
    const float z = ourPlayer->PositionZ - position[2];
    return std::sqrt(x * x + y * y + z * z);
}

bool SameItem(const net::LootItemId& left, const net::LootItemId& right)
{
    return net::RecordWithoutOffset(left.Record) == net::RecordWithoutOffset(right.Record) && left.RngSeed == right.RngSeed &&
           left.UniqueCounter == right.UniqueCounter && left.Structure == right.Structure && left.Flags == right.Flags;
}

uint32_t QuantityOf(const std::vector<net::LootEntry>& entries, const net::LootItemId& item)
{
    uint32_t quantity = 0;
    for (const auto& entry : entries)
    {
        quantity += SameItem(entry.Item, item) ? entry.Quantity : 0;
    }
    return quantity;
}

bool CallWithDrop(RED4ext::IScriptable* service, RED4ext::CBaseFunction* function, uint64_t entity)
{
    RED4ext::ent::EntityID drop(entity);
    bool answer = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &drop);
    return game::CallMethod(service, function, &answer, args) && answer;
}

bool MoveItems(RED4ext::IScriptable* service, RED4ext::CBaseFunction* function, uint64_t entity, const net::LootItemId& item, uint32_t quantity)
{
    RED4ext::ent::EntityID drop(entity);
    auto gameItem = ToGameItem(item);
    auto amount = static_cast<int32_t>(quantity);
    bool moved = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &drop);
    args.emplace_back(nullptr, &gameItem);
    args.emplace_back(nullptr, &amount);
    return game::CallMethod(service, function, &moved, args) && moved;
}

void Watch(RED4ext::IScriptable* service, int32_t ticket, const float* position)
{
    RED4ext::Vector4 spot(position[0], position[1], position[2], 1.0f);
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &ticket);
    args.emplace_back(nullptr, &spot);
    game::CallMethod(service, Script.Watch, nullptr, args);
}

void Unwatch(RED4ext::IScriptable* service, int32_t ticket)
{
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &ticket);
    game::CallMethod(service, Script.Unwatch, nullptr, args);
}

bool ReadContainer(RED4ext::IScriptable* service, uint64_t entity, std::vector<net::LootEntry>& entries, float* position, float* orientation,
                   bool* bag)
{
    entries.clear();
    if (!CallWithDrop(service, Script.Read, entity))
    {
        return false;
    }
    const auto* readItems = Script.ReadItems->GetValuePtr<RED4ext::DynArray<items::ItemId>>(service);
    const auto* readQuantities = Script.ReadQuantities->GetValuePtr<RED4ext::DynArray<int32_t>>(service);
    const auto* readQualities = Script.ReadQualities->GetValuePtr<RED4ext::DynArray<int32_t>>(service);
    for (uint32_t index = 0; index < readItems->Size() && index < readQuantities->Size() && entries.size() < net::kMostLootEntries; ++index)
    {
        const int32_t quantity = (*readQuantities)[index];
        if (quantity < 1 || (*readItems)[index].Record == 0)
        {
            continue;
        }
        net::LootEntry entry;
        entry.Item = FromGameItem((*readItems)[index]);
        entry.Quantity = std::min<uint32_t>(static_cast<uint32_t>(quantity), net::kMostLootQuantity);
        const int32_t quality = index < readQualities->Size() ? (*readQualities)[index] : net::kLootQualityUnknown;
        entry.Quality = quality >= 0 && quality <= 10 ? static_cast<uint8_t>(quality) : net::kLootQualityUnknown;
        entries.push_back(entry);
    }
    if (position)
    {
        const auto* spot = Script.ReadPosition->GetValuePtr<RED4ext::Vector4>(service);
        position[0] = spot->X;
        position[1] = spot->Y;
        position[2] = spot->Z;
    }
    if (orientation)
    {
        const auto* turn = Script.ReadOrientation->GetValuePtr<RED4ext::Quaternion>(service);
        orientation[0] = turn->i;
        orientation[1] = turn->j;
        orientation[2] = turn->k;
        orientation[3] = turn->r;
    }
    if (bag)
    {
        *bag = *Script.ReadBag->GetValuePtr<bool>(service);
    }
    return true;
}

void SendState(OwnDrop& drop, bool sharing, net::RelaySession& session)
{
    drop.LastSentMs = CurrentMs;
    if (!sharing)
    {
        return;
    }
    ++Count.StatesSent;
    session.SendGameMessage(net::WriteLootState(drop.State));
}

void TakeNews(RED4ext::IScriptable* service, net::RelaySession& session, const LogFunction& log)
{
    const auto* newOwnDrops = Script.NewOwnDrops->GetValuePtr<RED4ext::DynArray<uint64_t>>(service);
    const auto* boundTickets = Script.BoundTickets->GetValuePtr<RED4ext::DynArray<int32_t>>(service);
    const auto* boundDrops = Script.BoundDrops->GetValuePtr<RED4ext::DynArray<uint64_t>>(service);
    if (newOwnDrops->Size() == 0 && boundTickets->Size() == 0)
    {
        return;
    }
    const std::vector<uint64_t> ownEntities(newOwnDrops->begin(), newOwnDrops->end());
    std::vector<std::pair<int32_t, uint64_t>> bound;
    for (uint32_t index = 0; index < boundTickets->Size() && index < boundDrops->Size(); ++index)
    {
        bound.emplace_back((*boundTickets)[index], (*boundDrops)[index]);
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service, Script.ClearNews, nullptr, noArgs);
    const uint32_t ourPeer = session.GetStatus().MyPeerNumber;
    for (const uint64_t entity : ownEntities)
    {
        if ((++ContainerCounter & 0xFFFFFF) == 0)
        {
            ++ContainerCounter;
        }
        OwnDrop drop;
        drop.Entity = entity;
        drop.State.Kind = net::kLootDrop;
        drop.State.ContainerId = (static_cast<uint64_t>(ourPeer) << 24) | (ContainerCounter & 0xFFFFFF);
        drop.State.Revision = 0;
        drop.NextReadMs = CurrentMs;
        ++Count.OwnShared;
        log("world loot: our drop " + Hex(drop.State.ContainerId) + " is shared");
        OwnDrops[drop.State.ContainerId] = std::move(drop);
    }
    for (const auto& [ticket, entity] : bound)
    {
        const auto byTicket = MirrorByTicket.find(ticket);
        const auto mirror = byTicket == MirrorByTicket.end() ? Mirrors.end() : Mirrors.find(byTicket->second);
        if (mirror == Mirrors.end())
        {
            std::vector<net::LootEntry> leftovers;
            ReadContainer(service, entity, leftovers, nullptr, nullptr, nullptr);
            for (const auto& entry : leftovers)
            {
                MoveItems(service, Script.TakeFrom, entity, entry.Item, entry.Quantity);
            }
            log("world loot: a mirror drop nobody waits for any more came into the world, emptied");
            continue;
        }
        auto& bound = mirror->second;
        if (bound.Entity == entity)
        {
            continue;
        }
        if (bound.Entity)
        {
            std::vector<net::LootEntry> extra;
            ReadContainer(service, entity, extra, nullptr, nullptr, nullptr);
            for (const auto& entry : extra)
            {
                MoveItems(service, Script.TakeFrom, entity, entry.Item, entry.Quantity);
            }
            log("world loot: a second copy of the mirror of drop " + Hex(mirror->first.second) + " came into the world, emptied");
            continue;
        }
        bound.Entity = entity;
        bound.Unloaded = false;
        bound.FreshlyBound = true;
        bound.NextReadMs = CurrentMs;
        bound.UnwatchAtMs = (bound.SpawnAsked ? bound.SpawnAskedMs : CurrentMs) + WatchAfterSpawnMs;
        bound.SpawnAsked = false;
        log("world loot: mirror of drop " + Hex(mirror->first.second) + " (peer " + std::to_string(mirror->first.first) + ") is in the world");
    }
}

void ServeOwnDrops(RED4ext::IScriptable* service, bool sharing, const net::PlayerState* ourPlayer, net::RelaySession& session, const LogFunction& log)
{
    for (auto entry = OwnDrops.begin(); entry != OwnDrops.end();)
    {
        auto& drop = entry->second;
        if (drop.FinishedMs)
        {
            entry = CurrentMs - drop.FinishedMs >= FinishedKeptMs ? OwnDrops.erase(entry) : std::next(entry);
            continue;
        }
        if (CurrentMs < drop.NextReadMs)
        {
            ++entry;
            continue;
        }
        const bool lookedAt = CallWithDrop(service, Script.LookedAtRecently, drop.Entity);
        drop.NextReadMs = CurrentMs + (lookedAt ? LookedAtReadEveryMs : OwnReadEveryMs);
        std::vector<net::LootEntry> entries;
        float position[3] = {drop.State.Position[0], drop.State.Position[1], drop.State.Position[2]};
        float orientation[4] = {drop.State.Orientation[0], drop.State.Orientation[1], drop.State.Orientation[2], drop.State.Orientation[3]};
        bool bag = drop.State.Bag;
        const bool there = ReadContainer(service, drop.Entity, entries, position, orientation, &bag);
        if (!there || entries.empty())
        {
            const bool near = DistanceTo(ourPlayer, drop.State.Position) <= GoneNotUnloadedRange || (there && entries.empty());
            drop.State.State = near ? net::kLootClosed : net::kLootLetGo;
            drop.State.Entries.clear();
            ++drop.State.Revision;
            drop.FinishedMs = CurrentMs;
            if (drop.State.Revision > 1)
            {
                SendState(drop, sharing, session);
            }
            log("world loot: our drop " + Hex(drop.State.ContainerId) + (near ? " is gone, closed" : " streamed out, let go"));
            ++entry;
            continue;
        }
        if (entries != drop.State.Entries || bag != drop.State.Bag || drop.State.Revision == 0)
        {
            drop.State.Entries = entries;
            drop.State.Bag = bag;
            std::copy(position, position + 3, drop.State.Position);
            std::copy(orientation, orientation + 4, drop.State.Orientation);
            ++drop.State.Revision;
            SendState(drop, sharing, session);
            log("world loot: our drop " + Hex(drop.State.ContainerId) + " holds " + DescribeEntries(entries) + " (revision " +
                std::to_string(drop.State.Revision) + ")");
        }
        else if (CurrentMs - drop.LastSentMs >= StateResendMs)
        {
            SendState(drop, sharing, session);
        }
        ++entry;
    }
}

std::vector<net::LootEntry> Desired(const MirrorKey& key, const Mirror& mirror)
{
    std::vector<net::LootEntry> desired = mirror.Latest.Entries;
    for (const auto& [takeNumber, pending] : PendingTakes)
    {
        if (pending.Owner != key.first || pending.Take.ContainerId != key.second)
        {
            continue;
        }
        uint32_t stillToTake = pending.Take.Item.Quantity;
        for (auto& entry : desired)
        {
            if (SameItem(entry.Item, pending.Take.Item.Item) && stillToTake)
            {
                const uint32_t taken = std::min(entry.Quantity, stillToTake);
                entry.Quantity -= taken;
                stillToTake -= taken;
            }
        }
    }
    std::erase_if(desired, [](const net::LootEntry& entry) { return entry.Quantity == 0; });
    return desired;
}

void SendTake(const MirrorKey& key, Mirror& mirror, const net::LootEntry& taken, net::RelaySession& session, const LogFunction& log)
{
    if (++TakeCounter == 0)
    {
        ++TakeCounter;
    }
    PendingTake pending;
    pending.Owner = key.first;
    pending.Take.Kind = mirror.Latest.Kind;
    pending.Take.ContainerId = key.second;
    pending.Take.TakeNumber = TakeCounter;
    pending.Take.Item = taken;
    pending.FirstSentMs = CurrentMs;
    pending.LastSentMs = CurrentMs;
    PendingTakes[TakeCounter] = pending;
    mirror.Holding = true;
    mirror.HeldRevision = mirror.Latest.Revision;
    ++Count.TakesSent;
    session.SendGameMessage(net::WriteLootTake(pending.Take), static_cast<int32_t>(key.first));
    log("world loot: we took " + std::to_string(taken.Quantity) + " x " + Hex(taken.Item.Record) + " from the mirror of drop " + Hex(key.second) +
        ", asking peer " + std::to_string(key.first) + " (take " + std::to_string(TakeCounter) + ")");
}

void EmptyMirror(RED4ext::IScriptable* service, Mirror& mirror)
{
    if (!mirror.Entity)
    {
        return;
    }
    std::vector<net::LootEntry> left;
    if (ReadContainer(service, mirror.Entity, left, nullptr, nullptr, nullptr))
    {
        for (const auto& entry : left)
        {
            MoveItems(service, Script.TakeFrom, mirror.Entity, entry.Item, entry.Quantity);
        }
    }
    mirror.Entity = 0;
}

void SpawnIfNear(RED4ext::IScriptable* service, const MirrorKey& key, Mirror& mirror, const net::PlayerState* ourPlayer, RemoteBodies& bodies,
                 const LogFunction& log)
{
    if (mirror.GaveUp || mirror.Unloaded || mirror.Holding || DistanceTo(ourPlayer, mirror.Latest.Position) > MirrorSpawnRange)
    {
        return;
    }
    if (mirror.SpawnAsked)
    {
        if (CurrentMs - mirror.SpawnAskedMs < MirrorSpawnWaitMs)
        {
            return;
        }
        if (mirror.SpawnTries >= MostSpawnTries)
        {
            mirror.GaveUp = true;
            mirror.Watching = false;
            Unwatch(service, mirror.Ticket);
            log("world loot: mirror of drop " + Hex(key.second) + " (peer " + std::to_string(key.first) + ") never came into the world, given up");
            return;
        }
    }
    const auto desired = Desired(key, mirror);
    if (desired.empty())
    {
        return;
    }
    RED4ext::Handle<RED4ext::IScriptable> body;
    if (!bodies.GetBody(key.first, body))
    {
        if (!mirror.WaitingForBodyLogged)
        {
            mirror.WaitingForBodyLogged = true;
            log("world loot: mirror of drop " + Hex(key.second) + " (peer " + std::to_string(key.first) + ") waits for their body");
        }
        return;
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service, Script.BeginMirror, nullptr, noArgs);
    for (const auto& entry : desired)
    {
        auto gameItem = ToGameItem(entry.Item);
        auto amount = static_cast<int32_t>(entry.Quantity);
        RED4ext::StackArgs_t itemArgs;
        itemArgs.emplace_back(nullptr, &gameItem);
        itemArgs.emplace_back(nullptr, &amount);
        game::CallMethod(service, Script.AddMirrorItem, nullptr, itemArgs);
    }
    if (!mirror.Ticket)
    {
        mirror.Ticket = ++TicketCounter;
        MirrorByTicket[mirror.Ticket] = key;
    }
    auto ticket = mirror.Ticket;
    bool bag = mirror.Latest.Bag;
    RED4ext::Vector4 spot(mirror.Latest.Position[0], mirror.Latest.Position[1], mirror.Latest.Position[2], 1.0f);
    RED4ext::Quaternion turn(mirror.Latest.Orientation[0], mirror.Latest.Orientation[1], mirror.Latest.Orientation[2], mirror.Latest.Orientation[3]);
    bool spawned = false;
    RED4ext::StackArgs_t spawnArgs;
    spawnArgs.emplace_back(nullptr, &body);
    spawnArgs.emplace_back(nullptr, &ticket);
    spawnArgs.emplace_back(nullptr, &bag);
    spawnArgs.emplace_back(nullptr, &spot);
    spawnArgs.emplace_back(nullptr, &turn);
    game::CallMethod(service, Script.SpawnMirror, &spawned, spawnArgs);
    mirror.SpawnAsked = true;
    mirror.SpawnAskedMs = CurrentMs;
    mirror.Watching = true;
    ++mirror.SpawnTries;
    mirror.Shown = desired;
    if (spawned)
    {
        ++Count.MirrorsSpawned;
    }
    log("world loot: mirror of drop " + Hex(key.second) + " (peer " + std::to_string(key.first) + "): " + DescribeEntries(desired) +
        " asked thru their body at (" + std::to_string(static_cast<int>(spot.X)) + ", " + std::to_string(static_cast<int>(spot.Y)) + ", " +
        std::to_string(static_cast<int>(spot.Z)) + "), the game answered " + (spawned ? "yes" : "no"));
}

void KeepMirror(RED4ext::IScriptable* service, const MirrorKey& key, Mirror& mirror, const net::PlayerState* ourPlayer, net::RelaySession& session,
                const LogFunction& log)
{
    if (CurrentMs < mirror.NextReadMs)
    {
        return;
    }
    const bool lookedAt = CallWithDrop(service, Script.LookedAtRecently, mirror.Entity);
    mirror.NextReadMs = CurrentMs + (lookedAt ? LookedAtReadEveryMs : MirrorReadEveryMs);
    std::vector<net::LootEntry> actual;
    if (!ReadContainer(service, mirror.Entity, actual, nullptr, nullptr, nullptr))
    {
        mirror.Entity = 0;
        if (lookedAt || DistanceTo(ourPlayer, mirror.Latest.Position) <= PickedUpRange)
        {
            for (const auto& shown : mirror.Shown)
            {
                SendTake(key, mirror, shown, session, log);
            }
            mirror.Shown.clear();
            log("world loot: mirror of drop " + Hex(key.second) + " (peer " + std::to_string(key.first) + ") got picked up whole");
            return;
        }
        mirror.Unloaded = true;
        mirror.Watching = true;
        mirror.UnwatchAtMs = 0;
        Watch(service, mirror.Ticket, mirror.Latest.Position);
        log("world loot: mirror of drop " + Hex(key.second) + " (peer " + std::to_string(key.first) + ") streamed out, watching for it");
        return;
    }
    for (const auto& shown : mirror.FreshlyBound ? std::vector<net::LootEntry>() : mirror.Shown)
    {
        const uint32_t there = QuantityOf(actual, shown.Item);
        if (there < shown.Quantity)
        {
            net::LootEntry taken = shown;
            taken.Quantity = shown.Quantity - there;
            SendTake(key, mirror, taken, session, log);
        }
    }
    mirror.FreshlyBound = false;
    if (mirror.Holding)
    {
        mirror.Shown = actual;
        return;
    }
    const auto desired = Desired(key, mirror);
    bool moved = false;
    for (const auto& entry : desired)
    {
        const uint32_t there = QuantityOf(actual, entry.Item);
        if (there < entry.Quantity)
        {
            MoveItems(service, Script.PutInto, mirror.Entity, entry.Item, entry.Quantity - there);
            moved = true;
        }
        else if (there > entry.Quantity)
        {
            MoveItems(service, Script.TakeFrom, mirror.Entity, entry.Item, there - entry.Quantity);
            moved = true;
        }
    }
    for (const auto& entry : actual)
    {
        if (QuantityOf(desired, entry.Item) == 0)
        {
            MoveItems(service, Script.TakeFrom, mirror.Entity, entry.Item, entry.Quantity);
            moved = true;
        }
    }
    if (moved)
    {
        ReadContainer(service, mirror.Entity, actual, nullptr, nullptr, nullptr);
    }
    mirror.Shown = actual;
}

void ServeMirrors(RED4ext::IScriptable* service, const net::PlayerState* ourPlayer, net::RelaySession& session, RemoteBodies& bodies,
                  const LogFunction& log)
{
    for (auto entry = Mirrors.begin(); entry != Mirrors.end();)
    {
        const MirrorKey key = entry->first;
        auto& mirror = entry->second;
        if (mirror.Latest.State == net::kLootOpen && CurrentMs - mirror.HeardMs >= OwnerQuietMs)
        {
            mirror.Latest.State = net::kLootLetGo;
        }
        if (mirror.Latest.State != net::kLootOpen)
        {
            EmptyMirror(service, mirror);
            if (mirror.Watching)
            {
                Unwatch(service, mirror.Ticket);
            }
            MirrorByTicket.erase(mirror.Ticket);
            log("world loot: mirror of drop " + Hex(key.second) + " (peer " + std::to_string(key.first) + ") let go: " +
                (mirror.Latest.State == net::kLootClosed ? "the owner says it is gone"
                 : CurrentMs - mirror.HeardMs >= OwnerQuietMs ? "the owner went quiet"
                                                               : "the owner no longer sees it"));
            entry = Mirrors.erase(entry);
            continue;
        }
        if (mirror.Watching && mirror.UnwatchAtMs && CurrentMs >= mirror.UnwatchAtMs)
        {
            mirror.Watching = false;
            mirror.UnwatchAtMs = 0;
            Unwatch(service, mirror.Ticket);
        }
        if (mirror.Entity)
        {
            KeepMirror(service, key, mirror, ourPlayer, session, log);
        }
        else
        {
            SpawnIfNear(service, key, mirror, ourPlayer, bodies, log);
        }
        ++entry;
    }
}

void ResendTakes(net::RelaySession& session, const LogFunction& log)
{
    for (auto entry = PendingTakes.begin(); entry != PendingTakes.end();)
    {
        auto& pending = entry->second;
        if (CurrentMs - pending.FirstSentMs >= TakeGiveUpMs)
        {
            ++Count.TakesGivenUp;
            log("world loot: take " + std::to_string(entry->first) + " from drop " + Hex(pending.Take.ContainerId) + " got no answer from peer " +
                std::to_string(pending.Owner) + " in 60 s, given up");
            const auto mirror = Mirrors.find({pending.Owner, pending.Take.ContainerId});
            if (mirror != Mirrors.end())
            {
                mirror->second.Holding = false;
            }
            entry = PendingTakes.erase(entry);
            continue;
        }
        if (CurrentMs - pending.LastSentMs >= TakeResendMs)
        {
            pending.LastSentMs = CurrentMs;
            session.SendGameMessage(net::WriteLootTake(pending.Take), static_cast<int32_t>(pending.Owner));
        }
        ++entry;
    }
}

void LeaveWorld(net::RelaySession& session, bool sharing, const LogFunction& log)
{
    for (auto& [containerId, drop] : OwnDrops)
    {
        if (!drop.FinishedMs)
        {
            drop.State.State = net::kLootLetGo;
            drop.State.Entries.clear();
            ++drop.State.Revision;
            SendState(drop, sharing, session);
        }
    }
    if (!OwnDrops.empty() || !Mirrors.empty() || !PendingTakes.empty())
    {
        log("world loot: our world went away, " + std::to_string(OwnDrops.size()) + " drop(s) let go, " + std::to_string(Mirrors.size()) +
            " mirror(s) and " + std::to_string(PendingTakes.size()) + " take(s) forgotten");
    }
    OwnDrops.clear();
    Mirrors.clear();
    MirrorByTicket.clear();
    PendingTakes.clear();
    auto service = game::ScriptService("NightCoopWorldLoot");
    if (Script.Usable && service)
    {
        RED4ext::StackArgs_t noArgs;
        game::CallMethod(service.instance, Script.Forget, nullptr, noArgs);
    }
}

void AnswerTake(uint32_t fromPeer, const net::LootTake& take, net::RelaySession& session, const LogFunction& log)
{
    auto service = game::ScriptService("NightCoopWorldLoot");
    const auto found = OwnDrops.find(take.ContainerId);
    bool granted = false;
    if (found != OwnDrops.end() && found->second.State.Kind == take.Kind)
    {
        auto& drop = found->second;
        const auto answered = drop.Answers.find({fromPeer, take.TakeNumber});
        if (answered != drop.Answers.end())
        {
            session.SendGameMessage(net::WriteLootTakeResult({take.Kind, take.ContainerId, take.TakeNumber, answered->second}), static_cast<int32_t>(fromPeer));
            return;
        }
        if (!drop.FinishedMs && service)
        {
            granted = MoveItems(service.instance, Script.TakeFrom, drop.Entity, take.Item.Item, take.Item.Quantity);
        }
        drop.Answers[{fromPeer, take.TakeNumber}] = granted;
        drop.NextReadMs = CurrentMs;
    }
    if (granted)
    {
        ++Count.TakesGranted;
    }
    else
    {
        ++Count.TakesDenied;
    }
    session.SendGameMessage(net::WriteLootTakeResult({take.Kind, take.ContainerId, take.TakeNumber, granted}), static_cast<int32_t>(fromPeer));
    log("world loot: peer " + std::to_string(fromPeer) + " takes " + std::to_string(take.Item.Quantity) + " x " + Hex(take.Item.Item.Record) +
        " from our drop " + Hex(take.ContainerId) + ": " + (granted ? "granted" : "denied, it is not here any more"));
}

void TakeResult(uint32_t fromPeer, const net::LootTakeResult& result, const LogFunction& log)
{
    const auto found = PendingTakes.find(result.TakeNumber);
    if (found == PendingTakes.end() || found->second.Owner != fromPeer || found->second.Take.ContainerId != result.ContainerId)
    {
        return;
    }
    const auto pending = found->second;
    PendingTakes.erase(found);
    const auto mirror = Mirrors.find({fromPeer, result.ContainerId});
    if (result.Granted)
    {
        log("world loot: take " + std::to_string(result.TakeNumber) + " from drop " + Hex(result.ContainerId) + " granted, it is ours");
        return;
    }
    if (mirror != Mirrors.end())
    {
        mirror->second.Holding = false;
    }
    auto service = game::ScriptService("NightCoopWorldLoot");
    int32_t rolledBack = 0;
    if (service && InSaveNow)
    {
        auto gameItem = ToGameItem(pending.Take.Item.Item);
        auto amount = static_cast<int32_t>(pending.Take.Item.Quantity);
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &gameItem);
        args.emplace_back(nullptr, &amount);
        game::CallMethod(service.instance, Script.RollBack, &rolledBack, args);
    }
    Count.RolledBack += static_cast<uint32_t>(rolledBack);
    log("world loot: take " + std::to_string(result.TakeNumber) + " from drop " + Hex(result.ContainerId) + " denied, rolled back " +
        std::to_string(rolledBack) + " of it" + (static_cast<uint32_t>(rolledBack) < pending.Take.Item.Quantity ? " (the rest is already spent)" : ""));
}

void TakeState(uint32_t fromPeer, const net::LootState& state, bool ownerInOurWorld, const LogFunction& log)
{
    if (!ownerInOurWorld)
    {
        ++Count.SkippedNotInWorld;
        return;
    }
    if (state.Kind != net::kLootDrop || (state.ContainerId >> 24) != fromPeer)
    {
        ++Count.SkippedNotDone;
        return;
    }
    const MirrorKey key {fromPeer, state.ContainerId};
    auto found = Mirrors.find(key);
    if (found == Mirrors.end())
    {
        if (state.State != net::kLootOpen)
        {
            return;
        }
        found = Mirrors.emplace(key, Mirror {}).first;
        log("world loot: peer " + std::to_string(fromPeer) + " shares drop " + Hex(state.ContainerId) + " with " + DescribeEntries(state.Entries));
    }
    auto& mirror = found->second;
    if (state.Revision < mirror.Latest.Revision)
    {
        return;
    }
    if (mirror.Holding && state.Revision > mirror.HeldRevision)
    {
        bool takesOpen = false;
        for (const auto& [takeNumber, pending] : PendingTakes)
        {
            takesOpen = takesOpen || (pending.Owner == fromPeer && pending.Take.ContainerId == state.ContainerId);
        }
        mirror.Holding = takesOpen;
        mirror.HeldRevision = state.Revision;
    }
    mirror.Latest = state;
    mirror.HeardMs = CurrentMs;
}
}

void Tick(uint64_t nowMs, bool inSave, bool sharing, const net::PlayerState* ourPlayer, net::RelaySession& session, RemoteBodies& bodies,
          const LogFunction& log)
{
    if (nowMs < NextTickMs)
    {
        return;
    }
    NextTickMs = nowMs + TickEveryMs;
    CurrentMs = nowMs;
    const bool inSession = session.GetStatus().InSession;
    if (WasInSave && (!inSave || !inSession))
    {
        LeaveWorld(session, sharing && inSession, log);
    }
    WasInSave = inSave && inSession;
    InSaveNow = WasInSave;
    if (!WasInSave || !Resolve(log))
    {
        return;
    }
    auto service = game::ScriptService("NightCoopWorldLoot");
    if (!service)
    {
        return;
    }
    bool noteDrops = sharing;
    RED4ext::StackArgs_t configureArgs;
    configureArgs.emplace_back(nullptr, &noteDrops);
    game::CallMethod(service.instance, Script.Configure, nullptr, configureArgs);
    TakeNews(service.instance, session, log);
    ServeOwnDrops(service.instance, sharing, ourPlayer, session, log);
    ServeMirrors(service.instance, ourPlayer, session, bodies, log);
    ResendTakes(session, log);
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool ownerInOurWorld, net::RelaySession& session, const LogFunction& log)
{
    if (!Script.Usable)
    {
        return;
    }
    const uint8_t type = net::MessageTypeOf(message);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::LootState))
    {
        net::LootState state;
        if (net::ReadLootState(message, state))
        {
            TakeState(fromPeer, state, ownerInOurWorld, log);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::LootTake))
    {
        net::LootTake take;
        if (InSaveNow && net::ReadLootTake(message, take))
        {
            AnswerTake(fromPeer, take, session, log);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::LootTakeResult))
    {
        net::LootTakeResult result;
        if (net::ReadLootTakeResult(message, result))
        {
            TakeResult(fromPeer, result, log);
        }
    }
}

bool TestDrop(const std::string& recordName, const LogFunction& log)
{
    auto service = game::ScriptService("NightCoopWorldLoot");
    if (!InSaveNow || !Resolve(log) || !service)
    {
        return false;
    }
    auto gameItem = items::ItemFromRecord(RED4ext::TweakDBID(recordName).value);
    bool dropped = false;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameItem);
    return game::CallMethod(service.instance, Script.TestDrop, &dropped, args) && dropped;
}

std::string Status()
{
    return "loot: our drops " + std::to_string(OwnDrops.size()) + " (" + std::to_string(Count.OwnShared) + " shared, " +
           std::to_string(Count.StatesSent) + " states sent, takes granted " + std::to_string(Count.TakesGranted) + ", denied " +
           std::to_string(Count.TakesDenied) + "); mirrors " + std::to_string(Mirrors.size()) + " (" + std::to_string(Count.MirrorsSpawned) +
           " spawned, takes sent " + std::to_string(Count.TakesSent) + ", waiting " + std::to_string(PendingTakes.size()) + ", rolled back " +
           std::to_string(Count.RolledBack) + ", given up " + std::to_string(Count.TakesGivenUp) + "); skipped " +
           std::to_string(Count.SkippedNotInWorld) + " not in our world, " + std::to_string(Count.SkippedNotDone) + " corpses (not done yet)";
}
}
