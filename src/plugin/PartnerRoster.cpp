#include "plugin/PartnerRoster.hpp"

#include "net/GameMessages.hpp"
#include "net/RelaySession.hpp"
#include "net/RosterMessages.hpp"

#include <ProtocolCore.hpp>

#include <map>

namespace nightcoop_mac::partner_roster
{
namespace
{
constexpr uint64_t SendEveryMs = 2000;
constexpr uint32_t MostNamesLogged = 16;

std::map<uint32_t, net::PlayerPresence> PresenceByPeer;
net::SessionRoster RosterFromHost;
bool HaveRoster = false;
std::vector<uint8_t> LastSentPresence;
std::vector<uint8_t> LastSentRoster;
uint32_t RosterRevision = 0;
uint64_t NextSendMs = 0;
std::map<uint32_t, std::string> LoggedNames;
uint32_t NamesLogged = 0;

std::string FallbackName(uint32_t peerNumber)
{
    return "Player " + std::to_string(peerNumber);
}

void NoteName(uint32_t peerNumber, const std::string& name, const LogFunction& log)
{
    auto& logged = LoggedNames[peerNumber];
    if (logged != name && NamesLogged++ < MostNamesLogged)
    {
        log("roster: peer " + std::to_string(peerNumber) + " is '" + name + "'");
    }
    logged = name;
}

void SendPresence(uint64_t nowMs, bool inWorld, const std::string& ourName, net::RelaySession& session)
{
    net::PlayerPresence presence;
    presence.Name = ourName;
    presence.Where = inWorld ? net::kWhereInWorld : net::kWhereMenu;
    presence.Flags = inWorld ? net::kPresenceInHostWorld : 0;
    const auto message = net::WritePlayerPresence(presence);
    if (message == LastSentPresence && nowMs < NextSendMs)
    {
        return;
    }
    session.SendGameMessage(message, 0);
    LastSentPresence = message;
    NextSendMs = nowMs + SendEveryMs;
}

void SendRoster(uint64_t nowMs, bool inWorld, const std::string& ourName, net::RelaySession& session)
{
    net::SessionRoster roster;
    roster.HostState = inWorld ? net::kHostStateInWorld : 1;
    net::RosterEntry ourEntry;
    ourEntry.PeerNumber = 0;
    ourEntry.Name = ourName;
    ourEntry.Where = inWorld ? net::kWhereInWorld : net::kWhereMenu;
    ourEntry.PingMs = 0;
    ourEntry.Role = net::kRoleHost;
    ourEntry.Flags = inWorld ? net::kPresenceInHostWorld : 0;
    roster.Entries.push_back(ourEntry);
    for (const auto& [peerNumber, nickname] : session.PeerNicknames())
    {
        net::RosterEntry entry;
        entry.PeerNumber = peerNumber;
        entry.Name = nickname;
        const auto presence = PresenceByPeer.find(peerNumber);
        if (presence != PresenceByPeer.end())
        {
            entry.Name = presence->second.Name.empty() ? nickname : presence->second.Name;
            entry.Where = presence->second.Where;
            entry.PingMs = presence->second.PingMs;
            entry.Flags = presence->second.Flags;
            entry.Stage = presence->second.Stage;
            entry.PauseBits = presence->second.PauseBits;
            entry.SaveName = presence->second.SaveName;
            entry.MissingAddons = presence->second.MissingAddons;
            entry.Language = presence->second.Language;
        }
        roster.Entries.push_back(std::move(entry));
    }
    roster.Revision = RosterRevision;
    auto message = net::WriteSessionRoster(roster);
    if (message != LastSentRoster)
    {
        roster.Revision = ++RosterRevision;
        message = net::WriteSessionRoster(roster);
    }
    else if (nowMs < NextSendMs)
    {
        return;
    }
    session.SendGameMessage(message);
    LastSentRoster = message;
    NextSendMs = nowMs + SendEveryMs;
}
}

void Tick(uint64_t nowMs, bool inWorld, const std::string& ourName, net::RelaySession& session, const LogFunction& log)
{
    const auto status = session.GetStatus();
    if (!status.InSession)
    {
        return;
    }
    if (status.MyPeerNumber == 0)
    {
        SendRoster(nowMs, inWorld, ourName, session);
        for (const auto& [peerNumber, nickname] : session.PeerNicknames())
        {
            NoteName(peerNumber, NameOf(peerNumber, session), log);
        }
    }
    else
    {
        SendPresence(nowMs, inWorld, ourName, session);
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log)
{
    const uint8_t type = net::MessageTypeOf(message);
    const bool weAreHost = session.GetStatus().MyPeerNumber == 0;
    if (type == static_cast<uint8_t>(nc::proto::MsgType::PlayerPresence) && weAreHost && fromPeer != 0)
    {
        net::PlayerPresence presence;
        if (net::ReadPlayerPresence(message, presence))
        {
            PresenceByPeer[fromPeer] = presence;
        }
    }
    else if (type == static_cast<uint8_t>(nc::proto::MsgType::SessionRoster) && !weAreHost && fromPeer == 0)
    {
        net::SessionRoster roster;
        if (net::ReadSessionRoster(message, roster))
        {
            RosterFromHost = roster;
            HaveRoster = true;
            for (const auto& entry : roster.Entries)
            {
                NoteName(entry.PeerNumber, entry.Name.empty() ? FallbackName(entry.PeerNumber) : entry.Name, log);
            }
        }
    }
}

std::string NameOf(uint32_t peerNumber, net::RelaySession& session)
{
    if (HaveRoster)
    {
        for (const auto& entry : RosterFromHost.Entries)
        {
            if (entry.PeerNumber == peerNumber && !entry.Name.empty())
            {
                return entry.Name;
            }
        }
    }
    const auto presence = PresenceByPeer.find(peerNumber);
    if (presence != PresenceByPeer.end() && !presence->second.Name.empty())
    {
        return presence->second.Name;
    }
    const auto nicknames = session.PeerNicknames();
    const auto nickname = nicknames.find(peerNumber);
    if (nickname != nicknames.end() && !nickname->second.empty())
    {
        return nickname->second;
    }
    return FallbackName(peerNumber);
}

bool InOurWorld(uint32_t peerNumber)
{
    if (HaveRoster)
    {
        for (const auto& entry : RosterFromHost.Entries)
        {
            if (entry.PeerNumber == peerNumber)
            {
                return entry.Where == net::kWhereInWorld || entry.Where == net::kWhereUnknown;
            }
        }
    }
    const auto presence = PresenceByPeer.find(peerNumber);
    return presence == PresenceByPeer.end() || presence->second.Where == net::kWhereInWorld || presence->second.Where == net::kWhereUnknown;
}

void Forget()
{
    PresenceByPeer.clear();
    RosterFromHost = net::SessionRoster {};
    HaveRoster = false;
    LastSentPresence.clear();
    LastSentRoster.clear();
    NextSendMs = 0;
}
}
