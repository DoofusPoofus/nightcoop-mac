#include "plugin/DialogScenes.hpp"

#include "net/DialogMessages.hpp"
#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "plugin/GameCalls.hpp"
#include "plugin/Hud.hpp"
#include "plugin/KeyWatch.hpp"
#include "plugin/PartnerRoster.hpp"
#include "plugin/StateMachineBoard.hpp"
#include "plugin/TestKeys.hpp"
#include "plugin/WorldHandoff.hpp"

#include <ProtocolCore.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace nightcoop_mac::dialog_scenes
{
namespace
{
constexpr uint64_t DialogTickEveryMs = 100;
constexpr uint64_t BodyTickEveryMs = 250;
constexpr uint64_t HiddenRefreshEveryMs = 1000;
constexpr float HideInsideMeters = 0.5f;
constexpr float ShowPastMeters = 1.0f;
constexpr int32_t FirstPlacingSceneTier = 3;
constexpr int32_t LastSceneTier = 5;
constexpr float SceneHideMeters = 30.0f;
constexpr float VoterMeters = 60.0f;
constexpr float SubtitleMeters = 40.0f;
constexpr uint64_t VoteTimeoutMs = 60000;
constexpr uint64_t GameTimerMarginMs = 1500;
constexpr uint64_t ApplyLeadMs = 700;
constexpr uint64_t ApplyGiveUpMs = 90000;
constexpr uint64_t ArrowStepMs = 150;
constexpr uint64_t PressCheckMs = 1500;
constexpr int MostArrowPresses = 32;
constexpr int MostApplyPresses = 3;
constexpr uint64_t EarlyPressWaitMs = 8000;
constexpr uint64_t OwnSceneQuietMs = 5000;
constexpr uint32_t RelayedLineTypes = 0x2c06;
constexpr uint8_t RegularLineType = 1;
constexpr uint8_t HolocallLineType = 2;
constexpr int32_t OwnerlessRegularLineType = 10;
constexpr uint64_t RelayedLineMarker = 0x4E43000000000000ULL;
constexpr uint64_t PartnerSkipLongestMs = 15000;
constexpr uint64_t UnknownSceneId = 1;
constexpr size_t MostVoters = 8;
constexpr size_t MostOptions = 16;
constexpr const char* UpArrowCharacter = "\xEF\x9C\x80";
constexpr const char* DownArrowCharacter = "\xEF\x9C\x81";

struct Calls
{
    bool Resolved = false;
    bool Usable = false;
    RED4ext::CBaseFunction* Refresh = nullptr;
    RED4ext::CBaseFunction* ClearLines = nullptr;
    RED4ext::CBaseFunction* ShowRelayedLine = nullptr;
    RED4ext::CBaseFunction* HideRelayedLine = nullptr;
    RED4ext::CBaseFunction* SetBodyHidden = nullptr;
    RED4ext::CBaseFunction* StartSkip = nullptr;
    RED4ext::CBaseFunction* StopSkip = nullptr;
    RED4ext::CProperty* HubOpen = nullptr;
    RED4ext::CProperty* HubId = nullptr;
    RED4ext::CProperty* HubTitle = nullptr;
    RED4ext::CProperty* HubTimed = nullptr;
    RED4ext::CProperty* HubSeconds = nullptr;
    RED4ext::CProperty* SelectedIndex = nullptr;
    RED4ext::CProperty* RowTexts = nullptr;
    RED4ext::CProperty* RowTypes = nullptr;
    RED4ext::CProperty* LineIds = nullptr;
    RED4ext::CProperty* LineTypes = nullptr;
    RED4ext::CProperty* LineSeconds = nullptr;
    RED4ext::CProperty* LineSpeakers = nullptr;
    RED4ext::CProperty* LineTexts = nullptr;
    RED4ext::CProperty* OwnLinesSeen = nullptr;
    RED4ext::CProperty* Language = nullptr;
    RED4ext::CProperty* SkipActive = nullptr;
    RED4ext::CProperty* SkipAllowed = nullptr;
};

struct ChoiceWindow
{
    bool Open = false;
    int32_t HubId = 0;
    std::string Title;
    std::vector<std::string> Rows;
    std::vector<uint32_t> RowTypes;
    int32_t Selected = 0;
    bool Timed = false;
    float Seconds = 0;
    uint64_t Key = 0;
};

struct HostVote
{
    net::DialogBallot Ballot;
    uint64_t WindowKey = 0;
    std::map<uint32_t, int8_t> Votes;
    uint64_t DeadlineMs = 0;
    uint8_t State = 0;
};

struct GuestVote
{
    net::DialogBallot Ballot;
    net::DialogTally Tally;
    uint8_t State = 0;
};

struct PendingApply
{
    bool Active = false;
    uint32_t VoteId = 0;
    int8_t Option = -1;
    std::string OptionText;
    std::string Title;
    size_t RowCount = 0;
    bool WindowWasOpen = false;
    uint64_t ApplyAtSessionMs = 0;
    uint64_t GiveUpAtMs = 0;
    uint64_t NextStepMs = 0;
    int ArrowPresses = 0;
    int ApplyPresses = 0;
    uint64_t PressedWindowKey = 0;
};

struct EarlyPress
{
    bool Waiting = false;
    bool Released = false;
    int32_t Row = -1;
    uint64_t AtMs = 0;
};

struct BodyHiding
{
    bool Hidden = false;
    bool Overlapping = false;
    void* Instance = nullptr;
    uint64_t NextRefreshMs = 0;
};

struct RelayedLine
{
    uint64_t Id = 0;
    uint64_t HideAtMs = 0;
};

struct Spot
{
    float X = 0;
    float Y = 0;
    float Z = 0;
};

Calls Game;
DialogSettings Settings;
uint64_t NextDialogTickMs = 0;
uint64_t NextBodyTickMs = 0;
uint64_t LastTickMs = 0;
bool WasRunning = false;
ChoiceWindow Window;
HostVote Hosting;
uint32_t VoteCounter = 0;
bool WaitForWindowToClose = false;
GuestVote Joined;
PendingApply Applying;
EarlyPress Early;
bool HoldingKeys = false;
std::map<uint32_t, BodyHiding> BodyStates;
std::vector<RelayedLine> RelayedLines;
int32_t OwnLinesSeen = 0;
uint64_t LastOwnLineMs = 0;
uint64_t LastOwnLineId = 0;
std::string OurLanguage;
bool LocalKnown = false;
Spot LocalSpot;
std::map<uint32_t, Spot> PeerSpots;
std::set<uint32_t> VoteModeSentTo;
int32_t SceneTier = 0;
bool OurSkipRunning = false;
uint64_t OurSkipStartedMs = 0;
bool PartnerSkipRunning = false;
uint64_t PartnerSkipUntilMs = 0;
uint64_t IgnoreOurSkipUntilMs = 0;

RED4ext::CProperty* FieldOf(RED4ext::CClass* owner, const char* name, const char* typeName)
{
    auto* property = owner ? owner->GetProperty(name) : nullptr;
    return property && property->type && std::string(property->type->GetName().ToString()) == typeName ? property : nullptr;
}

bool Resolve(const LogFunction& log)
{
    if (Game.Resolved)
    {
        return Game.Usable;
    }
    Game.Resolved = true;
    auto* dialogsClass = RED4ext::CRTTISystem::Get()->GetClass("NightCoopDialogs");
    Game.Refresh = game::FindFunction("NightCoopDialogs", "Refresh");
    Game.ClearLines = game::FindFunction("NightCoopDialogs", "ClearLines");
    Game.ShowRelayedLine = game::FindFunction("NightCoopDialogs", "ShowRelayedLine");
    Game.HideRelayedLine = game::FindFunction("NightCoopDialogs", "HideRelayedLine");
    Game.SetBodyHidden = game::FindFunction("NightCoopDialogs", "SetBodyHidden");
    Game.StartSkip = game::FindFunction("NightCoopDialogs", "StartSkip");
    Game.StopSkip = game::FindFunction("NightCoopDialogs", "StopSkip");
    Game.HubOpen = FieldOf(dialogsClass, "hubOpen", "Bool");
    Game.HubId = FieldOf(dialogsClass, "hubId", "Int32");
    Game.HubTitle = FieldOf(dialogsClass, "hubTitle", "String");
    Game.HubTimed = FieldOf(dialogsClass, "hubTimed", "Bool");
    Game.HubSeconds = FieldOf(dialogsClass, "hubSeconds", "Float");
    Game.SelectedIndex = FieldOf(dialogsClass, "selectedIndex", "Int32");
    Game.RowTexts = FieldOf(dialogsClass, "rowTexts", "array:String");
    Game.RowTypes = FieldOf(dialogsClass, "rowTypes", "array:Int32");
    Game.LineIds = FieldOf(dialogsClass, "lineIds", "array:CRUID");
    Game.LineTypes = FieldOf(dialogsClass, "lineTypes", "array:Int32");
    Game.LineSeconds = FieldOf(dialogsClass, "lineSeconds", "array:Float");
    Game.LineSpeakers = FieldOf(dialogsClass, "lineSpeakers", "array:String");
    Game.LineTexts = FieldOf(dialogsClass, "lineTexts", "array:String");
    Game.OwnLinesSeen = FieldOf(dialogsClass, "ownLinesSeen", "Int32");
    Game.Language = FieldOf(dialogsClass, "language", "String");
    Game.SkipActive = FieldOf(dialogsClass, "skipActive", "Bool");
    Game.SkipAllowed = FieldOf(dialogsClass, "skipAllowed", "Bool");
    Game.Usable = game::ParamsAre(Game.Refresh, {}) && game::ParamsAre(Game.ClearLines, {}) &&
                  game::ParamsAre(Game.ShowRelayedLine, {"CRUID", "String", "String", "Int32", "Float"}) &&
                  game::ParamsAre(Game.HideRelayedLine, {"CRUID"}) && game::ParamsAre(Game.SetBodyHidden, {"handle:entEntity", "Bool"}) &&
                  game::ReturnIs(Game.SetBodyHidden, "Int32") && game::ParamsAre(Game.StartSkip, {}) && game::ReturnIs(Game.StartSkip, "Bool") &&
                  game::ParamsAre(Game.StopSkip, {}) && Game.HubOpen && Game.HubId && Game.HubTitle && Game.HubTimed && Game.HubSeconds &&
                  Game.SelectedIndex && Game.RowTexts && Game.RowTypes && Game.LineIds && Game.LineTypes && Game.LineSeconds && Game.LineSpeakers &&
                  Game.LineTexts && Game.OwnLinesSeen && Game.Language && Game.SkipActive && Game.SkipAllowed;
    log(Game.Usable ? "dialogs: choice votes, subtitles, partner bodies in scenes and shared skips are on"
                    : "dialogs: no NightCoopDialogs script service, each player keeps their own dialogs");
    return Game.Usable;
}

void CallService(RED4ext::Handle<RED4ext::IScriptable>& service, RED4ext::CBaseFunction* function, RED4ext::StackArgs_t& args, void* out = nullptr)
{
    game::CallMethod(service.instance, function, out, args);
}

uint64_t TextKey(const std::string& title, const std::vector<std::string>& rows)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    auto mix = [&hash](const std::string& text) {
        for (char letter : text)
        {
            hash = (hash ^ static_cast<uint8_t>(letter)) * 0x100000001b3ULL;
        }
        hash = (hash ^ 0x0A) * 0x100000001b3ULL;
    };
    mix(title);
    for (const auto& row : rows)
    {
        mix(row);
    }
    return hash;
}

void ReadWindow(RED4ext::Handle<RED4ext::IScriptable>& service)
{
    void* instance = service.instance;
    Window.Open = *Game.HubOpen->GetValuePtr<bool>(instance);
    Window.HubId = *Game.HubId->GetValuePtr<int32_t>(instance);
    Window.Title = Game.HubTitle->GetValuePtr<RED4ext::CString>(instance)->c_str();
    Window.Timed = *Game.HubTimed->GetValuePtr<bool>(instance);
    Window.Seconds = *Game.HubSeconds->GetValuePtr<float>(instance);
    Window.Selected = *Game.SelectedIndex->GetValuePtr<int32_t>(instance);
    const auto* rowTexts = Game.RowTexts->GetValuePtr<RED4ext::DynArray<RED4ext::CString>>(instance);
    const auto* rowTypes = Game.RowTypes->GetValuePtr<RED4ext::DynArray<int32_t>>(instance);
    Window.Rows.clear();
    Window.RowTypes.clear();
    for (uint32_t index = 0; index < rowTexts->Size(); ++index)
    {
        Window.Rows.emplace_back((*rowTexts)[index].c_str());
        Window.RowTypes.push_back(index < rowTypes->Size() ? static_cast<uint32_t>((*rowTypes)[index]) : 0);
    }
    Window.Open = Window.Open && !Window.Rows.empty();
    Window.Key = Window.Open ? TextKey(Window.Title, Window.Rows) : 0;
}

std::string Quoted(const std::string& text)
{
    return "'" + text + "'";
}

float DistanceBetween(const Spot& first, const Spot& second)
{
    const float dx = first.X - second.X;
    const float dy = first.Y - second.Y;
    const float dz = first.Z - second.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool SameWorldAsHost()
{
    return Settings.AnyPartnerCounts || world_handoff::InHostWorld();
}

bool HostNear()
{
    if (Settings.AnyPartnerCounts)
    {
        return true;
    }
    const auto host = PeerSpots.find(0);
    return host != PeerSpots.end() && SameWorldAsHost() && LocalKnown && DistanceBetween(LocalSpot, host->second) <= VoterMeters;
}

uint8_t NeededVotes(size_t voterCount)
{
    return static_cast<uint8_t>(voterCount / 2 + 1);
}

std::string NameFor(uint32_t peer, net::RelaySession& session)
{
    return peer == session.GetStatus().MyPeerNumber ? std::string("You") : partner_roster::NameOf(peer, session);
}

int FindRowByText(const std::vector<std::string>& rows, const std::string& text)
{
    if (text.empty())
    {
        return -1;
    }
    const auto found = std::find(rows.begin(), rows.end(), text);
    return found == rows.end() ? -1 : static_cast<int>(found - rows.begin());
}

bool WindowMatchesBallot(const net::DialogBallot& ballot)
{
    if (!Window.Open)
    {
        return false;
    }
    if (Window.Rows.size() == ballot.Options.size())
    {
        return true;
    }
    return std::any_of(ballot.Options.begin(), ballot.Options.end(),
                       [](const net::DialogOption& option) { return FindRowByText(Window.Rows, option.Text) >= 0; });
}

int8_t BallotOptionForRow(const net::DialogBallot& ballot, int32_t row)
{
    if (row < 0 || row >= static_cast<int32_t>(Window.Rows.size()))
    {
        return -1;
    }
    for (size_t index = 0; index < ballot.Options.size(); ++index)
    {
        if (ballot.Options[index].Text == Window.Rows[static_cast<size_t>(row)])
        {
            return static_cast<int8_t>(index);
        }
    }
    return Window.Rows.size() == ballot.Options.size() && row < static_cast<int32_t>(MostOptions) ? static_cast<int8_t>(row) : -1;
}

void StartApply(uint32_t voteId, int8_t option, const net::DialogBallot& ballot, uint64_t sharedTime, uint64_t nowMs)
{
    Applying = {};
    Applying.Active = true;
    Applying.VoteId = voteId;
    Applying.Option = option;
    Applying.OptionText = option >= 0 && static_cast<size_t>(option) < ballot.Options.size() ? ballot.Options[static_cast<size_t>(option)].Text : "";
    Applying.Title = ballot.Title;
    Applying.RowCount = ballot.Options.size();
    Applying.WindowWasOpen = WindowMatchesBallot(ballot);
    Applying.ApplyAtSessionMs = sharedTime;
    Applying.GiveUpAtMs = nowMs + ApplyGiveUpMs;
}

int RowForApply()
{
    const int byText = FindRowByText(Window.Rows, Applying.OptionText);
    if (byText >= 0)
    {
        return byText;
    }
    const bool sameWindow = Applying.WindowWasOpen || Applying.Title.empty() || Window.Title == Applying.Title;
    if (Window.Rows.size() == Applying.RowCount && sameWindow && Applying.Option >= 0)
    {
        return Applying.Option;
    }
    return -1;
}

std::string VoteSummary(const std::vector<net::DialogPeerVote>& votes, const net::DialogBallot& ballot, size_t voterCount, net::RelaySession& session)
{
    std::string summary = "Votes " + std::to_string(votes.size()) + "/" + std::to_string(voterCount);
    for (const auto& vote : votes)
    {
        if (vote.Option >= 0 && static_cast<size_t>(vote.Option) < ballot.Options.size())
        {
            summary += "  " + NameFor(vote.Peer, session) + ": " + Quoted(ballot.Options[static_cast<size_t>(vote.Option)].Text);
        }
    }
    return summary;
}

net::DialogTally TallyOf(const HostVote& vote, int8_t option, uint64_t sharedTime)
{
    net::DialogTally tally;
    tally.VoteId = vote.Ballot.VoteId;
    tally.State = vote.State;
    tally.Option = option;
    tally.Needed = NeededVotes(vote.Ballot.Voters.size());
    for (uint32_t voter : vote.Ballot.Voters)
    {
        const auto cast = vote.Votes.find(voter);
        if (cast != vote.Votes.end())
        {
            tally.Votes.push_back({voter, cast->second});
        }
    }
    tally.Voters = vote.Ballot.Voters;
    tally.SharedTime = sharedTime;
    return tally;
}

std::vector<uint32_t> PickVoters(uint32_t ourPeer)
{
    std::vector<uint32_t> voters = {ourPeer};
    for (const auto& [peer, spot] : PeerSpots)
    {
        const bool near = LocalKnown && DistanceBetween(LocalSpot, spot) <= VoterMeters;
        if (voters.size() < MostVoters && peer != ourPeer && (Settings.AnyPartnerCounts || (near && partner_roster::PeerInHostWorld(peer))))
        {
            voters.push_back(peer);
        }
    }
    return voters;
}

void OpenHostVote(const std::vector<uint32_t>& voters, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    Hosting = {};
    if (++VoteCounter == 0)
    {
        ++VoteCounter;
    }
    auto& ballot = Hosting.Ballot;
    ballot.VoteId = VoteCounter;
    ballot.Title = Window.Title;
    for (size_t row = 0; row < Window.Rows.size() && row < MostOptions; ++row)
    {
        ballot.Options.push_back({Window.Rows[row], Window.RowTypes[row]});
    }
    ballot.Flags = static_cast<uint8_t>((Window.Timed ? net::kBallotTimedByGame : 0) | (ballot.Options.size() == 1 ? net::kBallotOneOption : 0));
    ballot.Voters = voters;
    Hosting.WindowKey = Window.Key;
    Hosting.State = net::kTallyOpen;
    uint64_t timeout = VoteTimeoutMs;
    const uint64_t gameTimerMs = Window.Timed && Window.Seconds > 0.0f ? static_cast<uint64_t>(Window.Seconds * 1000.0f) : 0;
    if (gameTimerMs > GameTimerMarginMs * 2)
    {
        timeout = std::min(timeout, gameTimerMs - GameTimerMarginMs);
    }
    Hosting.DeadlineMs = nowMs + timeout;
    session.SendGameMessage(net::WriteDialogBallot(ballot));
    session.SendGameMessage(net::WriteDialogTally(TallyOf(Hosting, -1, 0)));
    std::string names;
    for (uint32_t voter : voters)
    {
        names += (names.empty() ? "" : ", ") + NameFor(voter, session);
    }
    log("dialogs: vote #" + std::to_string(ballot.VoteId) + " opened: " + Quoted(ballot.Title) + " with " + std::to_string(ballot.Options.size()) +
        " option(s), voters [" + names + "] (needed " + std::to_string(NeededVotes(voters.size())) + ")" + (Window.Timed ? ", timed by the game" : ""));
    hud::Show("Choice vote with your partners: arrows and F to vote, pick the same option to go on", 5.0f);
}

void Decide(int8_t option, uint8_t state, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    Hosting.State = state;
    const uint64_t sharedTime = session.SessionClockMs() + ApplyLeadMs;
    session.SendGameMessage(net::WriteDialogTally(TallyOf(Hosting, option, sharedTime)));
    StartApply(Hosting.Ballot.VoteId, option, Hosting.Ballot, sharedTime, nowMs);
    WaitForWindowToClose = true;
    const std::string optionText = Applying.OptionText;
    log("dialogs: vote #" + std::to_string(Hosting.Ballot.VoteId) + ": option " + std::to_string(option) + " " + Quoted(optionText) + " goes for evryone at " +
        std::to_string(sharedTime) + (state == net::kTallyDecidedByTime ? " (decided by time)" : ""));
    hud::Show("Decided: " + Quoted(optionText), 3.0f);
}

void TryDecide(bool byTime, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    std::map<int8_t, size_t> counts;
    for (const auto& [voter, option] : Hosting.Votes)
    {
        if (option >= 0)
        {
            ++counts[option];
        }
    }
    const size_t needed = NeededVotes(Hosting.Ballot.Voters.size());
    for (const auto& [option, count] : counts)
    {
        if (count >= needed)
        {
            Decide(option, net::kTallyDecided, nowMs, session, log);
            return;
        }
    }
    if (!byTime)
    {
        return;
    }
    int8_t best = -1;
    size_t bestCount = 0;
    const auto hostVote = Hosting.Votes.find(0);
    for (const auto& [option, count] : counts)
    {
        const bool hostsPick = hostVote != Hosting.Votes.end() && hostVote->second == option;
        if (count > bestCount || (count == bestCount && hostsPick))
        {
            best = option;
            bestCount = count;
        }
    }
    if (best < 0)
    {
        best = static_cast<int8_t>(std::clamp<int32_t>(Window.Selected, 0, static_cast<int32_t>(Hosting.Ballot.Options.size()) - 1));
    }
    Decide(best, net::kTallyDecidedByTime, nowMs, session, log);
}

void CountHostVote(uint32_t peer, int8_t option, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    if (Hosting.State != net::kTallyOpen ||
        std::find(Hosting.Ballot.Voters.begin(), Hosting.Ballot.Voters.end(), peer) == Hosting.Ballot.Voters.end())
    {
        return;
    }
    if (option < 0)
    {
        Hosting.Votes.erase(peer);
        log("dialogs: vote #" + std::to_string(Hosting.Ballot.VoteId) + ": " + NameFor(peer, session) + " took the vote back");
    }
    else
    {
        Hosting.Votes[peer] = option;
        log("dialogs: vote #" + std::to_string(Hosting.Ballot.VoteId) + ": " + NameFor(peer, session) + " votes for option " + std::to_string(option));
    }
    const auto tally = TallyOf(Hosting, -1, 0);
    session.SendGameMessage(net::WriteDialogTally(tally));
    hud::Show(VoteSummary(tally.Votes, Hosting.Ballot, Hosting.Ballot.Voters.size(), session), 4.0f);
    TryDecide(false, nowMs, session, log);
}

void CancelHostVote(const char* why, net::RelaySession& session, const LogFunction& log)
{
    Hosting.State = net::kTallyCancelled;
    Hosting.Votes.clear();
    session.SendGameMessage(net::WriteDialogTally(TallyOf(Hosting, -1, 0)));
    log("dialogs: vote #" + std::to_string(Hosting.Ballot.VoteId) + " cancelled, " + why);
}

void HostTick(unsigned presses, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    if (!Window.Open)
    {
        WaitForWindowToClose = false;
    }
    if (Hosting.State == net::kTallyOpen)
    {
        if (!Window.Open || Window.Key != Hosting.WindowKey)
        {
            CancelHostVote("the choice is gone", session, log);
        }
        else
        {
            const auto voters = PickVoters(0);
            if (voters.size() < 2)
            {
                CancelHostVote("no partner is a voter any more", session, log);
            }
            else if (voters != Hosting.Ballot.Voters)
            {
                log("dialogs: vote #" + std::to_string(Hosting.Ballot.VoteId) + ": voters now " + std::to_string(voters.size()) + " (was " +
                    std::to_string(Hosting.Ballot.Voters.size()) + ")");
                Hosting.Ballot.Voters = voters;
                std::erase_if(Hosting.Votes, [&voters](const auto& vote) { return std::find(voters.begin(), voters.end(), vote.first) == voters.end(); });
                session.SendGameMessage(net::WriteDialogTally(TallyOf(Hosting, -1, 0)));
                TryDecide(false, nowMs, session, log);
            }
        }
    }
    if (Hosting.State == net::kTallyOpen && presses > 0 && Window.Selected >= 0 && Window.Selected < static_cast<int32_t>(Hosting.Ballot.Options.size()))
    {
        CountHostVote(0, static_cast<int8_t>(Window.Selected), nowMs, session, log);
    }
    if (Hosting.State == net::kTallyOpen && nowMs >= Hosting.DeadlineMs)
    {
        TryDecide(true, nowMs, session, log);
    }
    if (Hosting.State != net::kTallyOpen && Settings.Votes && Window.Open && !WaitForWindowToClose && !Applying.Active)
    {
        const auto voters = PickVoters(0);
        if (voters.size() >= 2)
        {
            OpenHostVote(voters, nowMs, session, log);
        }
    }
    for (const auto& [peer, spot] : PeerSpots)
    {
        if (VoteModeSentTo.insert(peer).second)
        {
            session.SendGameMessage(net::WriteVoteModeWord({1, net::kVoteModeMajority, 0}), static_cast<int32_t>(peer));
        }
    }
}

void SendGuestVote(int32_t row, net::RelaySession& session, const LogFunction& log)
{
    const int8_t option = BallotOptionForRow(Joined.Ballot, row);
    if (option < 0)
    {
        hud::Show("This option is not in the host's choice", 3.0f);
        return;
    }
    const auto optionIndex = static_cast<size_t>(option);
    const uint32_t item = optionIndex < Joined.Ballot.RowIds.size() ? Joined.Ballot.RowIds[optionIndex].Item : 0;
    session.SendGameMessage(net::WriteDialogVote({Joined.Ballot.VoteId, option, item}), 0);
    log("dialogs: we vote for option " + std::to_string(option) + " in vote #" + std::to_string(Joined.Ballot.VoteId));
    hud::Show("You chose " + Quoted(Joined.Ballot.Options[static_cast<size_t>(option)].Text) + ", waiting for the others", 4.0f);
}

void GuestTick(unsigned presses, uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    const bool voteOpen = Joined.State == net::kTallyOpen;
    const bool windowMatches = voteOpen && WindowMatchesBallot(Joined.Ballot);
    if (!Window.Open)
    {
        Early = {};
    }
    if (presses > 0 && windowMatches)
    {
        SendGuestVote(Window.Selected, session, log);
    }
    else if (presses > 0 && Window.Open && !voteOpen && !Early.Released)
    {
        Early.Waiting = true;
        Early.Row = Window.Selected;
        Early.AtMs = nowMs;
        hud::Show("Waiting for the host's choice vote", 3.0f);
    }
    if (Early.Waiting && windowMatches)
    {
        Early.Waiting = false;
        SendGuestVote(Early.Row, session, log);
    }
    if (Early.Waiting && nowMs - Early.AtMs > EarlyPressWaitMs)
    {
        Early.Waiting = false;
        Early.Released = true;
        PressKey(KeyF, "f");
        log("dialogs: no vote from the host for our choice in " + std::to_string(EarlyPressWaitMs / 1000) + " s, our press goes thru");
        hud::Show("The host has no vote for this choice, your pick goes through", 4.0f);
    }
}

void ApplyTick(uint64_t nowMs, net::RelaySession& session, const LogFunction& log)
{
    if (!Applying.Active || session.SessionClockMs() < Applying.ApplyAtSessionMs)
    {
        return;
    }
    if (nowMs >= Applying.GiveUpAtMs)
    {
        log("dialogs: vote #" + std::to_string(Applying.VoteId) + ": the decided choice never showed up here");
        Applying.Active = false;
        return;
    }
    if (Applying.ApplyPresses > 0)
    {
        if (!Window.Open || Window.Key != Applying.PressedWindowKey)
        {
            log("dialogs: vote #" + std::to_string(Applying.VoteId) + ": option " + Quoted(Applying.OptionText) + " picked here (" +
                std::to_string(Applying.ArrowPresses) + " arrow press(es), " + std::to_string(Applying.ApplyPresses) + " apply press(es))");
            Applying.Active = false;
            return;
        }
        if (nowMs < Applying.NextStepMs)
        {
            return;
        }
        if (Applying.ApplyPresses >= MostApplyPresses)
        {
            log("dialogs: vote #" + std::to_string(Applying.VoteId) + ": the window stays after " + std::to_string(MostApplyPresses) + " presses, giving up");
            hud::Show("Could not pick " + Quoted(Applying.OptionText) + " for you, pick it yourself", 5.0f);
            Applying.Active = false;
            return;
        }
    }
    if (!Window.Open || nowMs < Applying.NextStepMs)
    {
        return;
    }
    const int targetRow = RowForApply();
    if (targetRow < 0)
    {
        log("dialogs: vote #" + std::to_string(Applying.VoteId) + ": our window does not have option " + Quoted(Applying.OptionText));
        hud::Show("You do not have the option the group chose (" + Quoted(Applying.OptionText) + ")", 5.0f);
        Applying.Active = false;
        return;
    }
    if (Window.Selected != targetRow)
    {
        if (Applying.ArrowPresses >= MostArrowPresses)
        {
            log("dialogs: vote #" + std::to_string(Applying.VoteId) + ": the selection did not follow the arrow keys, giving up");
            hud::Show("Could not pick " + Quoted(Applying.OptionText) + " for you, pick it yourself", 5.0f);
            Applying.Active = false;
            return;
        }
        ++Applying.ArrowPresses;
        const bool down = targetRow > Window.Selected;
        PressKey(down ? KeyDownArrow : KeyUpArrow, down ? DownArrowCharacter : UpArrowCharacter);
        Applying.NextStepMs = nowMs + ArrowStepMs;
        return;
    }
    if (++Applying.ApplyPresses == 1)
    {
        const int64_t late = static_cast<int64_t>(session.SessionClockMs()) - static_cast<int64_t>(Applying.ApplyAtSessionMs);
        log("dialogs: vote #" + std::to_string(Applying.VoteId) + ": pressing row " + std::to_string(targetRow) + " " + Quoted(Applying.OptionText) + " (" +
            (late >= 0 ? "+" : "") + std::to_string(late) + " ms against the shared time)");
    }
    if (Applying.ApplyPresses == 2)
    {
        PressKey(KeyReturn, "\r");
    }
    else
    {
        PressKey(KeyF, "f");
    }
    Applying.PressedWindowKey = Window.Key;
    Applying.NextStepMs = nowMs + PressCheckMs;
}

void LinesTick(uint64_t nowMs, bool weAreHost, RED4ext::Handle<RED4ext::IScriptable>& service, net::RelaySession& session)
{
    void* instance = service.instance;
    const int32_t seen = *Game.OwnLinesSeen->GetValuePtr<int32_t>(instance);
    OurLanguage = Game.Language->GetValuePtr<RED4ext::CString>(instance)->c_str();
    const auto* ids = Game.LineIds->GetValuePtr<RED4ext::DynArray<RED4ext::CRUID>>(instance);
    const auto* types = Game.LineTypes->GetValuePtr<RED4ext::DynArray<int32_t>>(instance);
    const auto* seconds = Game.LineSeconds->GetValuePtr<RED4ext::DynArray<float>>(instance);
    const auto* speakers = Game.LineSpeakers->GetValuePtr<RED4ext::DynArray<RED4ext::CString>>(instance);
    const auto* texts = Game.LineTexts->GetValuePtr<RED4ext::DynArray<RED4ext::CString>>(instance);
    if (seen != OwnLinesSeen)
    {
        OwnLinesSeen = seen;
        LastOwnLineMs = nowMs;
    }
    const uint32_t lineCount = std::min({ids->Size(), types->Size(), seconds->Size(), speakers->Size(), texts->Size()});
    for (uint32_t index = 0; index < lineCount; ++index)
    {
        const auto lineId = static_cast<uint64_t>((*ids)[index].unk00);
        LastOwnLineId = lineId ? lineId : LastOwnLineId;
        const int32_t lineType = (*types)[index];
        if (!weAreHost || PeerSpots.empty() || lineId == 0 || lineType < 0 || lineType > 13 || !((RelayedLineTypes >> lineType) & 1))
        {
            continue;
        }
        net::DialogLine line;
        line.LineId = lineId;
        line.LineType = static_cast<uint8_t>(lineType);
        line.Seconds = (*seconds)[index];
        line.Speaker = (*speakers)[index].c_str();
        line.Text = (*texts)[index].c_str();
        line.Language = OurLanguage;
        session.SendGameMessage(net::WriteDialogLine(line));
    }
    if (ids->Size() > 0)
    {
        RED4ext::StackArgs_t noArgs;
        CallService(service, Game.ClearLines, noArgs);
    }
    for (auto relayed = RelayedLines.begin(); relayed != RelayedLines.end();)
    {
        if (nowMs < relayed->HideAtMs)
        {
            ++relayed;
            continue;
        }
        RED4ext::CRUID id {static_cast<int64_t>(relayed->Id)};
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &id);
        CallService(service, Game.HideRelayedLine, args);
        relayed = RelayedLines.erase(relayed);
    }
}

void SendSkip(uint8_t flags, uint32_t milliseconds, net::RelaySession& session, const LogFunction& log)
{
    net::SceneLineSkip skip;
    skip.Peer = session.GetStatus().MyPeerNumber;
    skip.SceneId = UnknownSceneId;
    skip.Milliseconds = milliseconds;
    skip.SharedTime = session.SessionClockMs();
    skip.Flags = flags;
    skip.Places.push_back({1, LastOwnLineId ? LastOwnLineId : 1, 1});
    if (skip.SharedTime == 0)
    {
        return;
    }
    session.SendGameMessage(net::WriteSceneLineSkip(skip));
    log(flags & net::kSkipPress ? "dialogs: our scene fast forward started, partners skip along" : "dialogs: our scene fast forward ended after " +
                                                                                                        std::to_string(milliseconds) + " ms");
}

void SkipTick(uint64_t nowMs, RED4ext::Handle<RED4ext::IScriptable>& service, net::RelaySession& session, const LogFunction& log)
{
    const bool active = *Game.SkipActive->GetValuePtr<bool>(service.instance);
    if (PartnerSkipRunning)
    {
        if (Window.Open || nowMs >= PartnerSkipUntilMs)
        {
            RED4ext::StackArgs_t noArgs;
            CallService(service, Game.StopSkip, noArgs);
            PartnerSkipRunning = false;
            IgnoreOurSkipUntilMs = nowMs + 1000;
            log(Window.Open ? "dialogs: the partners fast forward stops at our choice window" : "dialogs: the partners fast forward ran too long, stopped");
        }
        return;
    }
    if (nowMs < IgnoreOurSkipUntilMs)
    {
        return;
    }
    if (active && !OurSkipRunning && PeerSpots.size() > 0)
    {
        OurSkipRunning = true;
        OurSkipStartedMs = nowMs;
        SendSkip(net::kSkipPress, 0, session, log);
    }
    else if (!active && OurSkipRunning)
    {
        OurSkipRunning = false;
        SendSkip(0, static_cast<uint32_t>(std::min<uint64_t>(nowMs - OurSkipStartedMs, 100000)), session, log);
    }
}

int32_t ReadSceneTier()
{
    static auto* getPlayer = RED4ext::CRTTISystem::Get()->GetFunction("GetPlayer;GameInstance");
    RED4ext::ScriptGameInstance gameInstance;
    RED4ext::Handle<RED4ext::IScriptable> player;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &gameInstance);
    int32_t tier = 0;
    if (getPlayer && game::CallStatic(getPlayer, &player, args) && player)
    {
        state_machine_board::ReadInt(player, "SceneTier", tier);
    }
    return tier;
}

