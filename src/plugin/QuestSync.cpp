#include "plugin/QuestSync.hpp"

#include "net/GameMessages.hpp"
#include "net/QuestMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/FactsDb.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/PartnerRoster.hpp"
#include "plugin/WorldHandoff.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <unordered_set>

namespace nightcoop_mac::quest_sync
{
namespace
{
constexpr uint64_t HostPollEveryMs = 250;
constexpr uint64_t TrackedCheckEveryMs = 1000;
constexpr uint64_t SendReportEveryMs = 10000;
constexpr uint64_t ApplyReportEveryMs = 10000;
constexpr uint64_t SameWorldQuietMs = 10000;
constexpr uint64_t AskAfterLoadMs = 3000;
constexpr uint64_t AskAgainMs = 20000;
constexpr uint32_t MostAsks = 3;
constexpr size_t FactsPerFrame = 400;
constexpr size_t JournalEntriesPerFrame = 100;
constexpr size_t MostQueuedFacts = 200000;
constexpr size_t MostQueuedJournalEntries = 50000;
constexpr uint32_t HighestPeerNumber = 0xFFFFFFF0u;
constexpr int32_t JournalChanged = 0;
constexpr int32_t JournalAlreadyLikeThat = 1;

const char* const OwnFactNames[] = {
    "game_was_loaded", "disable_tutorials", "tutorials_were_disabled", "disable_gyro_tutorials", "gmpl_player_dodged", "player_threw_a_grenade",
    "player_fired_a_weapon", "wanted_level", "wanted_chase_active", "number_of_ripperdocs_visited", "ping_installed", "braindanceVisionMode",
    "ResetAttributeDisabled", "sightseeing_binculars_ui_turned_off", "minigame_vertical_input", "minigame_horizontal_input",
    "initial_gadget_picked", "prevention_quest_disabled", "start_vr_tutorial_level", "q000_vr_tutorial_gyro_started",
    "q000_vr_tutorial_gyro_choice_enabled", "q000_vr_obligatory_courses_done", "tutorial_vehicle_hijack", "tutorial_vehicle_crack_lock",
    "tutorial_ripperdoc_hands_passed", "tutorial_ripperdoc_eyes_passed", "tutorial_ripperdoc_armor_passed", "tutorial_ripperdoc_items_added",
    "tutorial_scanner_bounty_displayed", "tutorial_fast_travel", "ranged_combat_tutorial", "melee_combat_tutorial", "fast_travel_tutorial",
    "encoded_shard_tutorial", "tech_weapon_tutorial", "swimming_tutorial", "street_cred_tutorial", "smart_weapon_tutorial", "skillcheck_tutorial",
    "power_weapon_tutorial", "police_system_tutorial", "perks_tutorial", "minigame_tutorial", "levelup_tutorial", "illegal_action_tutorial",
    "iconic_item_tutorial", "grenade_use_tutorial", "grenade_inventory_tutorial", "enemy_netrunner_tutorial", "cyberware_inventory_tutorial",
    "cyberdeck_inventory_tutorial", "consumable_use_tutorial", "consumable_inventory_tutorial", "combat_tutorial", "close_inventory_mode_tutorial",
    "car_damage_tutorial", "ep1_ripperdoc_tutorial_started", "ep1_ripperdoc_tutorial_seen", "ue_metro_next_station", "ue_metro_display_options",
    "ue_metro_active_station", "holo_setup_active", "holo_setup_started", "holo_setup_ended", "holo_interrupt_call", "nightcoop_host_world",
    "CalculateXPOldSaves", "EspionageAttributeRetrofix", "FreePerkRespec", "Perks2", "RelicDevelopmentData", "RetrofixCraftingComponents",
    "Skills2", "body_skillbook_bought_counter", "cool_skillbook_bought_counter", "ep1_tree_unlocked", "full_rpg_progression_on",
    "int_skillbook_bought_counter", "ref_skillbook_bought_counter", "tech_skillbook_bought_counter", "achilles_nash_owned", "ashura_twitch_owned",
    "baton_murphy_owned", "burya_airdrop_owned", "butchers_knife_owned", "defender_kurt_owned", "dildo_shop_owned", "dildo_stout_owned",
    "grad_airdrop_owned", "hercules_prototype_owned", "kappa_george_owned", "katana_cocktail_owned", "katana_e3_owned", "katana_hiromi_owned",
    "katana_saburo_owned", "katana_takemura_owned", "kenshin_frank_owned", "kenshin_royce_owned", "lexington_wilson_acquired", "liberty_dex_owned",
    "liberty_yorinobu_owned", "machete_borg_airdrop_owned", "metel_airdrop_owned", "metel_kurt_owned", "neurotoxin_iconic_acquired",
    "nova_doom_doom_owned", "nue_jackie_owned", "nue_maiko_owned", "omaha_suzie_owned", "osprey_prototype_owned", "pozhar_airdrop_owned",
    "punk_iconic_acquired", "saratoga_raffen_owned", "satara_brick_owned", "tanto_saburo_owned", "testera_nicolas_owned", "tinker_bell_owned",
    "umbra_bebe_owned", "w_melee_boss_hammer_owned"};

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Watch = nullptr;
    RED4ext::CBaseFunction* ClearChanges = nullptr;
    RED4ext::CBaseFunction* CollectJournal = nullptr;
    RED4ext::CBaseFunction* TrackedHash = nullptr;
    RED4ext::CBaseFunction* ApplyJournal = nullptr;
    RED4ext::CProperty* ChangedHashes = nullptr;
    RED4ext::CProperty* ChangedStates = nullptr;
    RED4ext::CProperty* ChangedNotify = nullptr;
    RED4ext::CProperty* SnapshotHashes = nullptr;
    RED4ext::CProperty* SnapshotStates = nullptr;
};

struct HostSide
{
    std::vector<std::pair<uint32_t, int32_t>> LastFacts;
    bool HaveBaseline = false;
    std::set<uint32_t> PeersThatAsked;
    std::map<uint32_t, uint32_t> WaitingSnapshots;
    std::map<uint32_t, std::pair<uint32_t, uint64_t>> LastSnapshotByPeer;
    uint32_t SnapshotCounter = 0;
    std::map<uint32_t, uint8_t> TrackModeSent;
    uint32_t Tracked = 0;
    bool TrackedKnown = false;
    bool Watching = false;
    bool ListProblemLogged = false;
    uint64_t NextPollMs = 0;
    uint64_t NextTrackedMs = 0;
    uint64_t NextReportMs = 0;
    uint32_t FactChangesSent = 0;
    uint32_t JournalChangesSent = 0;
    size_t PartnersReached = 0;
};

struct GuestSide
{
    bool WasInSave = false;
    bool WasInSession = false;
    bool Taking = false;
    uint32_t WorldNumber = 0;
    uint64_t InSaveSinceMs = 0;
    uint32_t AskedWorld = 0;
    uint32_t Asks = 0;
    uint64_t NextAskMs = 0;
    bool SnapshotCame = false;
    std::deque<net::QuestFact> Facts;
    std::deque<net::QuestJournalEntry> Journal;
    uint32_t HostTracked = 0;
    bool FollowHostTracking = false;
    uint32_t FollowedTracked = 0;
    uint32_t FactsChanged = 0;
    uint32_t FactsEqual = 0;
    uint32_t FactsKeptOwn = 0;
    uint32_t FactsNotStuck = 0;
    uint32_t JournalChangedHere = 0;
    uint32_t JournalEqual = 0;
    uint32_t JournalMissing = 0;
    uint32_t DroppedAway = 0;
    bool SnapshotQueued = false;
    uint64_t NextFactReportMs = 0;
    uint64_t NextJournalReportMs = 0;
    bool DropLogged = false;
    bool BrokenLogged = false;
};

constexpr uint8_t TrackModeType = 222;
bool GuestsFollowTracking = true;
Calls Script;
HostSide Host;
GuestSide Guest;
bool SyncOn = false;
std::unordered_set<uint32_t> OwnFactIds;

uint64_t ClockMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string HashText(uint32_t hash)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%#010x", hash);
    return text;
}

