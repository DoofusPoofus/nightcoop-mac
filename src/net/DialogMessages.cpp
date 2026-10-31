#include "net/DialogMessages.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>
#include <cmath>

namespace nightcoop_mac::net
{
namespace
{
constexpr size_t LongestTitle = 512;
constexpr size_t LongestOptionText = 512;
constexpr size_t LongestSpeaker = 128;
constexpr size_t LongestLineText = 1024;
constexpr size_t LongestLanguage = 8;
constexpr size_t MostOptions = 16;
constexpr size_t MostVoters = 8;
constexpr size_t MostPlaces = 8;
constexpr uint8_t HighestLineType = 13;
constexpr float LongestLineSeconds = 120.0f;
constexpr uint32_t LongestSkipMs = 100000;

std::string CutText(const std::string& text, size_t limit)
{
    if (text.size() <= limit)
    {
        return text;
    }
    size_t length = limit;
    while (length > 0 && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80)
    {
        --length;
    }
    return text.substr(0, length);
}

bool OptionInRange(int8_t option)
{
    return option >= -1 && option < static_cast<int8_t>(MostOptions);
}

void PutVoters(nc::proto::Writer& writer, const std::vector<uint32_t>& voters)
{
    const auto count = static_cast<uint8_t>(std::min(voters.size(), MostVoters));
    writer.PutU8(count);
    for (uint8_t index = 0; index < count; ++index)
    {
        writer.PutU32(voters[index]);
    }
}

bool GetVoters(nc::proto::Reader& reader, std::vector<uint32_t>& voters)
{
    const uint8_t count = reader.GetU8();
    voters.clear();
    if (count > MostVoters)
    {
        return false;
    }
    for (uint8_t index = 0; index < count && reader.Ok(); ++index)
    {
        voters.push_back(reader.GetU32());
    }
    return reader.Ok();
}

bool HasAnyRowId(const std::vector<DialogRowId>& rowIds)
{
    return std::any_of(rowIds.begin(), rowIds.end(), [](const DialogRowId& row) { return row.Scene != 0 && row.Item != 0; });
}
}

std::vector<uint8_t> WriteDialogBallot(const DialogBallot& ballot)
{
    nc::proto::Writer writer(nc::proto::MsgType::DialogBallot);
    writer.PutU32(ballot.VoteId);
    writer.PutU64(ballot.SceneId);
    writer.PutU8(ballot.Flags);
    writer.PutString(CutText(ballot.Title, LongestTitle));
    const size_t optionCount = std::min(ballot.Options.size(), MostOptions);
    writer.PutU8(static_cast<uint8_t>(optionCount));
    for (size_t index = 0; index < optionCount; ++index)
    {
        writer.PutString(CutText(ballot.Options[index].Text, LongestOptionText));
        writer.PutU32(ballot.Options[index].ChoiceType);
    }
    PutVoters(writer, ballot.Voters);
    const bool sendKeys = ballot.OptionKeys.size() == ballot.Options.size() && optionCount == ballot.Options.size();
    writer.PutU8(sendKeys ? static_cast<uint8_t>(optionCount) : 0);
    for (size_t index = 0; sendKeys && index < optionCount; ++index)
    {
        writer.PutU64(ballot.OptionKeys[index]);
    }
    const bool rowIdsOk = std::all_of(ballot.RowIds.begin(), ballot.RowIds.end(),
                                      [](const DialogRowId& row) { return (row.Scene == 0) == (row.Item == 0); });
    const bool sendRowIds = ballot.RowIds.size() == ballot.Options.size() && optionCount == ballot.Options.size() && rowIdsOk && HasAnyRowId(ballot.RowIds);
    writer.PutU8(sendRowIds ? static_cast<uint8_t>(optionCount) : 0);
    for (size_t index = 0; sendRowIds && index < optionCount; ++index)
    {
        writer.PutU64(ballot.RowIds[index].Scene);
        writer.PutU32(ballot.RowIds[index].Item);
    }
    return writer.Data();
}

bool ReadDialogBallot(const std::vector<uint8_t>& message, DialogBallot& ballot)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::DialogBallot)
    {
        return false;
    }
    ballot.VoteId = reader.GetU32();
    ballot.SceneId = reader.GetU64();
    ballot.Flags = reader.GetU8();
    ballot.Title = reader.GetString();
    const uint8_t optionCount = reader.GetU8();
    if (!reader.Ok() || ballot.VoteId == 0 || ballot.Title.size() > LongestTitle || optionCount == 0 || optionCount > MostOptions)
    {
        return false;
    }
    ballot.Options.clear();
    for (uint8_t index = 0; index < optionCount && reader.Ok(); ++index)
    {
        DialogOption option;
        option.Text = reader.GetString();
        option.ChoiceType = reader.GetU32();
        if (option.Text.size() > LongestOptionText)
        {
            return false;
        }
        ballot.Options.push_back(option);
    }
    if (!GetVoters(reader, ballot.Voters))
    {
        return false;
    }
    const uint8_t keyCount = reader.GetU8();
    if (keyCount != 0 && keyCount != optionCount)
    {
        return false;
    }
    ballot.OptionKeys.clear();
    for (uint8_t index = 0; index < keyCount && reader.Ok(); ++index)
    {
        ballot.OptionKeys.push_back(reader.GetU64());
    }
    const uint8_t rowIdCount = reader.GetU8();
    if (rowIdCount != 0 && rowIdCount != optionCount)
    {
        return false;
    }
    ballot.RowIds.clear();
    for (uint8_t index = 0; index < rowIdCount && reader.Ok(); ++index)
    {
        DialogRowId row;
        row.Scene = reader.GetU64();
        row.Item = reader.GetU32();
        if ((row.Scene == 0) != (row.Item == 0))
        {
            return false;
        }
        ballot.RowIds.push_back(row);
    }
    if (rowIdCount > 0 && !HasAnyRowId(ballot.RowIds))
    {
        return false;
    }
    return reader.Ok() && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteDialogVote(const DialogVote& vote)
{
    nc::proto::Writer writer(nc::proto::MsgType::DialogVote);
    writer.PutU32(vote.VoteId);
    writer.PutU8(static_cast<uint8_t>(vote.Option));
    writer.PutU32(vote.Option < 0 ? 0 : vote.Item);
    return writer.Data();
}

