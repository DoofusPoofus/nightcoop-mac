#include "net/RelaySession.hpp"

#include <NetCrypto.hpp>
#include <ProtocolCore.hpp>

#include <enet/enet.h>

#include <chrono>
#include <cstdlib>
#include <random>

namespace nightcoop_mac::net
{
namespace
{
using namespace nc;

constexpr uint32_t HostHubId = 0;
constexpr uint8_t ReliableChannel = 0;
constexpr uint8_t UnreliableChannel = 1;
constexpr int ServiceWaitMs = 5;
constexpr int HubConnectTimeoutMs = 8000;

uint64_t SteadyMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t RandomClientId()
{
    std::random_device randomSource;
    return (static_cast<uint64_t>(randomSource()) << 32) | randomSource();
}

struct HubAddress
{
    std::string Host;
    uint16_t Port = 47800;
};

HubAddress ParseHubAddress(const std::string& text)
{
    HubAddress address;
    const std::string withoutFingerprint = text.substr(0, text.find('#'));
    const auto colon = withoutFingerprint.find(':');
    address.Host = withoutFingerprint.substr(0, colon);
    if (colon != std::string::npos)
    {
        address.Port = static_cast<uint16_t>(std::atoi(withoutFingerprint.c_str() + colon + 1));
    }
    return address;
}
}

struct RelaySession::Internals
{
    ENetHost* Client = nullptr;
    ENetPeer* HubPeer = nullptr;
    netcrypto::Local Local;
    std::map<uint32_t, netcrypto::Link> Links;
    std::map<uint32_t, bool> WelcomedGuests;
    uint64_t ClientId = RandomClientId();
    uint32_t MyPeerNumber = 0;
    bool InSession = false;
    bool HubConnected = false;
    uint64_t NextStateSendMs = 0;
};

RelaySession::RelaySession(LogFunction log)
    : m_log(std::move(log))
{
}

RelaySession::~RelaySession()
{
    Stop();
}

bool RelaySession::Start(const SessionConfig& config)
{
    if (m_thread.joinable())
    {
        m_log("session: already running, stop it first");
        return false;
    }
    m_config = config;
    m_stopRequested = false;
    m_clockOffsetMs = 0;
    {
        std::lock_guard guard(m_lock);
        m_status = SessionStatus {};
        m_status.Running = true;
        m_remoteStates.clear();
        m_peerIsMale.clear();
        m_outgoingGameMessages.clear();
        m_incomingGameMessages.clear();
        m_hasNewLocalState = false;
    }
    m_thread = std::thread(&RelaySession::NetworkThread, this);
    return true;
}

void RelaySession::Stop()
{
    m_stopRequested = true;
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void RelaySession::SetLocalPlayerState(const PlayerState& state)
{
    std::lock_guard guard(m_lock);
    m_localState = state;
    m_hasNewLocalState = true;
}

void RelaySession::SendGameMessage(const std::vector<uint8_t>& message, int32_t toPeer, bool reliable)
{
    std::lock_guard guard(m_lock);
    m_outgoingGameMessages.push_back({toPeer, 0, reliable, message});
}

std::vector<IncomingGameMessage> RelaySession::TakeGameMessages()
{
    std::lock_guard guard(m_lock);
    std::vector<IncomingGameMessage> taken;
    taken.swap(m_incomingGameMessages);
    return taken;
}

std::vector<PlayerState> RelaySession::TakeRemotePlayerStates()
{
    std::lock_guard guard(m_lock);
    std::vector<PlayerState> taken;
    taken.swap(m_remoteStates);
    return taken;
}

std::map<uint32_t, bool> RelaySession::PeerGenders() const
{
    std::lock_guard guard(m_lock);
    return m_peerIsMale;
}

SessionStatus RelaySession::GetStatus() const
{
    std::lock_guard guard(m_lock);
    return m_status;
}

uint64_t RelaySession::SessionClockMs() const
{
    return static_cast<uint64_t>(static_cast<int64_t>(SteadyMs()) + m_clockOffsetMs.load());
}

void RelaySession::NetworkThread()
{
    m_internals = std::make_unique<Internals>();
    auto& net = *m_internals;
    auto setProblem = [this](const std::string& problem) {
        m_log("session: " + problem);
        std::lock_guard guard(m_lock);
        m_status.LastProblem = problem;
    };

    auto sendToHub = [&](const std::vector<uint8_t>& bytes, bool reliable) {
        ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        enet_peer_send(net.HubPeer, reliable ? ReliableChannel : UnreliableChannel, packet);
    };
    auto sendPayload = [&](uint32_t toPeer, const std::vector<uint8_t>& sessionBytes, bool reliable) {
        proto::Writer wrapper(proto::MsgType::HubPayload);
        wrapper.PutU32(toPeer);
        wrapper.PutBytes(sessionBytes);
        sendToHub(wrapper.Data(), reliable);
    };
    auto sendSessionMessage = [&](uint32_t toPeer, const std::vector<uint8_t>& plainMessage, bool reliable) {
        auto& link = net.Links[toPeer];
        std::vector<uint8_t> sealed;
        switch (link.Outgoing(plainMessage.data(), plainMessage.size(), sealed))
        {
        case netcrypto::Link::Out::Plain: sendPayload(toPeer, plainMessage, reliable); break;
        case netcrypto::Link::Out::Sealed: sendPayload(toPeer, sealed, reliable); break;
        case netcrypto::Link::Out::Hold: link.Hold(plainMessage, reliable); break;
        case netcrypto::Link::Out::Drop: break;
        }
    };
    auto flushResolvedLink = [&](uint32_t peerId, netcrypto::Link& link) {
        if (!link.TakeResolved())
        {
            return;
        }
        m_log("session: link with peer " + std::to_string(peerId) + " is " + link.Describe());
        for (auto& held : link.TakeHeld())
        {
            sendSessionMessage(peerId, held.data, held.reliable);
        }
        if (auto refusal = link.TakeRefusal(); !refusal.empty())
        {
            sendPayload(peerId, refusal, true);
        }
    };
    auto startLink = [&](uint32_t peerId, netcrypto::Role myRole, uint32_t guestHubId) {
        netcrypto::Setup setup;
        setup.mine = myRole;
        setup.viaHub = true;
        setup.guestHubId = guestHubId;
        setup.peer = peerId;
        net.Links.erase(peerId);
        auto& link = net.Links[peerId];
        sendPayload(peerId, link.Begin(net.Local, setup, SteadyMs()), true);
    };
    auto updatePeerCount = [&]() {
        uint32_t peers = 0;
        if (m_config.IsHost)
        {
            for (const auto& [peerId, welcomed] : net.WelcomedGuests)
            {
                peers += welcomed ? 1 : 0;
            }
        }
        else
        {
            peers = net.InSession ? 1 : 0;
        }
        std::lock_guard guard(m_lock);
        m_status.ConnectedPeers = peers;
        m_status.InSession = net.InSession;
        m_status.MyPeerNumber = net.MyPeerNumber;
    };
    auto pushRemoteState = [&](const PlayerState& state) {
        std::lock_guard guard(m_lock);
        if (m_remoteStates.size() > 256)
        {
            m_remoteStates.erase(m_remoteStates.begin());
        }
        m_remoteStates.push_back(state);
    };

    auto deliverGameMessage = [&](uint32_t fromPeer, const std::vector<uint8_t>& inner) {
        std::lock_guard guard(m_lock);
        if (bool isMale = false; ReadCustomizationGender(inner, isMale))
        {
            if (!m_peerIsMale.count(fromPeer) || m_peerIsMale[fromPeer] != isMale)
            {
                m_log("session: peer " + std::to_string(fromPeer) + " look came in (" + (isMale ? "male" : "female") + ")");
            }
            m_peerIsMale[fromPeer] = isMale;
        }
        if (m_incomingGameMessages.size() >= 512)
        {
            m_incomingGameMessages.erase(m_incomingGameMessages.begin());
        }
        m_incomingGameMessages.push_back({fromPeer, inner});
    };

    auto onSessionMessage = [&](uint32_t fromPeer, const std::vector<uint8_t>& message) {
        Hello hello;
        Welcome welcome;
        PlayerState state;
        std::string rejectReason;
        if (m_config.IsHost && ReadHello(message, hello))
        {
            if (hello.Protocol != kProtocolVersion)
            {
                sendSessionMessage(fromPeer, WriteReject("protocol " + std::to_string(hello.Protocol) + " != " +
                                                         std::to_string(kProtocolVersion)),
                                   true);
                return;
            }
            if (hello.GameVersion != 0 && m_config.GameVersion != 0 && hello.GameVersion != m_config.GameVersion)
            {
                sendSessionMessage(fromPeer, WriteReject("game version mismatch"), true);
                return;
            }
            sendSessionMessage(fromPeer, WriteWelcome({fromPeer, static_cast<int64_t>(SessionClockMs())}), true);
            net.WelcomedGuests[fromPeer] = true;
            m_log("session: '" + hello.Nickname + "' joined as peer " + std::to_string(fromPeer));
            updatePeerCount();
        }
        else if (!m_config.IsHost && ReadWelcome(message, welcome))
        {
            net.MyPeerNumber = welcome.PeerNumber;
            net.InSession = true;
            m_clockOffsetMs = welcome.HostClockMs - static_cast<int64_t>(SteadyMs());
            m_log("session: welcomed as peer " + std::to_string(welcome.PeerNumber) + ", clock offset " +
                  std::to_string(m_clockOffsetMs.load()) + " ms");
            updatePeerCount();
        }
        else if (ReadReject(message, rejectReason))
        {
            setProblem("the host refused us: " + rejectReason);
            m_stopRequested = true;
        }
        else if (GameEnvelope envelope; ReadGameEnvelope(message, envelope))
        {
            bool deliverHere = true;
            if (m_config.IsHost)
            {
                if (!net.WelcomedGuests[fromPeer] || envelope.FromPeer != fromPeer)
                {
                    return;
                }
                deliverHere = envelope.ToPeer == kEveryPeer || envelope.ToPeer == static_cast<int32_t>(net.MyPeerNumber);
                for (const auto& [guestId, welcomed] : net.WelcomedGuests)
                {
                    const bool wanted = envelope.ToPeer == kEveryPeer ? guestId != fromPeer : static_cast<int32_t>(guestId) == envelope.ToPeer;
                    if (welcomed && wanted)
                    {
                        sendSessionMessage(guestId, message, envelope.Reliable);
                    }
                }
            }
            if (deliverHere)
            {
                deliverGameMessage(envelope.FromPeer, envelope.Inner);
            }
        }
        else if (ReadPlayerState(message, state))
        {
            if (!PlayerStateLooksSane(state))
            {
                return;
            }
            if (m_config.IsHost)
            {
                if (state.PeerNumber != fromPeer || !net.WelcomedGuests[fromPeer])
                {
                    return;
                }
                for (const auto& [guestId, welcomed] : net.WelcomedGuests)
                {
                    if (welcomed && guestId != fromPeer)
                    {
                        sendSessionMessage(guestId, message, false);
                    }
                }
            }
            else if (state.PeerNumber == net.MyPeerNumber)
            {
                return;
            }
            pushRemoteState(state);
        }
    };

    auto onPeerBytes = [&](uint32_t fromPeer, const std::vector<uint8_t>& bytes) {
        auto linkEntry = net.Links.find(fromPeer);
        if (linkEntry == net.Links.end())
        {
            return;
        }
        auto& link = linkEntry->second;
        std::vector<uint8_t> opened;
        const auto result = link.Incoming(net.Local, bytes.data(), bytes.size(), opened, SteadyMs());
        flushResolvedLink(fromPeer, link);
        if (result == netcrypto::Link::In::DeliverAsIs)
        {
            onSessionMessage(fromPeer, bytes);
        }
        else if (result == netcrypto::Link::In::DeliverOpened)
        {
            onSessionMessage(fromPeer, opened);
        }
    };

    auto sendRequest = [&]() {
        if (m_config.IsHost)
        {
            proto::Writer request(proto::MsgType::HubCreateSession);
            request.PutU32(kProtocolVersion);
            request.PutU8(0);
            netcrypto::PutClientExt(request, net.Local, nullptr);
            sendToHub(request.Data(), true);
            return;
        }
        proto::Writer request(proto::MsgType::HubJoinSession);
        request.PutU32(kProtocolVersion);
        std::vector<uint8_t> sealedCode;
        const bool sealIt = net.Local.SealCodes();
        if (sealIt)
        {
            sealedCode = net.Local.SealCodeForHub(m_config.JoinCode);
        }
        request.PutString(sealIt ? std::string() : m_config.JoinCode);
        request.PutU8(0);
        netcrypto::PutClientExt(request, net.Local, sealIt ? &sealedCode : nullptr);
        sendToHub(request.Data(), true);
    };

    auto readCertTail = [&](proto::Reader& reader) {
        netcrypto::HubExt ext;
        netcrypto::ReadHubExt(reader, ext);
        if (ext.hasCert)
        {
            net.Local.SetCert(ext.cert, SteadyMs());
        }
        return ext;
    };

    auto onHubPacket = [&](const uint8_t* data, size_t size) {
        proto::Reader reader(data, size);
        const auto type = reader.ReadHeader();
        if (!reader.Ok())
        {
            return;
        }
        switch (type)
        {
        case proto::MsgType::HubIntro:
        {
            netcrypto::HubIntro intro;
            if (netcrypto::ReadHubIntro(reader, intro))
            {
                net.Local.Decide(&intro);
            }
            break;
        }
        case proto::MsgType::HubSessionCreated:
        {
            std::string code = reader.GetString();
            const auto ext = readCertTail(reader);
            if (!ext.sealed.empty())
            {
                net.Local.OpenCodeFromHub(ext.sealed, code);
            }
            net.InSession = true;
            m_log("session: hosting, code " + code);
            {
                std::lock_guard guard(m_lock);
                m_status.Code = code;
            }
            updatePeerCount();
            break;
        }
        case proto::MsgType::HubJoinAccepted:
        {
            const uint32_t myHubId = reader.GetU32();
            readCertTail(reader);
            net.Local.SetHubId(myHubId);
            startLink(HostHubId, netcrypto::Role::Guest, myHubId);
            sendSessionMessage(HostHubId, WriteHello(net.ClientId, m_config.Nickname, m_config.GameVersion, m_config.InstallId),
                               true);
            break;
        }
        case proto::MsgType::HubJoinRejected:
        case proto::MsgType::Reject:
            setProblem("the hub said no: " + reader.GetString());
            m_stopRequested = true;
            break;
        case proto::MsgType::HubPeerUp:
        {
            const uint32_t guestId = reader.GetU32();
            readCertTail(reader);
            if (m_config.IsHost)
            {
                net.WelcomedGuests[guestId] = false;
                startLink(guestId, netcrypto::Role::Host, guestId);
            }
            break;
        }
        case proto::MsgType::HubPeerDown:
        {
            const uint32_t peerId = reader.GetU32();
            net.Links.erase(peerId);
            net.WelcomedGuests.erase(peerId);
            {
                std::lock_guard guard(m_lock);
                m_peerIsMale.erase(peerId);
            }
            m_log("session: peer " + std::to_string(peerId) + " left");
            updatePeerCount();
            break;
        }
        case proto::MsgType::HubPayload:
        {
            const uint32_t fromPeer = reader.GetU32();
            auto payload = reader.GetRest();
            if (reader.Ok())
            {
                onPeerBytes(fromPeer, payload);
            }
            break;
        }
        default: break;
        }
    };

    auto sendLocalStateIfDue = [&]() {
        const uint64_t now = SteadyMs();
        if (!net.InSession || now < net.NextStateSendMs)
        {
            return;
        }
        net.NextStateSendMs = now + 1000 / std::max<uint32_t>(1, m_config.StatesPerSecond);
        PlayerState state;
        {
            std::lock_guard guard(m_lock);
            if (!m_hasNewLocalState)
            {
                return;
            }
            state = m_localState;
            m_hasNewLocalState = false;
        }
        state.PeerNumber = net.MyPeerNumber;
        state.SessionTimeMs = SessionClockMs();
        const auto message = WritePlayerState(state);
        if (m_config.IsHost)
        {
            for (const auto& [guestId, welcomed] : net.WelcomedGuests)
            {
                if (welcomed)
                {
                    sendSessionMessage(guestId, message, false);
                }
            }
        }
        else
        {
            sendSessionMessage(HostHubId, message, false);
        }
    };

    net.Local.Reset(m_config.Hub);
    const auto hubAddress = ParseHubAddress(m_config.Hub);
    net.Client = enet_host_create(nullptr, 1, 2, 0, 0);
    ENetAddress address {};
    if (net.Client && enet_address_set_host(&address, hubAddress.Host.c_str()) == 0)
    {
        address.port = hubAddress.Port;
        net.HubPeer = enet_host_connect(net.Client, &address, 2, 0);
    }
    if (!net.Client || !net.HubPeer)
    {
        setProblem("could not reach the hub address " + m_config.Hub);
    }
    else
    {
        m_log(std::string("session: connecting to the hub ") + m_config.Hub + (m_config.IsHost ? " to host" : " to join " + m_config.JoinCode));
        const uint64_t connectDeadline = SteadyMs() + HubConnectTimeoutMs;
        while (!m_stopRequested)
        {
            ENetEvent event;
            while (enet_host_service(net.Client, &event, ServiceWaitMs) > 0)
            {
                if (event.type == ENET_EVENT_TYPE_CONNECT)
                {
                    net.HubConnected = true;
                    if (net.Local.NeedsIntro())
                    {
                        sendToHub(netcrypto::MakeHubIntroAsk(netcrypto::CachedHubListSerial()), true);
                    }
                    sendRequest();
                }
                else if (event.type == ENET_EVENT_TYPE_RECEIVE)
                {
                    onHubPacket(event.packet->data, event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                }
                else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                {
                    setProblem("lost the hub");
                    m_stopRequested = true;
                    break;
                }
            }
            if (!net.HubConnected && SteadyMs() > connectDeadline)
            {
                setProblem("the hub did not answer in " + std::to_string(HubConnectTimeoutMs / 1000) + " s");
                break;
            }
            for (auto& [peerId, link] : net.Links)
            {
                link.Tick(net.Local, SteadyMs());
                flushResolvedLink(peerId, link);
            }
            sendLocalStateIfDue();
            if (net.InSession)
            {
                std::vector<GameEnvelope> outgoing;
                {
                    std::lock_guard guard(m_lock);
                    outgoing.swap(m_outgoingGameMessages);
                }
                for (auto& envelope : outgoing)
                {
                    envelope.FromPeer = net.MyPeerNumber;
                    const auto wrapped = WriteGameEnvelope(envelope);
                    if (!m_config.IsHost)
                    {
                        sendSessionMessage(HostHubId, wrapped, envelope.Reliable);
                        continue;
                    }
                    for (const auto& [guestId, welcomed] : net.WelcomedGuests)
                    {
                        if (welcomed && (envelope.ToPeer == kEveryPeer || static_cast<int32_t>(guestId) == envelope.ToPeer))
                        {
                            sendSessionMessage(guestId, wrapped, envelope.Reliable);
                        }
                    }
                }
            }
        }
        if (net.HubConnected)
        {
            enet_peer_disconnect(net.HubPeer, 0);
            enet_host_service(net.Client, nullptr, 50);
        }
    }
    if (net.Client)
    {
        enet_host_destroy(net.Client);
    }
    m_internals.reset();
    std::lock_guard guard(m_lock);
    m_status.Running = false;
    m_status.InSession = false;
    m_status.ConnectedPeers = 0;
}
}