bool IsOwnFact(uint8_t table, uint32_t factId)
{
    if (OwnFactIds.empty())
    {
        for (const char* factName : OwnFactNames)
        {
            OwnFactIds.insert(net::FactIdOf(factName));
        }
    }
    return table == net::kQuestFactsTable && OwnFactIds.count(factId) != 0;
}

RED4ext::CProperty* ArrayProperty(RED4ext::CClass* owner, const char* name, const char* typeName)
{
    auto* property = owner ? owner->GetProperty(name) : nullptr;
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName ? property : nullptr;
}

bool ResolveScript(const LogFunction& log)
{
    if (Script.Resolved)
    {
        return Script.Usable;
    }
    Script.Resolved = true;
    auto* syncClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopQuestSync");
    Script.Watch = game::FindFunction("NightCoopQuestSync", "Watch");
    Script.ClearChanges = game::FindFunction("NightCoopQuestSync", "ClearChanges");
    Script.CollectJournal = game::FindFunction("NightCoopQuestSync", "CollectJournal");
    Script.TrackedHash = game::FindFunction("NightCoopQuestSync", "TrackedHash");
    Script.ApplyJournal = game::FindFunction("NightCoopQuestSync", "ApplyJournal");
    Script.ChangedHashes = ArrayProperty(syncClass, "changedHashes", "array:Uint32");
    Script.ChangedStates = ArrayProperty(syncClass, "changedStates", "array:Int32");
    Script.ChangedNotify = ArrayProperty(syncClass, "changedNotify", "array:Bool");
    Script.SnapshotHashes = ArrayProperty(syncClass, "snapshotHashes", "array:Uint32");
    Script.SnapshotStates = ArrayProperty(syncClass, "snapshotStates", "array:Int32");
    Script.Usable = game::ParamsAre(Script.Watch, {"Bool"}) && game::ParamsAre(Script.ClearChanges, {}) && game::ParamsAre(Script.CollectJournal, {}) &&
                    game::ReturnIs(Script.CollectJournal, "Int32") && game::ParamsAre(Script.TrackedHash, {}) && game::ReturnIs(Script.TrackedHash, "Uint32") &&
                    game::ParamsAre(Script.ApplyJournal, {"Uint32", "Int32", "Bool"}) && game::ReturnIs(Script.ApplyJournal, "Int32") &&
                    Script.ChangedHashes && Script.ChangedStates && Script.ChangedNotify && Script.SnapshotHashes && Script.SnapshotStates;
    log(Script.Usable ? "quests: journal entry states go thru the NightCoopQuestSync script"
                      : "quests: no NightCoopQuestSync script service, the journal is not shared (facts still are)");
    return Script.Usable;
}

