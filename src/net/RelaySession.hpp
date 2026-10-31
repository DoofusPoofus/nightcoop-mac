/*
 relay session
 a nightcoop session thru a hub only (no direct path yet) running on its own thread
 the game thread hands in the local player state and takes the remote ones out, both behind a mutex
 host: answers Hello with Welcome, forwards each guests PlayerState to the other guests and sends its own to all
 guest: sends Hello after the link is up and its PlayerState to the host, gets evryone elses from the host
 PlayerStates go unreliable on channel 1, evrything else reliable on channel 0
 game messages ride in Game envelopes (net/GameMessages.hpp): a guest sends them to the host, the host keeps the ones for it,
 forwards the rest (to one peer or to evryone but the sender) and drops envelopes whose sender number is not the guest that
 sent it. the game thread takes the ones for us out with their real sender, PlayerCustomization also feeds the gender list
*/
#pragma once

#include "net/GameMessages.hpp"
#include "net/SessionMessages.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nightcoop_mac::net
{
struct SessionConfig
{
    bool IsHost = false;
    std::string JoinCode;
    std::string Hub;
    std::string Nickname = "guest";
    uint64_t GameVersion = 0;
    uint64_t InstallId = 0;
    uint32_t StatesPerSecond = 20;
};

struct IncomingGameMessage
{
    uint32_t FromPeer = 0;
    std::vector<uint8_t> Message;
};

struct SessionStatus
{
    bool Running = false;
    bool InSession = false;
    std::string Code;
    uint32_t MyPeerNumber = 0;
    uint32_t ConnectedPeers = 0;
    std::string LastProblem;
};

class RelaySession
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    explicit RelaySession(LogFunction log);
    ~RelaySession();
    RelaySession(const RelaySession&) = delete;
    RelaySession& operator=(const RelaySession&) = delete;

    bool Start(const SessionConfig& config);
    void Stop();

    void SetLocalPlayerState(const PlayerState& state);
    void SendGameMessage(const std::vector<uint8_t>& message, int32_t toPeer = kEveryPeer, bool reliable = true);
    std::vector<IncomingGameMessage> TakeGameMessages();
    std::vector<PlayerState> TakeRemotePlayerStates();
    std::map<uint32_t, bool> PeerGenders() const;
    std::map<uint32_t, std::string> PeerNicknames() const;
    SessionStatus GetStatus() const;
    uint64_t SessionClockMs() const;

private:
    struct Internals;

    void NetworkThread();

    LogFunction m_log;
    SessionConfig m_config;
    std::unique_ptr<Internals> m_internals;
    std::thread m_thread;
    std::atomic<bool> m_stopRequested {false};
    std::atomic<int64_t> m_clockOffsetMs {0};

    mutable std::mutex m_lock;
    SessionStatus m_status;
    bool m_hasNewLocalState = false;
    PlayerState m_localState;
    std::vector<PlayerState> m_remoteStates;
    std::map<uint32_t, bool> m_peerIsMale;
    std::map<uint32_t, std::string> m_peerNicknames;
    std::vector<GameEnvelope> m_outgoingGameMessages;
    std::vector<IncomingGameMessage> m_incomingGameMessages;
};
}