bool ReadDialogVote(const std::vector<uint8_t>& message, DialogVote& vote)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::DialogVote)
    {
        return false;
    }
    vote.VoteId = reader.GetU32();
    vote.Option = static_cast<int8_t>(reader.GetU8());
    vote.Item = reader.GetU32();
    return reader.Ok() && reader.Remaining() == 0 && vote.VoteId != 0 && OptionInRange(vote.Option) && (vote.Option >= 0 || vote.Item == 0);
}

std::vector<uint8_t> WriteDialogTally(const DialogTally& tally)
{
    nc::proto::Writer writer(nc::proto::MsgType::DialogTally);
    writer.PutU32(tally.VoteId);
    writer.PutU8(tally.State);
    writer.PutU8(static_cast<uint8_t>(tally.Option));
    writer.PutU8(tally.Needed);
    const auto voteCount = static_cast<uint8_t>(std::min(tally.Votes.size(), MostVoters));
    writer.PutU8(voteCount);
    for (uint8_t index = 0; index < voteCount; ++index)
    {
        writer.PutU32(tally.Votes[index].Peer);
        writer.PutU8(static_cast<uint8_t>(tally.Votes[index].Option));
    }
    PutVoters(writer, tally.Voters);
    const bool decided = tally.State == kTallyDecided || tally.State == kTallyDecidedByTime;
    writer.PutU64(decided ? tally.SharedTime : 0);
    return writer.Data();
}

bool ReadDialogTally(const std::vector<uint8_t>& message, DialogTally& tally)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::DialogTally)
    {
        return false;
    }
    tally.VoteId = reader.GetU32();
    tally.State = reader.GetU8();
    tally.Option = static_cast<int8_t>(reader.GetU8());
    tally.Needed = reader.GetU8();
    const uint8_t voteCount = reader.GetU8();
    if (!reader.Ok() || tally.VoteId == 0 || tally.State < kTallyOpen || tally.State > kTallyDecidedByTime || !OptionInRange(tally.Option) ||
        voteCount > MostVoters)
    {
        return false;
    }
    tally.Votes.clear();
    for (uint8_t index = 0; index < voteCount && reader.Ok(); ++index)
    {
        DialogPeerVote vote;
        vote.Peer = reader.GetU32();
        vote.Option = static_cast<int8_t>(reader.GetU8());
        if (!OptionInRange(vote.Option))
        {
            return false;
        }
        tally.Votes.push_back(vote);
    }
    if (!GetVoters(reader, tally.Voters))
    {
        return false;
    }
    tally.SharedTime = reader.GetU64();
    const bool decided = tally.State == kTallyDecided || tally.State == kTallyDecidedByTime;
    if (decided && tally.Option < 0)
    {
        return false;
    }
    if (!decided && tally.SharedTime != 0)
    {
        return false;
    }
    return reader.Ok() && reader.Remaining() == 0;
}

