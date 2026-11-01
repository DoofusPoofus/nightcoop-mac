#include "plugin/WorldLoad.hpp"

#include "net/RelaySession.hpp"
#include "plugin/Hud.hpp"

#include <ProtocolCore.hpp>

#include <algorithm>

namespace nightcoop_mac::world_load
{
namespace
{
constexpr uint64_t RefreshEveryMs = 3000;

bool WasInSave = false;
bool StateKnown = false;
uint32_t TransitionNumber = 0;
uint32_t WorldNumber = 0;
uint64_t TransitionStartedMs = 0;
uint64_t NextRefreshMs = 0;
uint8_t StoryState = 0xFF;
uint64_t NextStoryMs = 0;

std::vector<uint8_t> WriteOurState(uint32_t ourPeer, bool inTransition, uint64_t nowMs)
{
    nc::proto::Writer writer(nc::proto::MsgType::WorldLoad);
    writer.PutU32(ourPeer);
    writer.PutU8(1);
    writer.PutU8(inTransition ? 1 : 0);
    writer.PutU32(TransitionNumber);
    writer.PutU8(inTransition ? 0 : 1);
    writer.PutU32(static_cast<uint32_t>(std::min<uint64_t>(nowMs - TransitionStartedMs, 0xFFFFFFFFu)));
    writer.PutU32(WorldNumber);
    writer.PutU32(0);
    writer.PutU64(0);
    writer.PutU8(0);
    return writer.Data();
}
}

bool Handles(uint8_t messageType)
{
    return messageType == static_cast<uint8_t>(nc::proto::MsgType::WorldLoad) || messageType == static_cast<uint8_t>(nc::proto::MsgType::LobbyStart) ||
           messageType == static_cast<uint8_t>(nc::proto::MsgType::StoryHold);
}

void Tick(uint64_t nowMs, bool inSave, bool inHostWorld, net::RelaySession& session, const LogFunction& log)
{
    const auto status = session.GetStatus();
    if (status.InSession && status.MyPeerNumber != 0)
    {
        const uint8_t storyState = !inSave ? 1 : inHostWorld ? 3 : 0;
        if (storyState != StoryState || nowMs >= NextStoryMs)
        {
            if (storyState != StoryState)
            {
                log(std::string("story hold: we tell the host we are ") + (storyState == 1 ? "coming" : storyState == 3 ? "ready" : "idle"));
            }
            StoryState = storyState;
            NextStoryMs = nowMs + 5000;
            nc::proto::Writer story(nc::proto::MsgType::StoryHold);
            story.PutU32(status.MyPeerNumber);
            story.PutU8(1);
            story.PutU8(storyState);
            story.PutU8(0);
            story.PutU32(WorldNumber);
            story.PutU32(0);
            story.PutU64(0);
            story.PutU8(0);
            session.SendGameMessage(story.Data(), 0);
        }
    }
    const bool changed = !StateKnown || inSave != WasInSave;
    if (changed)
    {
        StateKnown = true;
        WasInSave = inSave;
        TransitionStartedMs = nowMs;
        if (inSave)
        {
            ++WorldNumber;
        }
        else
        {
            ++TransitionNumber;
        }
    }
    if (!status.InSession || status.ConnectedPeers == 0 || (!changed && nowMs < NextRefreshMs))
    {
        return;
    }
    NextRefreshMs = nowMs + RefreshEveryMs;
    session.SendGameMessage(WriteOurState(status.MyPeerNumber, !inSave, nowMs));
    if (changed)
    {
        log(inSave ? "world load: our world is up, the partners hear we are settled" : "world load: we are loading, the partners hear we are in a transition");
    }
}

void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, bool weAreHost, const LogFunction& log)
{
    nc::proto::Reader reader(message.data(), message.size());
    const auto type = reader.ReadHeader();
    if (type == nc::proto::MsgType::LobbyStart && !weAreHost && fromPeer == 0)
    {
        const uint32_t startId = reader.GetU32();
        const std::string name = reader.GetString();
        const uint8_t newGame = reader.GetU8();
        if (!reader.Ok() || reader.Remaining() != 0 || startId == 0 || name.size() > 160 || newGame > 1)
        {
            return;
        }
        log("world load: the host started " + (newGame ? std::string("a new game") : "its save " + name) + " from its lobby");
        hud::Show(newGame ? "The host started a new game (not on mac yet, we join its world once it is up)" : "The host started, we join once its world is up", 5.0f);
        return;
    }
    if (type == nc::proto::MsgType::StoryHold && fromPeer == 0 && !weAreHost)
    {
        reader.GetU32();
        const uint8_t kind = reader.GetU8();
        reader.GetU8();
        const uint8_t mode = reader.GetU8();
        const uint32_t hold = reader.GetU32();
        if (reader.Ok() && kind == 2)
        {
            log("story hold: the host holds its story (#" + std::to_string(hold) + (mode == 2 ? ", a full pause)" : ", the story stands)"));
        }
        return;
    }
    if (type == nc::proto::MsgType::WorldLoad && fromPeer == 0 && !weAreHost)
    {
        reader.GetU32();
        const uint8_t kind = reader.GetU8();
        if (reader.Ok() && kind == 2)
        {
            log("world load: the host let a loading hold go (our game does not wait on it)");
        }
    }
}
}