void SetWatch(bool on)
{
    if (on == Host.Watching || !Script.Usable)
    {
        return;
    }
    auto service = game::ScriptService("NightCoopQuestSync");
    if (!service)
    {
        return;
    }
    bool watch = on;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &watch);
    game::CallMethod(service.instance, Script.Watch, nullptr, args);
    Host.Watching = on;
}

std::vector<net::QuestJournalEntry> TakeJournalChanges()
{
    std::vector<net::QuestJournalEntry> changes;
    if (!Script.Usable || !Host.Watching)
    {
        return changes;
    }
    auto service = game::ScriptService("NightCoopQuestSync");
    if (!service)
    {
        return changes;
    }
    const auto* hashes = Script.ChangedHashes->GetValuePtr<RED4ext::DynArray<uint32_t>>(service.instance);
    const auto* states = Script.ChangedStates->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    const auto* notify = Script.ChangedNotify->GetValuePtr<RED4ext::DynArray<bool>>(service.instance);
    if (hashes->Size() == 0)
    {
        return changes;
    }
    for (uint32_t index = 0; index < hashes->Size() && index < states->Size(); ++index)
    {
        const int32_t state = (*states)[index];
        if (state < 0 || state > net::kJournalMostState)
        {
            continue;
        }
        net::QuestJournalEntry entry;
        entry.EntryHash = (*hashes)[index];
        entry.State = static_cast<uint8_t>(state);
        entry.Notify = index < notify->Size() && (*notify)[index] ? net::kJournalNotify : net::kJournalQuiet;
        changes.push_back(entry);
    }
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, Script.ClearChanges, nullptr, noArgs);
    return changes;
}

