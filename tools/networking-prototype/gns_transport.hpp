#pragma once

#include <steam/isteamnetworkingsockets.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace valdroid::net {

// An implementation must independently verify the active certificate's signature,
// trusted CA, expiry, app ID and local identity, plus the owned game's registered
// auth ticket. GNS SetCertificate/GetAuthenticationStatus alone are insufficient.
// Checked before permitting connect or payload operations. Invalid sends fail
// before this check without transmitting data. No permissive default.
class SessionAuthorization {
public:
    virtual ~SessionAuthorization() = default;
    virtual bool valid_now(const SteamNetworkingIdentity& local,
                           std::uint32_t app_id) const noexcept = 0;
};

// Narrow injection boundary. NativeSockets delegates to real GNS; tests inject a
// recording fake. Fake test success is never evidence of transport/authentication.
class Sockets {
public:
    virtual ~Sockets() = default;
    virtual bool identity(SteamNetworkingIdentity& out) = 0;
    virtual ESteamNetworkingAvailability authentication() = 0;
    virtual HSteamNetConnection connect_authenticated(const SteamNetworkingIPAddr& address) = 0;
    virtual void callbacks() = 0;
    virtual bool info(HSteamNetConnection handle, SteamNetConnectionInfo_t& out) = 0;
    virtual EResult send(HSteamNetConnection handle, const void* data, std::uint32_t size,
                         bool reliable, std::int64_t* number) = 0;
    virtual int receive(HSteamNetConnection handle, SteamNetworkingMessage_t** messages, int count) = 0;
    virtual void close(HSteamNetConnection handle) noexcept = 0;
};

class NativeSockets final : public Sockets {
public:
    // Caller owns an initialized GNS instance and outlives this wrapper/Transport.
    explicit NativeSockets(ISteamNetworkingSockets& sockets) : sockets_(sockets) {}
    bool identity(SteamNetworkingIdentity& out) override;
    ESteamNetworkingAvailability authentication() override;
    HSteamNetConnection connect_authenticated(const SteamNetworkingIPAddr& address) override;
    void callbacks() override;
    bool info(HSteamNetConnection handle, SteamNetConnectionInfo_t& out) override;
    EResult send(HSteamNetConnection handle, const void* data, std::uint32_t size,
                 bool reliable, std::int64_t* number) override;
    int receive(HSteamNetConnection handle, SteamNetworkingMessage_t** messages, int count) override;
    void close(HSteamNetConnection handle) noexcept override;
private:
    ISteamNetworkingSockets& sockets_;
};

struct MessageDeleter {
    void operator()(SteamNetworkingMessage_t* message) const noexcept {
        if (message) message->Release();
    }
};
using Message = std::unique_ptr<SteamNetworkingMessage_t, MessageDeleter>;

enum class State { Idle, Connecting, Connected, Failed };
enum class Error { None, InvalidArgument, NotAuthorized, ConnectFailed,
                   ConnectionLost, UnexpectedPeer, InsecureConnection, NotConnected,
                   SendFailed, ReceiveFailed };

// Single-threaded direct-IP transport; caller must serialize every operation and
// keep Sockets and SessionAuthorization alive. Not a Steamworks.NET ABI shim.
class Transport final {
public:
    Transport(Sockets& sockets, const SessionAuthorization& authorization,
              std::uint32_t app_id = 892970)
        : sockets_(sockets), authorization_(authorization), app_id_(app_id) {}
    ~Transport() { close(); }
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    bool connect(const SteamNetworkingIPAddr& address,
                 const SteamNetworkingIdentity& expected_peer);
    State poll();
    bool send(const void* data, std::size_t size, bool reliable,
              std::int64_t* message_number = nullptr);
    std::vector<Message> receive();
    void close() noexcept;
    State state() const noexcept { return state_; }
    Error error() const noexcept { return error_; }
private:
    bool authorized();
    void fail(Error error) noexcept;
    Sockets& sockets_;
    const SessionAuthorization& authorization_;
    std::uint32_t app_id_;
    HSteamNetConnection handle_ = k_HSteamNetConnection_Invalid;
    SteamNetworkingIdentity peer_{};
    State state_ = State::Idle;
    Error error_ = Error::None;
};
} // namespace valdroid::net
