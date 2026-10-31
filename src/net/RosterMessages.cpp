#include "net/RosterMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>

namespace nightcoop_mac::net
{
namespace
{
constexpr size_t MaxNameBytes = 48;
constexpr size_t MaxLobbyTextBytes = 64;
constexpr size_t MaxSaveNameBytes = 160;
constexpr size_t MaxShortTextBytes = 32;
constexpr size_t MaxLanguageBytes = 8;
constexpr size_t MaxListEntries = 8;
constexpr size_t MaxRosterEntries = 8;

std::string Cut(const std::string& text, size_t limit)
{
    if (text.size() <= limit)
    {
        return text;
    }
    size_t end = limit;
    while (end > 0 && (static_cast<uint8_t>(text[end]) & 0xC0) == 0x80)
    {
        --end;
    }
    return text.substr(0, end);
}

void PutList(nc::proto::Writer& writer, const std::vector<std::string>& list)
{
    const auto count = static_cast<uint8_t>(std::min(list.size(), MaxListEntries));
    writer.PutU8(count);
    for (uint8_t index = 0; index < count; ++index)
    {
        writer.PutString(Cut(list[index], MaxShortTextBytes));
    }
}

bool GetText(nc::proto::Reader& reader, size_t limit, std::string& text)
{
    text = reader.GetString();
    return reader.Ok() && text.size() <= limit;
}

bool GetList(nc::proto::Reader& reader, std::vector<std::string>& list)
{
    const uint8_t count = reader.GetU8();
    if (!reader.Ok() || count > MaxListEntries)
    {
        return false;
    }
    list.clear();
    for (uint8_t index = 0; index < count; ++index)
    {
        std::string text;
        if (!GetText(reader, MaxShortTextBytes, text))
        {
            return false;
        }
        list.push_back(std::move(text));
    }
    return true;
}

bool OpenMessage(nc::proto::Reader& reader, nc::proto::MsgType expectedType)
{
    return reader.ReadHeader() == expectedType && reader.Ok();
}
}

std::vector<uint8_t> WritePlayerPresence(const PlayerPresence& presence)
{
    nc::proto::Writer writer(nc::proto::MsgType::PlayerPresence);
    writer.PutString(Cut(presence.Name, MaxNameBytes));
    writer.PutU8(presence.Where < 4 ? presence.Where : kWhereUnknown);
    writer.PutU16(presence.PingMs);
    writer.PutU8(presence.Flags);
    writer.PutU8(presence.Stage == 3 ? 2 : std::min<uint8_t>(presence.Stage, 4));
    writer.PutU8(presence.PauseBits & 0x07);
    writer.PutString(Cut(presence.LobbyText, MaxLobbyTextBytes));
    writer.PutString(Cut(presence.SaveName, MaxSaveNameBytes));
    writer.PutString(Cut(presence.OtherText, MaxShortTextBytes));
    PutList(writer, presence.MissingAddons);
    writer.PutString(Cut(presence.Language, MaxLanguageBytes));
    return writer.Data();
}

bool ReadPlayerPresence(const std::vector<uint8_t>& message, PlayerPresence& presence)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::PlayerPresence) || !GetText(reader, MaxNameBytes, presence.Name))
    {
        return false;
    }
    presence.Where = reader.GetU8();
    presence.PingMs = reader.GetU16();
    presence.Flags = reader.GetU8();
    presence.Stage = reader.GetU8();
    presence.PauseBits = reader.GetU8();
    if (!reader.Ok() || presence.Where >= 4 || presence.Stage >= 5 || presence.Stage == 3 || (presence.PauseBits & 0xF8) != 0)
    {
        return false;
    }
    return GetText(reader, MaxLobbyTextBytes, presence.LobbyText) && GetText(reader, MaxSaveNameBytes, presence.SaveName) &&
           GetText(reader, MaxShortTextBytes, presence.OtherText) && GetList(reader, presence.MissingAddons) &&
           GetText(reader, MaxLanguageBytes, presence.Language) && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteSessionRoster(const SessionRoster& roster)
{
    nc::proto::Writer writer(nc::proto::MsgType::SessionRoster);
    writer.PutU32(roster.Revision);
    writer.PutU8(std::clamp<uint8_t>(roster.HostState, 1, 5));
    writer.PutU32(roster.HostValue);
    writer.PutString(Cut(roster.WorldName, MaxSaveNameBytes));
    writer.PutString(Cut(roster.HostText, MaxShortTextBytes));
    PutList(writer, roster.HostList);
    writer.PutU8(roster.PauseFlags & 0x03);
    writer.PutU8(roster.SharedNewGame ? 1 : 0);
    writer.PutU8(std::min<uint8_t>(roster.Difficulty, 3));
    writer.PutU8((roster.NewGameFlags & 0x03) == 1 ? 0 : roster.NewGameFlags & 0x07);
    writer.PutU8(std::min<uint8_t>(roster.HostByte, 4));
    writer.PutU64(roster.HostTime);
    const auto count = static_cast<uint8_t>(std::min(roster.Entries.size(), MaxRosterEntries));
    writer.PutU8(count);
    for (uint8_t index = 0; index < count; ++index)
    {
        const auto& entry = roster.Entries[index];
        writer.PutU32(entry.PeerNumber);
        writer.PutString(Cut(entry.Name, MaxNameBytes));
        writer.PutU8(entry.Where < 4 ? entry.Where : kWhereUnknown);
        writer.PutU16(entry.PingMs);
        writer.PutU8(std::min<uint8_t>(entry.Role, 5));
        writer.PutU8(entry.Flags);
        writer.PutU8(std::min<uint8_t>(entry.Stage, 4));
        writer.PutU8(entry.PauseBits & 0x07);
        writer.PutString(Cut(entry.SaveName, MaxSaveNameBytes));
        PutList(writer, entry.MissingAddons);
        writer.PutString(Cut(entry.Language, MaxLanguageBytes));
    }
    return writer.Data();
}