std::vector<net::QuestJournalEntry> CollectJournal()
{
    std::vector<net::QuestJournalEntry> entries;
    auto service = game::ScriptService("NightCoopQuestSync");
    if (!Script.Usable || !service)
    {
        return entries;
    }
    int32_t count = 0;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, Script.CollectJournal, &count, noArgs);
    const auto* hashes = Script.SnapshotHashes->GetValuePtr<RED4ext::DynArray<uint32_t>>(service.instance);
    const auto* states = Script.SnapshotStates->GetValuePtr<RED4ext::DynArray<int32_t>>(service.instance);
    std::set<uint32_t> seen;
    for (uint32_t index = 0; index < hashes->Size() && index < states->Size(); ++index)
    {
        const int32_t state = (*states)[index];
        if (state < 0 || state > net::kJournalMostState || !seen.insert((*hashes)[index]).second)
        {
            continue;
        }
        net::QuestJournalEntry entry;
        entry.EntryHash = (*hashes)[index];
        entry.State = static_cast<uint8_t>(state);
        entry.Notify = net::kJournalQuiet;
        entries.push_back(entry);
    }
    return entries;
}

bool CheckTracked(uint64_t nowMs)
{
    if (!Script.Usable || nowMs < Host.NextTrackedMs)
    {
        return false;
    }
    Host.NextTrackedMs = nowMs + TrackedCheckEveryMs;
    auto service = game::ScriptService("NightCoopQuestSync");
    if (!service)
    {
        return false;
    }
    uint32_t tracked = 0;
    RED4ext::StackArgs_t noArgs;
    game::CallMethod(service.instance, Script.TrackedHash, &tracked, noArgs);
    if (Host.TrackedKnown && tracked == Host.Tracked)
    {
        return false;
    }
    Host.Tracked = tracked;
    Host.TrackedKnown = true;
    return true;
}

std::vector<net::QuestFact> ChangedFacts(const std::vector<std::pair<uint32_t, int32_t>>& before, const std::vector<std::pair<uint32_t, int32_t>>& now)
{
    std::vector<net::QuestFact> changes;
    auto note = [&changes](uint32_t factId, int32_t value) {
        if (!IsOwnFact(net::kQuestFactsTable, factId))
        {
            net::QuestFact fact;
            fact.FactId = factId;
            fact.Value = value;
            changes.push_back(fact);
        }
    };
    size_t oldIndex = 0;
    size_t newIndex = 0;
    while (oldIndex < before.size() || newIndex < now.size())
    {
        if (newIndex < now.size() && (oldIndex >= before.size() || now[newIndex].first < before[oldIndex].first))
        {
            note(now[newIndex].first, now[newIndex].second);
            ++newIndex;
        }
        else if (oldIndex < before.size() && (newIndex >= now.size() || before[oldIndex].first < now[newIndex].first))
        {
            if (before[oldIndex].second != 0)
            {
                note(before[oldIndex].first, 0);
            }
            ++oldIndex;
        }
        else
        {
            if (before[oldIndex].second != now[newIndex].second)
            {
                note(now[newIndex].first, now[newIndex].second);
            }
            ++oldIndex;
            ++newIndex;
        }
    }
    return changes;
}

std::vector<uint32_t> PartnersInOurWorld(net::RelaySession& session)
{
    const auto nicknames = session.PeerNicknames();
    std::erase_if(Host.PeersThatAsked, [&nicknames](uint32_t peer) { return nicknames.count(peer) == 0; });
    std::vector<uint32_t> partners;
    for (const auto& [peerNumber, nickname] : nicknames)
    {
        if (partner_roster::PeerInHostWorld(peerNumber) || Host.PeersThatAsked.count(peerNumber))
        {
            partners.push_back(peerNumber);
        }
    }
    return partners;
}

void SendAll(const std::vector<std::vector<uint8_t>>& messages, uint32_t peer, net::RelaySession& session)
{
    for (const auto& message : messages)
    {
        session.SendGameMessage(message, static_cast<int32_t>(peer));
    }
}

