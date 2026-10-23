// Controlled GNS loopback transport fixture, NOT a Valheim/mock-login server.
// Never relax authentication, fabricate Steam identities, or use outside hosts.
#include "gns_transport.hpp"
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace valdroid::net;
using Clock = std::chrono::steady_clock;
static void require(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
struct Event {
    HSteamNetConnection handle;
    ESteamNetworkingConnectionState state;
    int reason;
};
static std::vector<Event> events;
static void status_changed(SteamNetConnectionStatusChangedCallback_t* event) {
    events.push_back({event->m_hConn, event->m_info.m_eState, event->m_info.m_eEndReason});
}
struct Lifetime {
    Lifetime() {
        SteamNetworkingErrMsg error{};
        require(GameNetworkingSockets_Init(nullptr, error), "GNS init failed");
    }
    ~Lifetime() { GameNetworkingSockets_Kill(); }
};
struct Pair {
    ISteamNetworkingSockets& api;
    HSteamNetConnection a = k_HSteamNetConnection_Invalid;
    HSteamNetConnection b = k_HSteamNetConnection_Invalid;
    explicit Pair(ISteamNetworkingSockets& sockets) : api(sockets) {
        require(api.CreateSocketPair(&a, &b, true, nullptr, nullptr), "UDP loopback pair failed");
    }
    ~Pair() {
        if (a) api.CloseConnection(a, k_ESteamNetConnectionEnd_App_Generic, "fixture cleanup", false);
        if (b) api.CloseConnection(b, k_ESteamNetConnectionEnd_App_Generic, "fixture cleanup", false);
    }
};
static void transfer(NativeSockets& sockets, HSteamNetConnection from,
                     HSteamNetConnection to, const char* payload) {
    std::int64_t number = -1;
    const auto size = static_cast<std::uint32_t>(std::strlen(payload));
    require(sockets.send(from, payload, size, true, &number) == k_EResultOK && number > 0,
            "Native adapter UDP send failed");
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
        sockets.callbacks();
        SteamNetworkingMessage_t* raw = nullptr;
        const int count = sockets.receive(to, &raw, 1);
        require(count >= 0, "Native adapter UDP receive failed");
        if (count == 1) {
            Message message(raw);
            require(message && message->m_cbSize == static_cast<int>(size), "UDP payload size mismatch");
            require(std::memcmp(message->m_pData, payload, size) == 0, "UDP payload bytes mismatch");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("UDP delivery exceeded five-second deadline");
}
int main() {
    try {
        Lifetime lifetime;
        auto* api = SteamNetworkingSockets();
        require(api != nullptr, "Missing GNS interface");
        require(SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(status_changed),
                "Callback registration failed");
        NativeSockets native(*api);
        require(native.authentication() != k_ESteamNetworkingAvailability_Current,
                "Credential-free fixture unexpectedly authenticated");

        // Pinned open-source GNS cannot create a strict listener without its
        // certificate authority capability. This asserts LOCAL rejection, not
        // reception of a remote server's auth rejection. Use a nonzero port:
        // this revision rejects port zero even before checking authentication.
        SteamNetworkingIPAddr endpoint{};
        endpoint.SetIPv4(0x7f000001, 29457);
        SteamNetworkingConfigValue_t options[3];
        options[0].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 0);
        options[1].SetInt32(k_ESteamNetworkingConfig_IPLocalHost_AllowWithoutAuth, 0);
        options[2].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
        const auto listener = api->CreateListenSocketIP(endpoint, 3, options);
        if (listener != k_HSteamListenSocket_Invalid) api->CloseListenSocket(listener);
        require(listener == k_HSteamListenSocket_Invalid,
                "Strict listener unexpectedly available without cert authority");
        const auto strict_client = native.connect_authenticated(endpoint);
        if (strict_client != k_HSteamNetConnection_Invalid) native.close(strict_client);
        require(strict_client == k_HSteamNetConnection_Invalid,
                "Strict client unexpectedly accepted missing local identity");
        native.callbacks();
        for (const auto& event : events)
            require(event.state != k_ESteamNetworkingConnectionState_Connected,
                    "Strict credential-free path reached Connected");
        events.clear();

        // Official test API with real UDP loopback, not listener handshaking.
        // Identities are left unset; this is NOT an authenticated Steam session.
        // Exercise the real NativeSockets send/receive/callback wrapper, not the
        // higher-level Transport session gate (which must continue to reject).
        Pair pair(*api);
        for (const auto handle : {pair.a, pair.b}) {
            SteamNetConnectionInfo_t info{};
            require(native.info(handle, info), "Loopback connection info unavailable");
            require(info.m_eState == k_ESteamNetworkingConnectionState_Connected,
                    "Loopback test pair not connected");
            require(info.m_addrRemote.IsLocalHost() && info.m_addrRemote.m_port != 0,
                    "Loopback pair endpoint not actual localhost UDP");
        }
        transfer(native, pair.a, pair.b, "valdroid-loopback-udp-request");
        transfer(native, pair.b, pair.a, "valdroid-loopback-udp-response");
        const auto closed_handle = pair.a;
        native.close(pair.a);
        pair.a = k_HSteamNetConnection_Invalid;
        bool observed_peer_close = false;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (Clock::now() < deadline && !observed_peer_close) {
            native.callbacks();
            for (const auto& event : events) {
                if (event.handle == pair.b && event.state == k_ESteamNetworkingConnectionState_ClosedByPeer) {
                    require(event.reason == k_ESteamNetConnectionEnd_App_Generic, "Peer close reason mismatch");
                    observed_peer_close = true;
                }
            }
            if (!observed_peer_close) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(observed_peer_close, "Peer close callback exceeded five-second deadline");
        std::int64_t number = 99;
        require(native.send(closed_handle, "forbidden", 9, true, &number) != k_EResultOK,
                "Native adapter accepted send on closed handle");
        require(number == -1, "Failed send leaked stale message number");
        SteamNetworkingMessage_t* raw = nullptr;
        require(native.receive(closed_handle, &raw, 1) < 0 && raw == nullptr,
                "Closed handle receive did not fail cleanly");
        std::cout << "PASS: strict listener/client rejected locally without credentials; "
                     "real UDP loopback pair request/response via NativeSockets, message release, "
                     "peer-close callback and closed-handle errors.\n"
                     "NOT TESTED: successful listener handshake, authenticated UDP, Steam login, "
                     "packet-loss timeout, Android or Valheim protocol/server join.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Loopback fixture failure: " << error.what() << '\n';
        return 1;
    }
}
