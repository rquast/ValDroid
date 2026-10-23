#include <assert.h>
#include <stddef.h>
#include "../../app/src/main/cpp/steam_networking_offline.h"

int main(void)
{
    struct { uint64_t before; vd_AuthenticationStatus status; uint64_t after; } auth;
    memset(&auth, 0x5a, sizeof(auth));
    const uint64_t guard = auth.before;
    assert(SteamAPI_ISteamNetworkingSockets_InitAuthentication(NULL) == -102);
    assert(SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus(NULL, NULL) == -102);
    assert(SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus(NULL, &auth.status) == -102);
    assert(auth.status.availability == -102);
    assert(strstr(auth.status.debug_message, "not implemented") != NULL);
    assert(auth.status.debug_message[255] == 0);
    assert(auth.before == guard && auth.after == guard);

    struct { uint64_t before; vd_RelayStatus status; uint64_t after; } relay;
    memset(&relay, 0x5a, sizeof(relay));
    assert(SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus(NULL, NULL) == -102);
    assert(SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus(NULL, &relay.status) == -102);
    assert(relay.status.availability == -102);
    assert(relay.status.network_config_availability == -102);
    assert(relay.status.any_relay_availability == -102);
    assert(relay.status.ping_measurement_in_progress == 0);
    assert(relay.status.debug_message[255] == 0);
    assert(relay.before == guard && relay.after == guard);
    int64_t number = 42;
    assert(SteamAPI_ISteamNetworkingSockets_SendMessageToConnection(NULL, 0, NULL, 0, 0, &number) == 3);
    assert(number == -1);
    assert(SteamAPI_ISteamNetworkingSockets_SendMessageToConnection(NULL, 42, "x", 1, 8, NULL) == 3);
    return 0;
}