void ServeSnapshots(uint64_t nowMs, bool factsListed, const std::vector<std::pair<uint32_t, int32_t>>& facts, net::RelaySession& session,
                    const LogFunction& log)
{
    if (Host.WaitingSnapshots.empty() || (!factsListed && !facts_db::TurnedOff()))
    {
        return;
    }
    const auto nicknames = session.PeerNicknames();
    std::vector<net::QuestFact> snapshotFacts;
    for (const auto& [factId, value] : facts)
    {
        if (!IsOwnFact(net::kQuestFactsTable, factId))
        {
            net::QuestFact fact;
            fact.FactId = factId;
            fact.Value = value;
            snapshotFacts.push_back(fact);
        }
    }
    const auto journal = CollectJournal();
    for (const auto& [peer, world] : Host.WaitingSnapshots)
    {
        if (nicknames.count(peer) == 0)
        {
            continue;
        }
        if (++Host.SnapshotCounter == 0)
        {
            ++Host.SnapshotCounter;
        }
        if (factsListed)
        {
            SendAll(net::SplitQuestFacts(true, Host.SnapshotCounter, snapshotFacts), peer, session);
        }
        if (Script.Usable)
        {
            SendAll(net::SplitQuestJournal(true, Host.SnapshotCounter, Host.TrackedKnown, Host.Tracked, journal), peer, session);
        }
        Host.LastSnapshotByPeer[peer] = {world, nowMs};
        log("quests: peer " + std::to_string(peer) + " loaded their world #" + std::to_string(world) + ", they get our facts and journal again (snapshot #" +
            std::to_string(Host.SnapshotCounter) + ": " + (factsListed ? std::to_string(snapshotFacts.size()) : std::string("no")) + " facts, " +
            std::to_string(journal.size()) + " journal entries)");
    }
    Host.WaitingSnapshots.clear();
}

void HostTick(uint64_t nowMs, bool inSave, net::RelaySession& session, const LogFunction& log)
{
    if (!inSave)
    {
        Host.HaveBaseline = false;
        Host.LastFacts.clear();
        Host.TrackedKnown = false;
        SetWatch(false);
        return;
    }
    if (nowMs < Host.NextPollMs)
    {
        return;
    }
    Host.NextPollMs = nowMs + HostPollEveryMs;
    ResolveScript(log);
    const auto partners = PartnersInOurWorld(session);
    if (partners.empty() && Host.WaitingSnapshots.empty())
    {
        Host.HaveBaseline = false;
        Host.LastFacts.clear();
        SetWatch(false);
        return;
    }
    SetWatch(true);
    std::vector<std::pair<uint32_t, int32_t>> facts;
    const bool factsListed = facts_db::Ready(log) && facts_db::ListTable(net::kQuestFactsTable, facts);
    if (!factsListed && facts_db::TurnedOff() && !Host.ListProblemLogged)
    {
        Host.ListProblemLogged = true;
        log("quests: our facts table cannot be read, partners get our journal only");
    }
    std::sort(facts.begin(), facts.end());
    std::vector<net::QuestFact> factChanges;
    if (factsListed)
    {
        if (Host.HaveBaseline)
        {
            factChanges = ChangedFacts(Host.LastFacts, facts);
        }
        Host.LastFacts = facts;
        Host.HaveBaseline = true;
    }
    const auto journalChanges = TakeJournalChanges();
    const bool trackedChanged = CheckTracked(nowMs);
    for (const uint32_t peer : partners)
    {
        if (!factChanges.empty())
        {
            SendAll(net::SplitQuestFacts(false, 0, factChanges), peer, session);
        }
        if (!journalChanges.empty() || trackedChanged)
        {
            SendAll(net::SplitQuestJournal(false, 0, trackedChanged, Host.Tracked, journalChanges), peer, session);
        }
    }
    if (!partners.empty())
    {
        Host.FactChangesSent += static_cast<uint32_t>(factChanges.size());
        Host.JournalChangesSent += static_cast<uint32_t>(journalChanges.size());
        Host.PartnersReached = partners.size();
    }
    ServeSnapshots(nowMs, factsListed, facts, session, log);
    if (nowMs >= Host.NextReportMs)
    {
        Host.NextReportMs = nowMs + SendReportEveryMs;
        if (Host.FactChangesSent || Host.JournalChangesSent)
        {
            log("quests: " + std::to_string(Host.FactChangesSent) + " fact change(s) and " + std::to_string(Host.JournalChangesSent) +
                " journal change(s) went to " + std::to_string(Host.PartnersReached) + " partner(s) in our world (" + std::to_string(facts.size()) +
                " facts in table 1)");
        }
        Host.FactChangesSent = 0;
        Host.JournalChangesSent = 0;
    }
}

