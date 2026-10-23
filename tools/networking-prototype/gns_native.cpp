#include "gns_transport.hpp"

namespace valdroid::net {
bool NativeSockets::identity(SteamNetworkingIdentity& out) { return sockets_.GetIdentity(&out); }
ESteamNetworkingAvailability NativeSockets::authentication() {
    return sockets_.GetAuthenticationStatus(nullptr);
}
HSteamNetConnection NativeSockets::connect_authenticated(const SteamNetworkingIPAddr& address) {
    SteamNetworkingConfigValue_t options[3];
    options[0].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 0);
    // The localhost setting takes the MORE permissive value: set both to zero.
    options[1].SetInt32(k_ESteamNetworkingConfig_IPLocalHost_AllowWithoutAuth, 0);
    options[2].SetInt32(k_ESteamNetworkingConfig_Unencrypted, 0);
    return sockets_.ConnectByIPAddress(address, 3, options);
}
void NativeSockets::callbacks() { sockets_.RunCallbacks(); }
bool NativeSockets::info(HSteamNetConnection handle, SteamNetConnectionInfo_t& out) {
    return sockets_.GetConnectionInfo(handle, &out);
}
EResult NativeSockets::send(HSteamNetConnection handle, const void* data, std::uint32_t size,
                            bool reliable, std::int64_t* number) {
    int64 native_number = -1;
    const EResult result = sockets_.SendMessageToConnection(handle, data, size,
        reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable, &native_number);
    if (number) *number = static_cast<std::int64_t>(native_number);
    return result;
}
int NativeSockets::receive(HSteamNetConnection handle, SteamNetworkingMessage_t** messages, int count) {
    return sockets_.ReceiveMessagesOnConnection(handle, messages, count);
}
void NativeSockets::close(HSteamNetConnection handle) noexcept {
    sockets_.CloseConnection(handle, k_ESteamNetConnectionEnd_App_Generic,
                            "ValDroid transport closed", false);
}
} // namespace valdroid::net
