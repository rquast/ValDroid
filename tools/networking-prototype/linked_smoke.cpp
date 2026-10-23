// Real linked-library smoke test. No account credentials, external server or
// unauthenticated-network setting changes. SocketPair(false) is in-process only.
#include "gns_transport.hpp"
#include <steam/steamnetworkingsockets.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace valdroid::net;

static void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}

struct GNSLifetime {
    GNSLifetime() {
        SteamNetworkingErrMsg error{};
        require(GameNetworkingSockets_Init(nullptr, error), "GNS initialization failed");
    }
    ~GNSLifetime() { GameNetworkingSockets_Kill(); }
};

struct NoSession final : SessionAuthorization {
    bool valid_now(const SteamNetworkingIdentity&, std::uint32_t) const noexcept override {
        return false; // No session/certificate/ticket is supplied by this test.
    }
};

struct SocketPair {
    ISteamNetworkingSockets& sockets;
    HSteamNetConnection a = k_HSteamNetConnection_Invalid;
    HSteamNetConnection b = k_HSteamNetConnection_Invalid;
    explicit SocketPair(ISteamNetworkingSockets& api) : sockets(api) {
        // false explicitly selects internal buffers, not UDP/network loopback.
        require(sockets.CreateSocketPair(&a, &b, false, nullptr, nullptr),
                "In-process GNS socket pair creation failed");
    }
    ~SocketPair() {
        if (a) sockets.CloseConnection(a, k_ESteamNetConnectionEnd_App_Generic, "smoke complete", false);
        if (b) sockets.CloseConnection(b, k_ESteamNetConnectionEnd_App_Generic, "smoke complete", false);
    }
};

static void transfer(ISteamNetworkingSockets& sockets, HSteamNetConnection from,
                     HSteamNetConnection to, const char* payload) {
    const auto size = static_cast<uint32>(std::strlen(payload));
    int64 number = -1;
    require(sockets.SendMessageToConnection(from, payload, size,
                k_nSteamNetworkingSend_Reliable, &number) == k_EResultOK,
            "Real GNS send failed");
    require(number > 0, "Real GNS did not assign a message number");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        sockets.RunCallbacks();
        SteamNetworkingMessage_t* raw = nullptr;
        const int count = sockets.ReceiveMessagesOnConnection(to, &raw, 1);
        require(count >= 0, "Real GNS receive failed");
        if (count == 1) {
            Message message(raw); // Invoke the real GNS Release callback.
            require(message && message->m_cbSize == static_cast<int>(size), "Payload size mismatch");
            require(std::memcmp(message->m_pData, payload, size) == 0, "Payload bytes mismatch");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("In-process GNS receive timed out");
}

int main() {
    try {
        GNSLifetime lifetime;
        auto* api = SteamNetworkingSockets();
        require(api != nullptr, "GNS interface is null");
        SteamNetworkingIdentity local{};
        // A credential-free instance may legitimately have no usable identity.
        (void)api->GetIdentity(&local);
        // No public or private identity/key bytes are logged or written to disk.
        SteamNetAuthenticationStatus_t auth{};
        const auto availability = api->GetAuthenticationStatus(&auth);
        require(availability != k_ESteamNetworkingAvailability_Current,
                "Unexpected authenticated state in credential-free process");

        SteamNetworkingErrMsg error{};
        int request_size = 0;
        require(api->GetCertificateRequest(&request_size, nullptr, error),
                "Certificate request size query failed");
        require(request_size > 0 && request_size <= 65536, "Unexpected certificate request size");
        std::vector<unsigned char> request(static_cast<std::size_t>(request_size));
        require(api->GetCertificateRequest(&request_size, request.data(), error),
                "Certificate request generation failed");
        require(request_size > 0 && static_cast<std::size_t>(request_size) <= request.size(),
                "Certificate request returned an invalid size");
        // GNS retains the ephemeral private key internally until shutdown.
        require(api->GetAuthenticationStatus(nullptr) != k_ESteamNetworkingAvailability_Current,
                "Unsigned request must not establish authentication");

        NativeSockets native(*api);
        (void)native.identity(local);
        require(native.authentication() != k_ESteamNetworkingAvailability_Current,
                "Native adapter unexpectedly reports authenticated state");
        NoSession no_session;
        Transport guarded(native, no_session);
        SteamNetworkingIPAddr address{};
        address.SetIPv4(0x7f000001, 2456);
        SteamNetworkingIdentity no_server_identity{};
        require(!guarded.connect(address, no_server_identity), "Missing peer identity was accepted");
        require(guarded.error() == Error::InvalidArgument, "Expected missing-identity rejection");
        std::int64_t rejected_number = 99;
        require(!guarded.send("forbidden", 9, true, &rejected_number) && rejected_number == -1,
                "Guarded adapter sent without an authenticated session");
        require(guarded.receive().empty(), "Guarded adapter delivered without a session");

        // Exercise real library message allocation, numbering, delivery and release.
        // The pair intentionally bypasses Transport's Steam-session gate because
        // it is a distinct intra-process API test, not an authenticated connection.
        {
            SocketPair pair(*api);
            transfer(*api, pair.a, pair.b, "valdroid-real-gns-ping");
            transfer(*api, pair.b, pair.a, "valdroid-real-gns-pong");
        }
        std::cout << "PASS: real GNS init, unsigned certificate request, fail-closed adapter, "
                     "in-process two-way message delivery/release and cleanup.\n"
                     "NOT TESTED: authenticated UDP, Steam login/tickets, Android or Valheim servers.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GNS smoke failure: " << error.what() << '\n';
        return 1;
    }
}