void AskForSnapshot(uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    if (!partner_roster::HostInItsWorld() || nowMs - Guest.InSaveSinceMs < AskAfterLoadMs)
    {
        return;
    }
    const bool newWorld = Guest.AskedWorld != Guest.WorldNumber;
    const bool askAgain = !newWorld && !Guest.SnapshotCame && Guest.Asks < MostAsks && nowMs >= Guest.NextAskMs;
    if (!newWorld && !askAgain)
    {
        return;
    }
    if (newWorld)
    {
        Guest.Asks = 0;
        Guest.SnapshotCame = false;
    }
    session.SendGameMessage(net::WriteQuestWant(Guest.WorldNumber), 0);
    Guest.AskedWorld = Guest.WorldNumber;
    ++Guest.Asks;
    Guest.NextAskMs = nowMs + AskAgainMs;
    log("quests: asked the host for its facts and journal (our world #" + std::to_string(Guest.WorldNumber) +
        (Guest.Asks > 1 ? ", try " + std::to_string(Guest.Asks) : std::string()) + ")");
}

void ApplyFacts(uint64_t nowMs, const LogFunction& log)
{
    if (Guest.Facts.empty())
    {
        return;
    }
    if (!facts_db::Ready(log))
    {
        if (facts_db::TurnedOff())
        {
            if (!Guest.BrokenLogged)
            {
                Guest.BrokenLogged = true;
                log("quests: our facts db is off, the hosts facts get dropped (the journal still follows)");
            }
            Guest.Facts.clear();
        }
        return;
    }
    for (size_t done = 0; done < FactsPerFrame && !Guest.Facts.empty(); ++done)
    {
        const net::QuestFact fact = Guest.Facts.front();
        Guest.Facts.pop_front();
        if (IsOwnFact(fact.Table, fact.FactId))
        {
            ++Guest.FactsKeptOwn;
            continue;
        }
        int32_t ours = 0;
        if (!facts_db::ReadFact(fact.Table, fact.FactId, ours))
        {
            ++Guest.FactsNotStuck;
            continue;
        }
        if (ours == fact.Value)
        {
            ++Guest.FactsEqual;
            continue;
        }
        int32_t afterwards = 0;
        if (facts_db::WriteFact(fact.Table, fact.FactId, fact.Value) && facts_db::ReadFact(fact.Table, fact.FactId, afterwards) && afterwards == fact.Value)
        {
            ++Guest.FactsChanged;
        }
        else
        {
            ++Guest.FactsNotStuck;
        }
    }
    if (Guest.Facts.empty() && (Guest.SnapshotQueued || nowMs >= Guest.NextFactReportMs))
    {
        Guest.NextFactReportMs = nowMs + ApplyReportEveryMs;
        log("quests: host facts applied (" + std::to_string(Guest.FactsChanged) + " changed here, " + std::to_string(Guest.FactsEqual) +
            " already equal, " + std::to_string(Guest.FactsKeptOwn) + " kept as our own" +
            (Guest.FactsNotStuck ? ", " + std::to_string(Guest.FactsNotStuck) + " did not stick" : std::string()) + ")");
        Guest.FactsChanged = 0;
        Guest.FactsEqual = 0;
        Guest.FactsKeptOwn = 0;
        Guest.FactsNotStuck = 0;
    }
}

void ApplyJournal(uint64_t nowMs, const LogFunction& log)
{
    if (Guest.Journal.empty())
    {
        return;
    }
    auto service = game::ScriptService("NightCoopQuestSync");
    if (!ResolveScript(log) || !service)
    {
        Guest.Journal.clear();
        return;
    }
    for (size_t done = 0; done < JournalEntriesPerFrame && !Guest.Journal.empty(); ++done)
    {
        const net::QuestJournalEntry entry = Guest.Journal.front();
        Guest.Journal.pop_front();
        uint32_t hash = entry.EntryHash;
        int32_t state = entry.State;
        bool notify = entry.Notify == net::kJournalNotify;
        int32_t result = -1;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &hash);
        args.emplace_back(nullptr, &state);
        args.emplace_back(nullptr, &notify);
        game::CallMethod(service.instance, Script.ApplyJournal, &result, args);
        if (result == JournalChanged)
        {
            ++Guest.JournalChangedHere;
        }
        else if (result == JournalAlreadyLikeThat)
        {
            ++Guest.JournalEqual;
        }
        else
        {
            ++Guest.JournalMissing;
        }
    }
    if (Guest.Journal.empty() && (Guest.SnapshotQueued || nowMs >= Guest.NextJournalReportMs))
    {
        Guest.NextJournalReportMs = nowMs + ApplyReportEveryMs;
        log("quests: host journal applied (" + std::to_string(Guest.JournalChangedHere) + " entries changed here, " + std::to_string(Guest.JournalEqual) +
            " already like that, " + std::to_string(Guest.JournalMissing) + " not in our journal)");
        Guest.JournalChangedHere = 0;
        Guest.JournalEqual = 0;
        Guest.JournalMissing = 0;
    }
}