void BodyTick(uint64_t nowMs, const std::vector<RemotePeerView>& peers, RemoteBodies& bodies, RED4ext::Handle<RED4ext::IScriptable>& service,
              const LogFunction& log)
{
    const int32_t tier = ReadSceneTier();
    if (tier != SceneTier)
    {
        log("dialogs: our scene tier is " + std::to_string(tier) + " now (was " + std::to_string(SceneTier) + ")");
        SceneTier = tier;
    }
    const bool scenePlacesUs = tier >= FirstPlacingSceneTier && tier <= LastSceneTier;
    std::set<uint32_t> seen;
    for (const auto& view : peers)
    {
        seen.insert(view.PeerNumber);
        auto& hiding = BodyStates[view.PeerNumber];
        const float distance = DistanceBetween(LocalSpot, {view.State.PositionX, view.State.PositionY, view.State.PositionZ});
        if (distance < HideInsideMeters)
        {
            hiding.Overlapping = true;
        }
        else if (distance > ShowPastMeters)
        {
            hiding.Overlapping = false;
        }
        const bool sceneHides = scenePlacesUs && distance <= SceneHideMeters;
        const bool wantHidden = hiding.Overlapping || sceneHides;
        RED4ext::Handle<RED4ext::IScriptable> body;
        if (!bodies.GetBody(view.PeerNumber, body) || !body)
        {
            hiding.Hidden = false;
            hiding.Instance = nullptr;
            continue;
        }
        const bool bodyChanged = body.instance != hiding.Instance;
        if (wantHidden == hiding.Hidden && !bodyChanged && (!wantHidden || nowMs < hiding.NextRefreshMs))
        {
            continue;
        }
        bool hide = wantHidden;
        int32_t partsTurned = 0;
        RED4ext::StackArgs_t args;
        args.emplace_back(nullptr, &body);
        args.emplace_back(nullptr, &hide);
        CallService(service, Game.SetBodyHidden, args, &partsTurned);
        if (wantHidden != hiding.Hidden)
        {
            log("dialogs: peer " + std::to_string(view.PeerNumber) + " body " +
                (wantHidden ? std::string("hidden (") + (sceneHides ? "scene tier " + std::to_string(tier) : "inside our player") + ")" : "shown again") + ", " +
                std::to_string(partsTurned) + " part(s) turned " + (wantHidden ? "off" : "on"));
        }
        hiding.Hidden = wantHidden;
        hiding.Instance = body.instance;
        hiding.NextRefreshMs = nowMs + HiddenRefreshEveryMs;
    }
    std::erase_if(BodyStates, [&seen](const auto& entry) { return !seen.count(entry.first); });
}