bool ReadSessionRoster(const std::vector<uint8_t>& message, SessionRoster& roster)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (!OpenMessage(reader, nc::proto::MsgType::SessionRoster))
    {
        return false;
    }
    roster.Revision = reader.GetU32();
    roster.HostState = reader.GetU8();
    roster.HostValue = reader.GetU32();
    if (!reader.Ok() || roster.HostState < 1 || roster.HostState > 5 || !GetText(reader, MaxSaveNameBytes, roster.WorldName) ||
        !GetText(reader, MaxShortTextBytes, roster.HostText) || !GetList(reader, roster.HostList))
    {
        return false;
    }
    roster.PauseFlags = reader.GetU8();
    roster.SharedNewGame = reader.GetU8();
    roster.Difficulty = reader.GetU8();
    roster.NewGameFlags = reader.GetU8();
    roster.HostByte = reader.GetU8();
    roster.HostTime = reader.GetU64();
    const uint8_t count = reader.GetU8();
    if (!reader.Ok() || (roster.PauseFlags & 0xFC) != 0 || roster.SharedNewGame >= 2 || roster.Difficulty >= 4 ||
        (roster.NewGameFlags & 0xF8) != 0 || (roster.NewGameFlags & 0x03) == 1 || roster.HostByte >= 5 || count > MaxRosterEntries)
    {
        return false;
    }
    roster.Entries.clear();
    for (uint8_t index = 0; index < count; ++index)
    {
        RosterEntry entry;
        entry.PeerNumber = reader.GetU32();
        if (!GetText(reader, MaxNameBytes, entry.Name))
        {
            return false;
        }
        entry.Where = reader.GetU8();
        entry.PingMs = reader.GetU16();
        entry.Role = reader.GetU8();
        entry.Flags = reader.GetU8();
        entry.Stage = reader.GetU8();
        entry.PauseBits = reader.GetU8();
        if (!reader.Ok() || entry.Where >= 4 || entry.Role >= 6 || entry.Stage >= 5 || (entry.PauseBits & 0xF8) != 0 ||
            !GetText(reader, MaxSaveNameBytes, entry.SaveName) || !GetList(reader, entry.MissingAddons) ||
            !GetText(reader, MaxLanguageBytes, entry.Language))
        {
            return false;
        }
        roster.Entries.push_back(std::move(entry));
    }
    return reader.Remaining() == 0;
}
}
