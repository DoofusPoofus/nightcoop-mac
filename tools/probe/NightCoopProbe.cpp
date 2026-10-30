/*
 nightcoop probe
 a headless nightcoop client that talks to a hub like the relay transport does
 host mode opens a session prints the code and answers a Hello with a Welcome
 join mode joins by code sends a Hello and after the Welcome walks a few PlayerStates along x
 both sides do the CryptoHello link thru HubPayload with the shared netcrypto code
 usage: nightcoop_probe host HUB   or   nightcoop_probe join CODE HUB
 HUB looks like 127.0.0.1:47800#FINGERPRINT
*/
#include "net/SessionMessages.hpp"

#include <Log.hpp>
#include <NetCrypto.hpp>
#include <ProtocolCore.hpp>

#include <enet/enet.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace
{
using namespace nc;

constexpr uint32_t HostHubId = 0;
constexpr int ProbeSeconds = 20;
constexpr int WalkSteps = 5;

uint64_t NowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct HubAddress
{
    std::string Host;
    uint16_t Port = 47800;
    std::string Full;
};

HubAddress ParseHub(const std::string& text)
{
    HubAddress address;
    address.Full = text;
    std::string withoutFingerprint = text.substr(0, text.find('#'));
    const auto colon = withoutFingerprint.find(':');
    address.Host = withoutFingerprint.substr(0, colon);
    if (colon != std::string::npos)
    {
        address.Port = static_cast<uint16_t>(std::atoi(withoutFingerprint.c_str() + colon + 1));
    }
    return address;
}

class Probe
{
public:
    Probe(bool isHost, std::string joinCode, HubAddress hub)
        : m_isHost(isHost)
        , m_joinCode(std::move(joinCode))
        , m_hub(std::move(hub))
    {
    }

    int Run()
    {
        m_local.Reset(m_hub.Full);
        m_client = enet_host_create(nullptr, 1, 2, 0, 0);
        ENetAddress hubAddress{};
        enet_address_set_host(&hubAddress, m_hub.Host.c_str());
        hubAddress.port = m_hub.Port;
        m_hubPeer = enet_host_connect(m_client, &hubAddress, 2, 0);
        if (!m_client || !m_hubPeer)
        {
            std::printf("probe: could not start enet\n");
            return 1;
        }

        const uint64_t deadline = NowMs() + ProbeSeconds * 1000;
        while (NowMs() < deadline && !m_done)
        {
            ENetEvent event;
            while (enet_host_service(m_client, &event, 50) > 0)
            {
                if (event.type == ENET_EVENT_TYPE_CONNECT)
                {
                    OnHubConnected();
                }
                else if (event.type == ENET_EVENT_TYPE_RECEIVE)
                {
                    OnHubPacket(event.packet->data, event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                }
                else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                {
                    std::printf("probe: the hub dropped us\n");
                    return 1;
                }
            }
            for (auto& [peerId, link] : m_links)
            {
                link.Tick(m_local, NowMs());
                FlushResolvedLink(peerId, link);
            }
        }
        enet_host_flush(m_client);
        enet_host_destroy(m_client);
        return m_done ? 0 : 2;
    }

private:
    void SendToHub(const std::vector<uint8_t>& bytes)
    {
        ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(m_hubPeer, 0, packet);
    }

    void SendPayload(uint32_t toPeer, const std::vector<uint8_t>& sessionBytes)
    {
        proto::Writer wrapper(proto::MsgType::HubPayload);
        wrapper.PutU32(toPeer);
        wrapper.PutBytes(sessionBytes);
        SendToHub(wrapper.Data());
    }

    void SendSessionMessage(uint32_t toPeer, const std::vector<uint8_t>& plainMessage)
    {
        auto& link = m_links[toPeer];
        std::vector<uint8_t> sealed;
        switch (link.Outgoing(plainMessage.data(), plainMessage.size(), sealed))
        {
        case netcrypto::Link::Out::Plain: SendPayload(toPeer, plainMessage); break;
        case netcrypto::Link::Out::Sealed: SendPayload(toPeer, sealed); break;
        case netcrypto::Link::Out::Hold: link.Hold(plainMessage, true); break;
        case netcrypto::Link::Out::Drop: std::printf("probe: a message to peer %u was dropped by the link\n", toPeer); break;
        }
    }

    void OnHubConnected()
    {
        std::printf("probe: connected to the hub %s\n", m_hub.Full.c_str());
        if (m_local.NeedsIntro())
        {
            auto ask = netcrypto::MakeHubIntroAsk(netcrypto::CachedHubListSerial());
            SendToHub(ask);
            std::printf("probe: asked the hub who it is\n");
        }
        SendRequest();
    }

    void SendRequest()
    {
        if (m_isHost)
        {
            proto::Writer request(proto::MsgType::HubCreateSession);
            request.PutU32(nightcoop_mac::net::kProtocolVersion);
            request.PutU8(0);
            netcrypto::PutClientExt(request, m_local, nullptr);
            SendToHub(request.Data());
            std::printf("probe: asked for a new session (protocol %u)\n", nightcoop_mac::net::kProtocolVersion);
            return;
        }
        proto::Writer request(proto::MsgType::HubJoinSession);
        request.PutU32(nightcoop_mac::net::kProtocolVersion);
        std::vector<uint8_t> sealedCode;
        const bool sealIt = m_local.SealCodes();
        if (sealIt)
        {
            sealedCode = m_local.SealCodeForHub(m_joinCode);
        }
        request.PutString(sealIt ? std::string() : m_joinCode);
        request.PutU8(0);
        netcrypto::PutClientExt(request, m_local, sealIt ? &sealedCode : nullptr);
        SendToHub(request.Data());
        std::printf("probe: asked to join %s%s\n", m_joinCode.c_str(), sealIt ? " (code sealed)" : "");
    }

    void OnHubPacket(const uint8_t* data, size_t size)
    {
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
                m_local.Decide(&intro);
                std::printf("probe: hub identity decided, strict %d sealed codes %d\n", m_local.Strict(), m_local.SealCodes());
            }
            break;
        }
        case proto::MsgType::HubSessionCreated:
        {
            std::string code = reader.GetString();
            netcrypto::HubExt ext;
            netcrypto::ReadHubExt(reader, ext);
            if (ext.hasCert)
            {
                m_local.SetCert(ext.cert, NowMs());
            }
            if (!ext.sealed.empty())
            {
                m_local.OpenCodeFromHub(ext.sealed, code);
            }
            std::printf("probe: session created, code %s (key signed by the hub %d)\n", code.c_str(), ext.hasCert);
            std::fflush(stdout);
            break;
        }
        case proto::MsgType::HubJoinAccepted:
        {
            const uint32_t myHubId = reader.GetU32();
            netcrypto::HubExt ext;
            netcrypto::ReadHubExt(reader, ext);
            if (ext.hasCert)
            {
                m_local.SetCert(ext.cert, NowMs());
            }
            m_local.SetHubId(myHubId);
            std::printf("probe: joined as hub peer %u (key signed by the hub %d)\n", myHubId, ext.hasCert);
            StartLink(HostHubId, netcrypto::Role::Guest, myHubId);
            break;
        }
        case proto::MsgType::HubJoinRejected:
        case proto::MsgType::Reject:
            std::printf("probe: the hub said no: %s\n", reader.GetString().c_str());
            m_done = true;
            break;
        case proto::MsgType::HubPeerUp:
        {
            const uint32_t guestId = reader.GetU32();
            netcrypto::HubExt ext;
            netcrypto::ReadHubExt(reader, ext);
            if (ext.hasCert)
            {
                m_local.SetCert(ext.cert, NowMs());
            }
            std::printf("probe: guest %u came in\n", guestId);
            StartLink(guestId, netcrypto::Role::Host, guestId);
            break;
        }
        case proto::MsgType::HubPayload:
        {
            const uint32_t fromPeer = reader.GetU32();
            auto payload = reader.GetRest();
            if (reader.Ok())
            {
                OnPeerBytes(fromPeer, payload);
            }
            break;
        }
        default: break;
        }
    }

    void StartLink(uint32_t peerId, netcrypto::Role myRole, uint32_t guestHubId)
    {
        netcrypto::Setup setup;
        setup.mine = myRole;
        setup.viaHub = true;
        setup.guestHubId = guestHubId;
        setup.peer = peerId;
        auto& link = m_links[peerId];
        SendPayload(peerId, link.Begin(m_local, setup, NowMs()));
        if (myRole == netcrypto::Role::Guest)
        {
            SendSessionMessage(peerId, nightcoop_mac::net::WriteHello(m_clientId, "macprobe", m_gameVersion, 0));
        }
    }

    void FlushResolvedLink(uint32_t peerId, netcrypto::Link& link)
    {
        if (!link.TakeResolved())
        {
            return;
        }
        std::printf("probe: link with peer %u is %s\n", peerId, link.Describe().c_str());
        for (auto& held : link.TakeHeld())
        {
            SendSessionMessage(peerId, held.data);
        }
        if (auto refusal = link.TakeRefusal(); !refusal.empty())
        {
            SendPayload(peerId, refusal);
        }
    }

    void OnPeerBytes(uint32_t fromPeer, const std::vector<uint8_t>& bytes)
    {
        auto& link = m_links[fromPeer];
        std::vector<uint8_t> opened;
        const auto result = link.Incoming(m_local, bytes.data(), bytes.size(), opened, NowMs());
        FlushResolvedLink(fromPeer, link);
        if (result == netcrypto::Link::In::DeliverAsIs)
        {
            OnSessionMessage(fromPeer, bytes);
        }
        else if (result == netcrypto::Link::In::DeliverOpened)
        {
            OnSessionMessage(fromPeer, opened);
        }
    }

    void OnSessionMessage(uint32_t fromPeer, const std::vector<uint8_t>& message)
    {
        nightcoop_mac::net::Hello hello;
        nightcoop_mac::net::Welcome welcome;
        nightcoop_mac::net::PlayerState state;
        std::string rejectReason;
        if (m_isHost && nightcoop_mac::net::ReadHello(message, hello))
        {
            std::printf("probe: Hello from peer %u: protocol %u client %016llx name '%s' game 0x%016llx install %016llx\n",
                        fromPeer, hello.Protocol, static_cast<unsigned long long>(hello.ClientId), hello.Nickname.c_str(),
                        static_cast<unsigned long long>(hello.GameVersion), static_cast<unsigned long long>(hello.InstallId));
            if (hello.Protocol != nightcoop_mac::net::kProtocolVersion)
            {
                SendSessionMessage(fromPeer, nightcoop_mac::net::WriteReject("protocol mismatch"));
                return;
            }
            SendSessionMessage(fromPeer, nightcoop_mac::net::WriteWelcome({fromPeer, static_cast<int64_t>(SessionClockMs())}));
            std::printf("probe: welcomed peer %u\n", fromPeer);
        }
        else if (!m_isHost && nightcoop_mac::net::ReadWelcome(message, welcome))
        {
            m_myPeerNumber = welcome.PeerNumber;
            m_clockOffsetMs = welcome.HostClockMs - static_cast<int64_t>(NowMs());
            std::printf("probe: welcomed as peer %u, clock offset %lld ms\n", welcome.PeerNumber,
                        static_cast<long long>(m_clockOffsetMs));
            WalkAlongX();
        }
        else if (nightcoop_mac::net::ReadReject(message, rejectReason))
        {
            std::printf("probe: rejected: %s\n", rejectReason.c_str());
            m_done = true;
        }
        else if (m_isHost && nightcoop_mac::net::ReadPlayerState(message, state))
        {
            const bool sane = nightcoop_mac::net::PlayerStateLooksSane(state);
            std::printf("probe: PlayerState from peer %u: pos %.2f %.2f %.2f yaw %.1f speed %.2f t %llu%s\n", state.PeerNumber,
                        state.PositionX, state.PositionY, state.PositionZ, state.Yaw, state.Speed,
                        static_cast<unsigned long long>(state.SessionTimeMs), sane ? "" : " (refused)");
            if (++m_statesSeen == WalkSteps)
            {
                m_done = true;
            }
        }
        else
        {
            std::printf("probe: %zu byte session message from peer %u (type %u)\n", message.size(), fromPeer,
                        message.size() > 4 ? message[4] : 0);
        }
    }

    uint64_t SessionClockMs() const
    {
        return static_cast<uint64_t>(static_cast<int64_t>(NowMs()) + m_clockOffsetMs);
    }

    void WalkAlongX()
    {
        for (int step = 0; step < WalkSteps; ++step)
        {
            nightcoop_mac::net::PlayerState state;
            state.PeerNumber = m_myPeerNumber;
            state.PositionX = -1300.0f + step * 0.5f;
            state.PositionY = 1200.0f;
            state.PositionZ = 18.0f;
            state.Yaw = 90.0f;
            state.Speed = 5.0f;
            state.SessionTimeMs = SessionClockMs();
            SendSessionMessage(HostHubId, nightcoop_mac::net::WritePlayerState(state));
        }
        std::printf("probe: sent %d player states\n", WalkSteps);
        m_done = true;
    }

    bool m_isHost;
    std::string m_joinCode;
    HubAddress m_hub;
    netcrypto::Local m_local;
    std::map<uint32_t, netcrypto::Link> m_links;
    ENetHost* m_client = nullptr;
    ENetPeer* m_hubPeer = nullptr;
    bool m_done = false;
    uint32_t m_myPeerNumber = 0;
    int64_t m_clockOffsetMs = 0;
    int m_statesSeen = 0;
    uint64_t m_clientId = (static_cast<uint64_t>(std::rand()) << 32) ^ static_cast<uint64_t>(NowMs());
    uint64_t m_gameVersion = 0x000300000058B49BULL;
};
}

int main(int argc, char** argv)
{
    if (argc < 3 || (std::string(argv[1]) == "join" && argc < 4))
    {
        std::printf("usage: %s host HUB | %s join CODE HUB\n", argv[0], argv[0]);
        return 1;
    }
    nc::log::SetSink([](nc::log::Level, const char* line) { std::fprintf(stderr, "[nc] %s\n", line); });
    nc::netcrypto::SetDataDirectory("probe-data");
    if (enet_initialize() != 0)
    {
        return 1;
    }
    const bool isHost = std::string(argv[1]) == "host";
    Probe probe(isHost, isHost ? std::string() : argv[2], ParseHub(isHost ? argv[2] : argv[3]));
    const int result = probe.Run();
    enet_deinitialize();
    return result;
}
