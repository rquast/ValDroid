/* Offline Steam networking boundary. This is NOT a network transport.
 * ABI layouts follow Valve's public isteamnetworkingsockets.h / isteamnetworkingutils.h.
 * Keep explicit signatures here: the generated import inventory has return types only.
 */
#ifndef VALDROID_STEAM_NETWORKING_OFFLINE_H
#define VALDROID_STEAM_NETWORKING_OFFLINE_H
#include <stdint.h>
#include <string.h>

#ifndef VD_EXPORT
#define VD_EXPORT __attribute__((visibility("default")))
#endif

enum { VD_NETWORK_CANNOT_TRY = -102, VD_NO_CONNECTION = 3 };
typedef struct {
    int32_t availability;
    char debug_message[256];
} vd_AuthenticationStatus;
typedef struct {
    int32_t availability;
    int32_t ping_measurement_in_progress;
    int32_t network_config_availability;
    int32_t any_relay_availability;
    char debug_message[256];
} vd_RelayStatus;
_Static_assert(sizeof(vd_AuthenticationStatus) == 260, "Steam auth status ABI");
_Static_assert(sizeof(vd_RelayStatus) == 272, "Steam relay status ABI");

VD_EXPORT int32_t SteamAPI_ISteamNetworkingSockets_InitAuthentication(void *self)
{
    (void)self;
    return VD_NETWORK_CANNOT_TRY;
}

VD_EXPORT int32_t SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus(
        void *self, vd_AuthenticationStatus *status)
{
    (void)self;
    if (status) {
        memset(status, 0, sizeof(*status));
        status->availability = VD_NETWORK_CANNOT_TRY;
        const char message[] = "ValDroid offline backend: Steam networking is not implemented";
        memcpy(status->debug_message, message, sizeof(message));
    }
    return VD_NETWORK_CANNOT_TRY;
}

VD_EXPORT int32_t SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus(
        void *self, vd_RelayStatus *status)
{
    (void)self;
    if (status) {
        memset(status, 0, sizeof(*status));
        status->availability = VD_NETWORK_CANNOT_TRY;
        status->network_config_availability = VD_NETWORK_CANNOT_TRY;
        status->any_relay_availability = VD_NETWORK_CANNOT_TRY;
        const char message[] = "ValDroid offline backend: Steam Datagram Relay is unavailable";
        memcpy(status->debug_message, message, sizeof(message));
    }
    return VD_NETWORK_CANNOT_TRY;
}

VD_EXPORT int32_t SteamAPI_ISteamNetworkingSockets_SendMessageToConnection(
        void *self, uint32_t connection, const void *data, uint32_t size,
        int32_t flags, int64_t *message_number)
{
    (void)self; (void)connection; (void)data; (void)size; (void)flags;
    if (message_number) *message_number = -1;
    return VD_NO_CONNECTION;
}
#endif
