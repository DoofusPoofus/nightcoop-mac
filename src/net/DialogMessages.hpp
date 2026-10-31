/*
 dialog and scene messages rebuilt from nightcoops dialogvote and sceneskip modules (0.1.2 dll). all ride in Game envelopes
 DialogBallot (74) host -> evryone when a choice window opens: u32 vote id (not 0) | u64 scene id | u8 flags (1 timed by the
   game 2 exactly one option 4 gather at the npc) | string title (512) | u8 options 1..16 x (string text (512) | u32 choice
   type bits) | u8 voters 0..8 x u32 peer | u8 keys (0 or one per option) x u64 | u8 row ids (0 or one per option) x (u64 scene
   | u32 item both 0 or both set at least one pair set)
 DialogVote (75) guest -> host 14 bytes: u32 vote id | i8 option (-1 takes the vote back) | u32 item (0 when option is -1)
 DialogTally (76) host -> evryone on evry change: u32 vote id | u8 state (1 open 2 decided 3 cancelled 4 decided by time) | i8
   option (0..15 for 2 and 4) | u8 votes needed (0 by mode) | u8 votes 0..8 x (u32 peer | i8 option) | u8 voters 0..8 x u32 peer
   | u64 shared time (the session clock moment evryone applies it at 0 while open or cancelled)
 DialogLine (78) host -> evryone one per subtitle line: u64 line id (not 0) | u64 scene id | u8 line type (0..13) | f32
   seconds (0..120) | string speaker (128) | string text (1024) | u64 scene file | u64 line key | string language (8)
 VoteModeWord (249) host -> each peer: u8 sub (1) | u8 mode (0 majority 1 evryone) | u8 flag. 249 is not in the hub enum
 SceneLineSkip (118) any -> host -> evryone: u32 peer | u64 scene id (not 0) | u32 ms (up to 100000) | u64 shared time (not 0)
   | u8 flags (bits 0..2 bit0 alone or not at all) | u8 places 1..8 x (u8 kind 1..4 | u64 key (not 0) | u16 nth (not 0))
 readers drop short reads values out of range and bytes left over like the windows readers do. writers cut strings and
 lists to what the readers take
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
inline constexpr uint8_t kVoteModeWordType = 249;

inline constexpr uint8_t kBallotTimedByGame = 0x01;
inline constexpr uint8_t kBallotOneOption = 0x02;
inline constexpr uint8_t kBallotGatherAtNpc = 0x04;

inline constexpr uint8_t kTallyOpen = 1;
inline constexpr uint8_t kTallyDecided = 2;
inline constexpr uint8_t kTallyCancelled = 3;
inline constexpr uint8_t kTallyDecidedByTime = 4;

inline constexpr uint8_t kVoteModeMajority = 0;
inline constexpr uint8_t kVoteModeEveryone = 1;

inline constexpr uint8_t kSkipByLine = 0x01;
inline constexpr uint8_t kSkipPress = 0x02;
inline constexpr uint8_t kSkipLandedAtChoice = 0x04;

struct DialogOption
{
    std::string Text;
    uint32_t ChoiceType = 0;
};

struct DialogRowId
{
    uint64_t Scene = 0;
    uint32_t Item = 0;
};

struct DialogBallot
{
    uint32_t VoteId = 0;
    uint64_t SceneId = 0;
    uint8_t Flags = 0;
    std::string Title;
    std::vector<DialogOption> Options;
    std::vector<uint32_t> Voters;
    std::vector<uint64_t> OptionKeys;
    std::vector<DialogRowId> RowIds;
};

struct DialogVote
{
    uint32_t VoteId = 0;
    int8_t Option = -1;
    uint32_t Item = 0;
};

struct DialogPeerVote
{
    uint32_t Peer = 0;
    int8_t Option = -1;
};

struct DialogTally
{
    uint32_t VoteId = 0;
    uint8_t State = kTallyOpen;
    int8_t Option = -1;
    uint8_t Needed = 0;
    std::vector<DialogPeerVote> Votes;
    std::vector<uint32_t> Voters;
    uint64_t SharedTime = 0;
};

struct DialogLine
{
    uint64_t LineId = 0;
    uint64_t SceneId = 0;
    uint8_t LineType = 1;
    float Seconds = 4.0f;
    std::string Speaker;
    std::string Text;
    uint64_t SceneFile = 0;
    uint64_t LineKey = 0;
    std::string Language;
};

struct VoteModeWord
{
    uint8_t Sub = 1;
    uint8_t Mode = kVoteModeMajority;
    uint8_t Flag = 0;
};

struct ScenePlace
{
    uint8_t Kind = 1;
    uint64_t Key = 0;
    uint16_t Nth = 1;
};

struct SceneLineSkip
{
    uint32_t Peer = 0;
    uint64_t SceneId = 0;
    uint32_t Milliseconds = 0;
    uint64_t SharedTime = 0;
    uint8_t Flags = 0;
    std::vector<ScenePlace> Places;
};

std::vector<uint8_t> WriteDialogBallot(const DialogBallot& ballot);
bool ReadDialogBallot(const std::vector<uint8_t>& message, DialogBallot& ballot);
std::vector<uint8_t> WriteDialogVote(const DialogVote& vote);
bool ReadDialogVote(const std::vector<uint8_t>& message, DialogVote& vote);
std::vector<uint8_t> WriteDialogTally(const DialogTally& tally);
bool ReadDialogTally(const std::vector<uint8_t>& message, DialogTally& tally);
std::vector<uint8_t> WriteDialogLine(const DialogLine& line);
bool ReadDialogLine(const std::vector<uint8_t>& message, DialogLine& line);
std::vector<uint8_t> WriteVoteModeWord(const VoteModeWord& word);
bool ReadVoteModeWord(const std::vector<uint8_t>& message, VoteModeWord& word);
std::vector<uint8_t> WriteSceneLineSkip(const SceneLineSkip& skip);
bool ReadSceneLineSkip(const std::vector<uint8_t>& message, SceneLineSkip& skip);
}