std::vector<uint8_t> WriteDialogLine(const DialogLine& line)
{
    nc::proto::Writer writer(nc::proto::MsgType::DialogLine);
    writer.PutU64(line.LineId);
    writer.PutU64(line.SceneId);
    writer.PutU8(std::min(line.LineType, HighestLineType));
    const bool secondsOk = std::isfinite(line.Seconds) && line.Seconds > 0.0f && line.Seconds <= LongestLineSeconds;
    writer.PutF32(secondsOk ? line.Seconds : 4.0f);
    writer.PutString(CutText(line.Speaker, LongestSpeaker));
    writer.PutString(CutText(line.Text, LongestLineText));
    writer.PutU64(line.SceneFile);
    writer.PutU64(line.LineKey);
    writer.PutString(CutText(line.Language, LongestLanguage));
    return writer.Data();
}

bool ReadDialogLine(const std::vector<uint8_t>& message, DialogLine& line)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::DialogLine)
    {
        return false;
    }
    line.LineId = reader.GetU64();
    line.SceneId = reader.GetU64();
    line.LineType = reader.GetU8();
    line.Seconds = reader.GetF32();
    line.Speaker = reader.GetString();
    line.Text = reader.GetString();
    line.SceneFile = reader.GetU64();
    line.LineKey = reader.GetU64();
    line.Language = reader.GetString();
    return reader.Ok() && reader.Remaining() == 0 && line.LineId != 0 && line.LineType <= HighestLineType && std::isfinite(line.Seconds) &&
           line.Seconds >= 0.0f && line.Seconds <= LongestLineSeconds && line.Speaker.size() <= LongestSpeaker && line.Text.size() <= LongestLineText &&
           line.Language.size() <= LongestLanguage;
}

std::vector<uint8_t> WriteVoteModeWord(const VoteModeWord& word)
{
    nc::proto::Writer writer(static_cast<nc::proto::MsgType>(kVoteModeWordType));
    writer.PutU8(word.Sub);
    writer.PutU8(word.Mode);
    writer.PutU8(word.Flag);
    return writer.Data();
}

bool ReadVoteModeWord(const std::vector<uint8_t>& message, VoteModeWord& word)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != static_cast<nc::proto::MsgType>(kVoteModeWordType))
    {
        return false;
    }
    word.Sub = reader.GetU8();
    word.Mode = reader.GetU8();
    word.Flag = reader.GetU8();
    return reader.Ok() && reader.Remaining() == 0 && word.Sub == 1 && word.Mode <= kVoteModeEveryone;
}

std::vector<uint8_t> WriteSceneLineSkip(const SceneLineSkip& skip)
{
    nc::proto::Writer writer(nc::proto::MsgType::SceneLineSkip);
    writer.PutU32(skip.Peer);
    writer.PutU64(skip.SceneId);
    writer.PutU32(std::min(skip.Milliseconds, LongestSkipMs));
    writer.PutU64(skip.SharedTime);
    writer.PutU8(skip.Flags & 0x07);
    const auto placeCount = static_cast<uint8_t>(std::min(skip.Places.size(), MostPlaces));
    writer.PutU8(placeCount);
    for (uint8_t index = 0; index < placeCount; ++index)
    {
        writer.PutU8(skip.Places[index].Kind);
        writer.PutU64(skip.Places[index].Key);
        writer.PutU16(skip.Places[index].Nth);
    }
    return writer.Data();
}

bool ReadSceneLineSkip(const std::vector<uint8_t>& message, SceneLineSkip& skip)
{
    nc::proto::Reader reader(message.data(), message.size());
    if (reader.ReadHeader() != nc::proto::MsgType::SceneLineSkip)
    {
        return false;
    }
    skip.Peer = reader.GetU32();
    skip.SceneId = reader.GetU64();
    skip.Milliseconds = reader.GetU32();
    skip.SharedTime = reader.GetU64();
    skip.Flags = reader.GetU8();
    const uint8_t placeCount = reader.GetU8();
    if (!reader.Ok() || skip.SceneId == 0 || skip.Milliseconds > LongestSkipMs || skip.SharedTime == 0 || (skip.Flags & ~0x07) != 0 ||
        ((skip.Flags & (kSkipPress | kSkipLandedAtChoice)) != 0 && (skip.Flags & kSkipByLine) != 0) || placeCount == 0 || placeCount > MostPlaces)
    {
        return false;
    }
    skip.Places.clear();
    for (uint8_t index = 0; index < placeCount && reader.Ok(); ++index)
    {
        ScenePlace place;
        place.Kind = reader.GetU8();
        place.Key = reader.GetU64();
        place.Nth = reader.GetU16();
        if (place.Kind < 1 || place.Kind > 4 || place.Key == 0 || place.Nth == 0)
        {
            return false;
        }
        skip.Places.push_back(place);
    }
    return reader.Ok() && reader.Remaining() == 0;
}
}