void ReleaseKeys()
{
    if (HoldingKeys)
    {
        HoldKeyPresses(KeyF, false);
        HoldKeyPresses(KeyReturn, false);
        HoldingKeys = false;
    }
}

void Forget()
{
    ReleaseKeys();
    Window = {};
    Hosting = {};
    Joined = {};
    Applying = {};
    Early = {};
    WaitForWindowToClose = false;
    BodyStates.clear();
    RelayedLines.clear();
    PeerSpots.clear();
    VoteModeSentTo.clear();
    OurSkipRunning = false;
    PartnerSkipRunning = false;
    LocalKnown = false;
}

void OnBallot(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    net::DialogBallot ballot;
    if (fromPeer != 0 || session.GetStatus().MyPeerNumber == 0 || !Settings.Votes || !SameWorldAsHost() || !net::ReadDialogBallot(message, ballot))
    {
        return;
    }
    Joined = {};
    Joined.Ballot = ballot;
    Joined.State = net::kTallyOpen;
    log("dialogs: the host opened vote #" + std::to_string(ballot.VoteId) + ": " + Quoted(ballot.Title) + " with " + std::to_string(ballot.Options.size()) +
        " option(s), " + std::to_string(ballot.Voters.size()) + " voter(s)" + (WindowMatchesBallot(ballot) ? ", our window matches" : ", our window is not open"));
    hud::Show(WindowMatchesBallot(ballot) ? "Choice vote with the host: arrows and F to vote, pick the same option to go on"
                                          : "The host is at a choice, go to the NPC to vote",
              5.0f);
}

