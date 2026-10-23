/* Encode JavaSteam NetworkingCertificateCallback fields for GNS SetCertificate.
 * Wire contract: Valve's steamnetworkingsockets_messages_certs.proto,
 * CMsgSteamDatagramCertificateSigned. This encoder does NOT authenticate anything.
 * GNS must verify the signature/trust, identity, app restrictions and expiry.
 * No private key is accepted or serialized. Keep the original cert bytes intact.
 */
#ifndef VALDROID_STEAM_CERTIFICATE_BRIDGE_H
#define VALDROID_STEAM_CERTIFICATE_BRIDGE_H
#include <stddef.h>
#include <stdint.h>

/* Returns encoded size, or zero for invalid input / insufficient destination.
 * Passing output=NULL and capacity=0 queries the size. Output remains untouched
 * on failure. Inputs and output must not overlap. cap limits malformed responses.
 */
size_t vd_encode_signed_certificate(const uint8_t *certificate, size_t cert_size,
        uint64_t ca_key_id, const uint8_t *signature, size_t signature_size,
        uint8_t *output, size_t capacity);
#endif
