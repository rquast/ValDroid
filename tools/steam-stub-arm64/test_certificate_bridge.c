#include <assert.h>
#include <string.h>
#include "../../app/src/main/cpp/steam_certificate_bridge.h"

int main(void)
{
    uint8_t cert[128], sig[64], out[256];
    memset(cert, 0xa5, sizeof(cert)); memset(sig, 0x5a, sizeof(sig));
    const uint64_t id = UINT64_C(0x0807060504030201);
    size_t size = vd_encode_signed_certificate(cert, 2, id, sig, 64, NULL, 0);
    assert(size == 79);
    memset(out, 0xcc, sizeof(out));
    assert(vd_encode_signed_certificate(cert, 2, id, sig, 64, out, size - 1) == 0);
    for (size_t i = 0; i < sizeof(out); ++i) assert(out[i] == 0xcc);
    assert(vd_encode_signed_certificate(cert, 2, id, sig, 64, out, size) == size);
    const uint8_t expected[] = {0x22, 2, 0xa5, 0xa5, 0x29, 1, 2, 3, 4, 5, 6, 7, 8, 0x32, 64};
    assert(memcmp(out, expected, sizeof(expected)) == 0);
    assert(memcmp(out + sizeof(expected), sig, 64) == 0);
    assert(out[size] == 0xcc);
    size = vd_encode_signed_certificate(cert, 128, id, sig, 64, out, sizeof(out));
    assert(size == 206 && out[0] == 0x22 && out[1] == 0x80 && out[2] == 1);
    assert(memcmp(out + 3, cert, 128) == 0);
    assert(vd_encode_signed_certificate(NULL, 1, id, sig, 64, out, sizeof(out)) == 0);
    assert(vd_encode_signed_certificate(cert, 0, id, sig, 64, out, sizeof(out)) == 0);
    assert(vd_encode_signed_certificate(cert, 65537, id, sig, 64, out, sizeof(out)) == 0);
    assert(vd_encode_signed_certificate(cert, 1, 0, sig, 64, out, sizeof(out)) == 0);
    assert(vd_encode_signed_certificate(cert, 1, id, NULL, 64, out, sizeof(out)) == 0);
    assert(vd_encode_signed_certificate(cert, 1, id, sig, 63, out, sizeof(out)) == 0);
    assert(vd_encode_signed_certificate(cert, 1, id, sig, 64, NULL, 1) == 0);
    return 0;
}