void OnTally(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    net::DialogTally tally;
    if (fromPeer != 0 || session.GetStatus().MyPeerNumber == 0 || !Settings.Votes || !SameWorldAsHost() || !net::ReadDialogTally(message, tally))
    {
        return;
    }
    if (tally.VoteId != Joined.Ballot.VoteId)
    {
        log("dialogs: a tally for vote #" + std::to_string(tally.VoteId) + " whose ballot we never got, skipped");
        return;
    }
    const bool votesChanged = tally.Votes.size() != Joined.Tally.Votes.size() ||
                              !std::equal(tally.Votes.begin(), tally.Votes.end(), Joined.Tally.Votes.begin(),
                                          [](const auto& first, const auto& second) { return first.Peer == second.Peer && first.Option == second.Option; });
    Joined.Tally = tally;
    Joined.State = tally.State;
    if (tally.State == net::kTallyOpen && votesChanged)
    {
        hud::Show(VoteSummary(tally.Votes, Joined.Ballot, tally.Voters.size(), session), 4.0f);
    }
    else if (tally.State == net::kTallyCancelled)
    {
        log("dialogs: the host cancelled vote #" + std::to_string(tally.VoteId));
    }
    else if ((tally.State == net::kTallyDecided || tally.State == net::kTallyDecidedByTime) &&
             (!Applying.Active || Applying.VoteId != tally.VoteId))
    {
        StartApply(tally.VoteId, tally.Option, Joined.Ballot, tally.SharedTime, LastTickMs);
        log("dialogs: vote #" + std::to_string(tally.VoteId) + ": option " + std::to_string(tally.Option) + " " + Quoted(Applying.OptionText) +
            " goes at the shared time " + std::to_string(tally.SharedTime) + " (in " +
            std::to_string(static_cast<int64_t>(tally.SharedTime) - static_cast<int64_t>(session.SessionClockMs())) + " ms)" +
            (tally.State == net::kTallyDecidedByTime ? ", decided by time" : ""));
        hud::Show("Decided: " + Quoted(Applying.OptionText), 3.0f);
    }
}

