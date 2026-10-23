#include "steam_certificate_bridge.h"
#include <string.h>

static size_t varint_size(size_t value)
{
    size_t size = 1;
    while (value >= 128) { value >>= 7; ++size; }
    return size;
}

static uint8_t *write_varint(uint8_t *out, size_t value)
{
    while (value >= 128) {
        *out++ = (uint8_t)((value & 127) | 128);
        value >>= 7;
    }
    *out++ = (uint8_t)value;
    return out;
}

size_t vd_encode_signed_certificate(const uint8_t *certificate, size_t cert_size,
        uint64_t ca_key_id, const uint8_t *signature, size_t signature_size,
        uint8_t *output, size_t capacity)
{
    /* Steam networking uses Ed25519, whose signature is exactly 64 bytes.
     * 64 KiB is a defensive input limit, not a claim about server acceptance.
     */
    if (!certificate || cert_size == 0 || cert_size > 65536 || !signature
            || signature_size != 64 || ca_key_id == 0)
        return 0;
    const size_t required = 1 + varint_size(cert_size) + cert_size + 9 + 2 + 64;
    if (!output) return capacity == 0 ? required : 0;
    if (capacity < required) return 0;
    uint8_t *p = output;
    *p++ = (4 << 3) | 2; /* bytes cert = 4 */
    p = write_varint(p, cert_size);
    memcpy(p, certificate, cert_size);
    p += cert_size;
    *p++ = (5 << 3) | 1; /* fixed64 ca_key_id = 5, little endian */
    for (unsigned i = 0; i < 8; ++i) *p++ = (uint8_t)(ca_key_id >> (i * 8));
    *p++ = (6 << 3) | 2; /* bytes ca_signature = 6 */
    *p++ = 64;
    memcpy(p, signature, 64);
    return required;
}