void GuestTick(uint64_t nowMs, bool inSave, bool inAnyWorld, net::RelaySession& session, const LogFunction& log)
{
    if (inSave && !Guest.WasInSave)
    {
        ++Guest.WorldNumber;
        Guest.InSaveSinceMs = nowMs;
    }
    Guest.WasInSave = inSave;
    const bool taking = inSave && (world_handoff::InHostWorld() || inAnyWorld);
    if (taking != Guest.Taking)
    {
        Guest.Taking = taking;
        Guest.DropLogged = false;
        if (!taking)
        {
            Guest.Facts.clear();
            Guest.Journal.clear();
        }
    }
    if (!taking)
    {
        return;
    }
    AskForSnapshot(nowMs, session, log);
    ApplyFacts(nowMs, log);
    ApplyJournal(nowMs, log);
    static auto* trackByHash = game::FindFunction("NightCoopQuestSync", "TrackByHash");
    if (Guest.FollowHostTracking && Guest.HostTracked && Guest.HostTracked != Guest.FollowedTracked && Guest.Journal.empty() && trackByHash)
    {
        auto service = game::ScriptService("NightCoopQuestSync");
        uint32_t hash = Guest.HostTracked;
        bool tracked = false;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &hash);
        if (service && game::CallMethod(service.instance, trackByHash, &tracked, args) && tracked)
        {
            Guest.FollowedTracked = Guest.HostTracked;
            log("quests: we track the hosts objective " + HashText(hash) + " too");
        }
    }
    if (Guest.Facts.empty() && Guest.Journal.empty())
    {
        Guest.SnapshotQueued = false;
    }
}

bool Accepting(const LogFunction& log)
{
    if (Guest.Taking)
    {
        return true;
    }
    ++Guest.DroppedAway;
    if (!Guest.DropLogged)
    {
        Guest.DropLogged = true;
        log("quests: the host shares its story but we are not in its world, dropped (" + std::to_string(Guest.DroppedAway) + " batch(es) so far)");
    }
    return false;
}

void TakeHostFacts(const net::QuestFacts& facts, const LogFunction& log)
{
    if (!Accepting(log))
    {
        return;
    }
    if (facts.Batch != net::kQuestDelta)
    {
        Guest.SnapshotQueued = true;
    }
    if (facts.Batch == net::kQuestSnapshotBegin)
    {
        Guest.SnapshotCame = true;
        log("quests: the hosts snapshot #" + std::to_string(facts.Number) + " comes (" + std::to_string(facts.Total) + " facts)");
    }
    for (const auto& fact : facts.Facts)
    {
        if (Guest.Facts.size() < MostQueuedFacts)
        {
            Guest.Facts.push_back(fact);
        }
    }
}

void TakeHostJournal(const net::QuestJournal& journal, const LogFunction& log)
{
    if (!Accepting(log))
    {
        return;
    }
    if (journal.Batch != net::kQuestDelta)
    {
        Guest.SnapshotQueued = true;
    }
    if (journal.Batch == net::kQuestSnapshotBegin)
    {
        Guest.SnapshotCame = true;
        log("quests: the hosts journal snapshot #" + std::to_string(journal.Number) + " comes (" + std::to_string(journal.Total) + " entries)");
    }
    if (journal.TrackedKnown && journal.Tracked != Guest.HostTracked)
    {
        Guest.HostTracked = journal.Tracked;
        log("quests: the host tracks journal entry " + HashText(journal.Tracked) + (Guest.FollowHostTracking ? " (we follow it)" : " (each player tracks their own here)"));
    }
    for (const auto& entry : journal.Entries)
    {
        if (Guest.Journal.size() < MostQueuedJournalEntries)
        {
            Guest.Journal.push_back(entry);
        }
    }
}