void OnLine(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    net::DialogLine line;
    if (fromPeer != 0 || session.GetStatus().MyPeerNumber == 0 || !SameWorldAsHost() || !net::ReadDialogLine(message, line))
    {
        return;
    }
    const auto host = PeerSpots.find(0);
    const bool hostClose = Settings.AnyPartnerCounts || (host != PeerSpots.end() && LocalKnown && DistanceBetween(LocalSpot, host->second) <= SubtitleMeters);
    const bool ourSceneQuiet = LastTickMs - LastOwnLineMs > OwnSceneQuietMs;
    auto service = game::ScriptService("NightCoopDialogs");
    if (!hostClose || !ourSceneQuiet || !service || !Game.Usable)
    {
        return;
    }
    const uint64_t relayedId = line.LineId ^ RelayedLineMarker;
    RED4ext::CRUID id {static_cast<int64_t>(relayedId)};
    RED4ext::CString speaker(line.Speaker.c_str());
    RED4ext::CString text(line.Text.c_str());
    int32_t lineType = line.LineType == RegularLineType || line.LineType == HolocallLineType ? OwnerlessRegularLineType : line.LineType;
    float seconds = line.Seconds > 0.0f ? line.Seconds : 4.0f;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &id);
    args.emplace_back(nullptr, &speaker);
    args.emplace_back(nullptr, &text);
    args.emplace_back(nullptr, &lineType);
    args.emplace_back(nullptr, &seconds);
    CallService(service, Game.ShowRelayedLine, args);
    RelayedLines.push_back({relayedId, LastTickMs + static_cast<uint64_t>(seconds * 1000.0f) + 300});
    log("dialogs: our scene is quiet, showing the hosts line (" + line.Speaker + ", " + std::to_string(line.Text.size()) + " bytes)");
}

