// Pure recording-fake tests. No socket, Steam session, certificate or real peer.
#include "gns_transport.hpp"
#include <cassert>
#include <cstring>
#include <limits>

using namespace valdroid::net;

struct TestAuthorization final : SessionAuthorization {
    bool allowed = false;
    bool valid_now(const SteamNetworkingIdentity&, std::uint32_t app) const noexcept override {
        return allowed && app == 892970;
    }
};

static int released = 0;
struct TestMessage : SteamNetworkingMessage_t {};
static void release_test_message(SteamNetworkingMessage_t* message) {
    ++released;
    delete static_cast<TestMessage*>(message);
}

struct RecordingSockets final : Sockets {
    SteamNetworkingIdentity local{}, remote{};
    ESteamNetworkingAvailability availability = k_ESteamNetworkingAvailability_Current;
    ESteamNetworkingConnectionState connection_state = k_ESteamNetworkingConnectionState_Connecting;
    int flags = 0;
    int connects = 0, sends = 0, receives = 0, closes = 0;
    bool identity_ok = true, info_ok = true, reject_connect = false, reject_receive = false;
    EResult send_result = k_EResultOK;
    TestAuthorization* revoke_on_callback = nullptr;
    RecordingSockets() {
        // Synthetic IDs for control-flow tests only. No network/auth use.
        local.SetSteamID64(1);
        remote.SetSteamID64(2);
    }
    bool identity(SteamNetworkingIdentity& out) override { out = local; return identity_ok; }
    ESteamNetworkingAvailability authentication() override { return availability; }
    HSteamNetConnection connect_authenticated(const SteamNetworkingIPAddr&) override {
        ++connects;
        return reject_connect ? k_HSteamNetConnection_Invalid : 7;
    }
    void callbacks() override { if (revoke_on_callback) revoke_on_callback->allowed = false; }
    bool info(HSteamNetConnection handle, SteamNetConnectionInfo_t& out) override {
        assert(handle == 7);
        out = {};
        out.m_eState = connection_state;
        out.m_identityRemote = remote;
        out.m_nFlags = flags;
        return info_ok;
    }
    EResult send(HSteamNetConnection handle, const void* data, std::uint32_t size,
                 bool reliable, std::int64_t* number) override {
        assert(handle == 7 && size == 3 && reliable && std::memcmp(data, "abc", 3) == 0);
        ++sends;
        *number = 42;
        return send_result;
    }
    int receive(HSteamNetConnection handle, SteamNetworkingMessage_t** messages, int count) override {
        assert(handle == 7 && count == 32);
        ++receives;
        if (reject_receive) return -1;
        messages[0] = new TestMessage{};
        messages[0]->m_pfnRelease = release_test_message;
        return 1;
    }
    void close(HSteamNetConnection handle) noexcept override { assert(handle == 7); ++closes; }
};

struct Fixture {
    RecordingSockets sockets;
    TestAuthorization authorization;
    Transport transport{sockets, authorization};
    SteamNetworkingIPAddr address{};
    Fixture() { address.m_port = 2456; }
    void connect() {
        authorization.allowed = true;
        assert(transport.connect(address, sockets.remote));
    }
    void connected() { connect(); sockets.connection_state = k_ESteamNetworkingConnectionState_Connected; }
};

int main() {
    { Fixture f;
      assert(!f.transport.connect(f.address, f.sockets.remote));
      assert(f.transport.error() == Error::NotAuthorized && f.sockets.connects == 0); }
    { Fixture f; f.authorization.allowed = true;
      f.sockets.availability = k_ESteamNetworkingAvailability_CannotTry;
      assert(!f.transport.connect(f.address, f.sockets.remote) && f.sockets.connects == 0); }
    { Fixture f; f.authorization.allowed = true; f.address.m_port = 0;
      assert(!f.transport.connect(f.address, f.sockets.remote));
      assert(f.transport.error() == Error::InvalidArgument && f.sockets.connects == 0); }
    { Fixture f; f.authorization.allowed = true; f.sockets.remote.m_cbSize = 1000000;
      assert(!f.transport.connect(f.address, f.sockets.remote) && f.sockets.connects == 0); }
    { Fixture f; f.sockets.reject_connect = true; f.authorization.allowed = true;
      assert(!f.transport.connect(f.address, f.sockets.remote));
      assert(f.transport.error() == Error::ConnectFailed && f.sockets.closes == 0); }
    { Fixture f; f.connect(); std::int64_t number = 55;
      assert(f.transport.poll() == State::Connecting);
      assert(!f.transport.send("abc", 3, true, &number) && number == -1 && f.sockets.sends == 0);
      assert(f.transport.receive().empty() && f.sockets.receives == 0); }
    { Fixture f; f.connected(); std::int64_t number = -1;
      assert(f.transport.send("abc", 3, true, &number) && number == 42 && f.sockets.sends == 1);
      const int before = released;
      { auto messages = f.transport.receive(); assert(messages.size() == 1 && released == before); }
      assert(released == before + 1);
      f.transport.close(); f.transport.close(); assert(f.sockets.closes == 1); }
    for (int flag : {k_nSteamNetworkConnectionInfoFlags_Unauthenticated,
                     k_nSteamNetworkConnectionInfoFlags_Unencrypted}) {
        Fixture f; f.connected(); f.sockets.flags = flag;
        assert(!f.transport.send("abc", 3, true));
        assert(f.transport.error() == Error::InsecureConnection && f.sockets.sends == 0 && f.sockets.closes == 1);
    }
    { Fixture f; f.connected(); f.sockets.remote.SetSteamID64(3);
      assert(f.transport.poll() == State::Failed && f.transport.error() == Error::UnexpectedPeer);
      assert(f.sockets.closes == 1); }
    { Fixture f; f.connected(); f.authorization.allowed = false;
      assert(f.transport.receive().empty());
      assert(f.transport.error() == Error::NotAuthorized && f.sockets.receives == 0 && f.sockets.closes == 1); }
    { Fixture f; f.connected(); f.sockets.revoke_on_callback = &f.authorization;
      assert(!f.transport.send("abc", 3, true));
      assert(f.transport.error() == Error::NotAuthorized && f.sockets.sends == 0); }
    { Fixture f; f.connected(); f.sockets.connection_state = k_ESteamNetworkingConnectionState_ClosedByPeer;
      assert(f.transport.poll() == State::Failed && f.transport.error() == Error::ConnectionLost); }
    { Fixture f; f.connected(); f.sockets.send_result = k_EResultLimitExceeded;
      std::int64_t number = 77;
      assert(!f.transport.send("abc", 3, true, &number) && number == -1);
      assert(f.transport.error() == Error::SendFailed); }
    { Fixture f; f.connected(); f.sockets.reject_receive = true;
      assert(f.transport.receive().empty() && f.transport.error() == Error::ReceiveFailed); }
    { Fixture f; f.connected();
      assert(!f.transport.send(nullptr, 3, true));
      assert(!f.transport.send("abc", std::numeric_limits<std::size_t>::max(), true));
      assert(f.sockets.sends == 0); }
    { Fixture f; f.connected(); f.connect(); assert(f.sockets.connects == 2 && f.sockets.closes == 1); }
}