void SendTrackModes(bool guestsFollow, net::RelaySession& session)
{
    const uint8_t mode = guestsFollow ? 1 : 0;
    for (const uint32_t peer : PartnersInOurWorld(session))
    {
        auto sent = Host.TrackModeSent.find(peer);
        if (sent != Host.TrackModeSent.end() && sent->second == mode)
        {
            continue;
        }
        Host.TrackModeSent[peer] = mode;
        nc::proto::Writer word(static_cast<nc::proto::MsgType>(TrackModeType));
        word.PutU8(1);
        word.PutU8(mode);
        session.SendGameMessage(word.Data(), static_cast<int32_t>(peer));
    }
}

void TakeWant(uint32_t fromPeer, uint32_t world, const LogFunction& log)
{
    const auto last = Host.LastSnapshotByPeer.find(fromPeer);
    if (last != Host.LastSnapshotByPeer.end() && last->second.first == world && ClockMs() - last->second.second < SameWorldQuietMs)
    {
        log("quests: peer " + std::to_string(fromPeer) + " asked for world #" + std::to_string(world) + " again, it has just got one");
        return;
    }
    Host.WaitingSnapshots[fromPeer] = world;
    Host.PeersThatAsked.insert(fromPeer);
}
}

void Tick(uint64_t nowMs, bool inSave, bool enabled, bool inAnyWorld, net::RelaySession& session, const LogFunction& log)
{
    const auto status = session.GetStatus();
    SyncOn = enabled && status.InSession;
    if (status.InSession && !Guest.WasInSession)
    {
        Guest.AskedWorld = 0;
        Guest.Asks = 0;
    }
    Guest.WasInSession = status.InSession;
    if (!SyncOn)
    {
        SetWatch(false);
        return;
    }
    if (status.MyPeerNumber == 0)
    {
        HostTick(nowMs, inSave, session, log);
        SendTrackModes(GuestsFollowTracking, session);
    }
    else
    {
        GuestTick(nowMs, inSave, inAnyWorld, session, log);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    if (!SyncOn)
    {
        return;
    }
    const uint8_t type = net::MessageTypeOf(message);
    const bool weAreHost = session.GetStatus().MyPeerNumber == 0;
    if (type == TrackModeType)
    {
        nc::proto::Reader reader(message.data(), message.size());
        reader.ReadHeader();
        const uint8_t version = reader.GetU8();
        const uint8_t mode = reader.GetU8();
        if (!weAreHost && fromPeer == 0 && reader.Ok() && reader.Remaining() == 0 && version == 1 && mode < 2 && (mode == 1) != Guest.FollowHostTracking)
        {
            Guest.FollowHostTracking = mode == 1;
            Guest.FollowedTracked = 0;
            log(mode == 1 ? "quests: the host wants us to track its objective" : "quests: each player tracks their own objective");
        }
        return;
    }
    if (type == static_cast<uint8_t>(nc::proto::MsgType::QuestWant))
    {
        uint32_t world = 0;
        if (weAreHost && fromPeer != 0 && fromPeer < HighestPeerNumber && net::ReadQuestWant(message, world))
        {
            TakeWant(fromPeer, world, log);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::QuestFacts))
    {
        net::QuestFacts facts;
        if (!weAreHost && fromPeer == 0 && net::ReadQuestFacts(message, facts))
        {
            TakeHostFacts(facts, log);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::QuestJournal))
    {
        net::QuestJournal journal;
        if (!weAreHost && fromPeer == 0 && net::ReadQuestJournal(message, journal))
        {
            TakeHostJournal(journal, log);
        }
    }
}

std::vector<std::pair<uint32_t, int32_t>> SharedChanges(const std::vector<std::pair<uint32_t, int32_t>>& before, const std::vector<std::pair<uint32_t, int32_t>>& now)
{
    std::vector<std::pair<uint32_t, int32_t>> changes;
    for (const auto& fact : ChangedFacts(before, now))
    {
        changes.push_back({fact.FactId, fact.Value});
    }
    return changes;
}

void SetGuestsFollowTracking(bool follow)
{
    GuestsFollowTracking = follow;
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestFacts) || messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestJournal) ||
           messageType == static_cast<uint8_t>(nc::proto::MsgType::QuestWant) || messageType == TrackModeType;
}
}