void OnSkip(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    net::SceneLineSkip skip;
    if (!net::ReadSceneLineSkip(message, skip) || skip.Peer == session.GetStatus().MyPeerNumber || !Game.Usable)
    {
        return;
    }
    auto service = game::ScriptService("NightCoopDialogs");
    if (!service)
    {
        return;
    }
    RED4ext::StackArgs_t noArgs;
    if (skip.Flags & net::kSkipPress)
    {
        bool started = false;
        if (!Window.Open && !OurSkipRunning)
        {
            CallService(service, Game.StartSkip, noArgs, &started);
        }
        PartnerSkipRunning = started;
        PartnerSkipUntilMs = LastTickMs + PartnerSkipLongestMs;
        log("dialogs: " + partner_roster::NameOf(skip.Peer, session) + " skips the scene, " +
            (started ? "we fast forward along" : "our scene does not let us fast forward here"));
        if (started)
        {
            hud::Show("Fast forward with " + partner_roster::NameOf(skip.Peer, session), 2.0f);
        }
    }
    else if (PartnerSkipRunning)
    {
        CallService(service, Game.StopSkip, noArgs);
        PartnerSkipRunning = false;
        IgnoreOurSkipUntilMs = LastTickMs + 1000;
        log("dialogs: " + partner_roster::NameOf(skip.Peer, session) + " stopped skipping after " + std::to_string(skip.Milliseconds) + " ms, so do we");
    }
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::DialogBallot) || messageType == static_cast<uint8_t>(nc::proto::MsgType::DialogVote) ||
           messageType == static_cast<uint8_t>(nc::proto::MsgType::DialogTally) || messageType == static_cast<uint8_t>(nc::proto::MsgType::DialogLine) ||
           messageType == static_cast<uint8_t>(nc::proto::MsgType::SceneLineSkip) || messageType == net::kVoteModeWordType;
}

