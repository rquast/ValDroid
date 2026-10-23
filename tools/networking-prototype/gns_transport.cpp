#include "gns_transport.hpp"

namespace valdroid::net {
bool Transport::authorized() {
    SteamNetworkingIdentity local{};
    return app_id_ != 0 && sockets_.identity(local)
        && local.m_eType == k_ESteamNetworkingIdentityType_SteamID
        && local.m_cbSize == 8
        && local.GetSteamID64() != 0
        && sockets_.authentication() == k_ESteamNetworkingAvailability_Current
        && authorization_.valid_now(local, app_id_);
}

bool Transport::connect(const SteamNetworkingIPAddr& address,
                        const SteamNetworkingIdentity& expected_peer) {
    close();
    if (address.m_port == 0 || expected_peer.m_eType != k_ESteamNetworkingIdentityType_SteamID
            || expected_peer.m_cbSize != 8
            || expected_peer.GetSteamID64() == 0) {
        fail(Error::InvalidArgument);
        return false;
    }
    if (!authorized()) { fail(Error::NotAuthorized); return false; }
    peer_ = expected_peer;
    handle_ = sockets_.connect_authenticated(address);
    if (handle_ == k_HSteamNetConnection_Invalid) { fail(Error::ConnectFailed); return false; }
    state_ = State::Connecting;
    error_ = Error::None;
    return true; // Accepted by GNS, not proof of a connection.
}

State Transport::poll() {
    if (handle_ == k_HSteamNetConnection_Invalid) return state_;
    if (!authorized()) { fail(Error::NotAuthorized); return state_; }
    sockets_.callbacks();
    // Callback dispatch can report logout/revocation. Recheck before delivering
    // or sending anything in the same poll cycle.
    if (!authorized()) { fail(Error::NotAuthorized); return state_; }
    SteamNetConnectionInfo_t info{};
    if (!sockets_.info(handle_, info)) { fail(Error::ConnectionLost); return state_; }
    switch (info.m_eState) {
    case k_ESteamNetworkingConnectionState_Connecting:
    case k_ESteamNetworkingConnectionState_FindingRoute:
        state_ = State::Connecting;
        break;
    case k_ESteamNetworkingConnectionState_Connected:
        if ((info.m_nFlags & (k_nSteamNetworkConnectionInfoFlags_Unauthenticated
                             | k_nSteamNetworkConnectionInfoFlags_Unencrypted)) != 0) {
            fail(Error::InsecureConnection);
        } else if (info.m_identityRemote.m_eType != k_ESteamNetworkingIdentityType_SteamID
                || info.m_identityRemote.m_cbSize != 8 || !(info.m_identityRemote == peer_)) {
            fail(Error::UnexpectedPeer);
        } else {
            state_ = State::Connected;
        }
        break;
    default:
        fail(Error::ConnectionLost);
        break;
    }
    return state_;
}

bool Transport::send(const void* data, std::size_t size, bool reliable, std::int64_t* message_number) {
    if (message_number) *message_number = -1;
    if ((!data && size != 0) || size > k_cbMaxSteamNetworkingSocketsMessageSizeSend) {
        error_ = Error::InvalidArgument;
        return false;
    }
    if (poll() != State::Connected) {
        if (state_ != State::Failed) error_ = Error::NotConnected;
        return false;
    }
    std::int64_t number = -1;
    EResult result = sockets_.send(handle_, data, static_cast<std::uint32_t>(size), reliable, &number);
    if (result != k_EResultOK) { error_ = Error::SendFailed; return false; }
    if (message_number) *message_number = number;
    error_ = Error::None;
    return true;
}

std::vector<Message> Transport::receive() {
    std::vector<Message> result;
    if (poll() != State::Connected) return result;
    constexpr int limit = 32;
    SteamNetworkingMessage_t* messages[limit]{};
    // Reserve before the backend hands us ownership, so allocation failure cannot
    // leak GNS message objects.
    result.reserve(limit);
    const int count = sockets_.receive(handle_, messages, limit);
    if (count < 0 || count > limit) {
        // Native GNS promises a bounded count. Defend against a broken adapter.
        for (auto* message : messages) if (message) message->Release();
        fail(Error::ReceiveFailed);
        return result;
    }
    for (int i = 0; i < count; ++i) result.emplace_back(messages[i]);
    error_ = Error::None;
    return result;
}

void Transport::close() noexcept {
    if (handle_ != k_HSteamNetConnection_Invalid) sockets_.close(handle_);
    handle_ = k_HSteamNetConnection_Invalid;
    state_ = State::Idle;
    error_ = Error::None;
}
void Transport::fail(Error error) noexcept {
    close();
    state_ = State::Failed;
    error_ = error;
}
} // namespace valdroid::net