void Tick(uint64_t nowMs, bool inSave, const DialogSettings& settings, const net::PlayerState* localPlayer, const std::vector<RemotePeerView>& peers,
          RemoteBodies& bodies, net::RelaySession& session, const LogFunction& log)
{
    LastTickMs = nowMs;
    Settings = settings;
    const bool running = inSave && localPlayer && session.GetStatus().InSession;
    if (!running || !Resolve(log))
    {
        if (WasRunning)
        {
            Forget();
        }
        WasRunning = false;
        return;
    }
    WasRunning = true;
    auto service = game::ScriptService("NightCoopDialogs");
    if (!service)
    {
        ReleaseKeys();
        return;
    }
    WatchKeys();
    LocalKnown = true;
    LocalSpot = {localPlayer->PositionX, localPlayer->PositionY, localPlayer->PositionZ};
    PeerSpots.clear();
    for (const auto& view : peers)
    {
        PeerSpots[view.PeerNumber] = {view.State.PositionX, view.State.PositionY, view.State.PositionZ};
    }
    if (nowMs >= NextBodyTickMs)
    {
        NextBodyTickMs = nowMs + BodyTickEveryMs;
        BodyTick(nowMs, peers, bodies, service, log);
    }
    if (nowMs < NextDialogTickMs)
    {
        return;
    }
    NextDialogTickMs = nowMs + DialogTickEveryMs;
    RED4ext::StackArgs_t noArgs;
    CallService(service, Game.Refresh, noArgs);
    ReadWindow(service);
    const bool weAreHost = session.GetStatus().MyPeerNumber == 0;
    LinesTick(nowMs, weAreHost, service, session);
    const unsigned presses = HoldingKeys ? TakeHeldPresses(KeyF) + TakeHeldPresses(KeyReturn) : 0;
    if (weAreHost)
    {
        HostTick(Applying.Active ? 0 : presses, nowMs, session, log);
    }
    else
    {
        GuestTick(Applying.Active ? 0 : presses, nowMs, session, log);
    }
    ApplyTick(nowMs, session, log);
    SkipTick(nowMs, service, session, log);
    const bool hostHolds = weAreHost && Hosting.State == net::kTallyOpen && Window.Open && Window.Key == Hosting.WindowKey;
    const bool guestHolds = !weAreHost && Window.Open &&
                            ((Joined.State == net::kTallyOpen && WindowMatchesBallot(Joined.Ballot)) ||
                             (Joined.State != net::kTallyOpen && HostNear() && !Early.Released));
    const bool hold = Settings.Votes && Window.Open && (hostHolds || guestHolds || Applying.Active);
    if (hold != HoldingKeys)
    {
        HoldKeyPresses(KeyF, hold);
        HoldKeyPresses(KeyReturn, hold);
        HoldingKeys = hold;
        TakeHeldPresses(KeyF);
        TakeHeldPresses(KeyReturn);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    const uint8_t type = net::MessageTypeOf(message);
    if (type == static_cast<uint8_t>(nc::proto::MsgType::DialogVote))
    {
        net::DialogVote vote;
        if (session.GetStatus().MyPeerNumber == 0 && net::ReadDialogVote(message, vote) && vote.VoteId == Hosting.Ballot.VoteId)
        {
            CountHostVote(fromPeer, vote.Option, LastTickMs, session, log);
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::DialogBallot))
    {
        OnBallot(fromPeer, message, session, log);
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::DialogTally))
    {
        OnTally(fromPeer, message, session, log);
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::DialogLine))
    {
        OnLine(fromPeer, message, session, log);
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::SceneLineSkip))
    {
        OnSkip(fromPeer, message, session, log);
    }
    else if (type == net::kVoteModeWordType)
    {
        net::VoteModeWord word;
        if (fromPeer == 0 && net::ReadVoteModeWord(message, word))
        {
            log(std::string("dialogs: the host decides choices by ") + (word.Mode == net::kVoteModeEveryone ? "evryone" : "majority"));
        }
    }
}
}
