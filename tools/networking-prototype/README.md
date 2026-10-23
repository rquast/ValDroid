# Optional authenticated GNS direct-IP transport prototype

This is **not enabled in the APK**, not a `libsteam_api.so` replacement, and not
working Valheim multiplayer. It is a small C++ adapter that calls real Valve
GameNetworkingSockets methods when supplied an initialized GNS instance.

## Dependency and compilation

Public API baseline: Valve GameNetworkingSockets commit
[`d534c19aa760df3fb75fd20db13ba1932b8a5463`](https://github.com/ValveSoftware/GameNetworkingSockets/tree/d534c19aa760df3fb75fd20db13ba1932b8a5463).
The official headers at this exact revision were used for syntax checks. No
third-party wrapper, proprietary SDK/game binary or Valve source copy is bundled.

To compile into a caller, compile `gns_transport.cpp` and `gns_native.cpp` with
C++17, add the pinned GNS `include/` directory, and link the caller against its
reviewed GNS build and that build's dependencies. This prototype does not install
or run GNS's build scripts. A real link and Android ARM64 build remain required.

Host compile/control-flow checks with a previously obtained header checkout:

```sh
python3 tools/networking-prototype/run_tests.py --gns-include /path/to/GameNetworkingSockets/include
```

In the current ptrace sandbox only, prefix the command with
`ASAN_OPTIONS=detect_leaks=0`; retain default leak checking elsewhere.

## Actual behavior

- `NativeSockets` calls `ConnectByIPAddress`, `SendMessageToConnection`,
  `ReceiveMessagesOnConnection`, `GetConnectionInfo`, `RunCallbacks` and
  `CloseConnection` on a real `ISteamNetworkingSockets` interface.
- It sets `IP_AllowWithoutAuth=0`, `IPLocalHost_AllowWithoutAuth=0` and
  `Unencrypted=0` on each connection. **Both** IP-auth settings are necessary:
  GNS applies the more permissive value for localhost.
- `Transport` does not send or deliver payloads until GNS reports Connected,
  authenticated and encrypted, and the peer identity matches the expected
  server SteamID supplied by the caller. A successful `connect` return means
  only that GNS created a handle, not that a server was reached.
- A mandatory `SessionAuthorization` policy is checked before permitting connect or
  payload operations, including again after callback dispatch. Invalid send
  arguments are rejected before this check and cannot transmit data. Failure closes
  the connection. There is no built-in policy that simply permits connections.
- Messages use RAII to invoke GNS `Release` exactly once. Caller must keep the
  GNS instance alive until all returned messages are destroyed.
- Single connection, single caller thread. Serialize operations; do not reenter
  the adapter from GNS callbacks. Explicit close and destruction close without
  lingering; reconnect first closes the old connection.

## Integration contract: still missing

The caller must initialize GNS, configure the correct app identity, obtain a
legitimate Steam-signed networking certificate matching GNS's in-memory key,
and implement `SessionAuthorization::valid_now`. That policy must independently
verify CA trust, signature, expiry, certificate identity and app restriction,
plus the owned game's registered auth ticket and current login/revocation state.
It must not treat GNS `SetCertificate` or `GetAuthenticationStatus` alone as
cryptographic verification. No certificate verifier is implemented here.

Use the existing JavaSteam certificate request and session-ticket APIs rather
than embedding account credentials. The certificate bridge in
`app/src/main/cpp/steam_certificate_bridge.c` can encode the signed reply but
does not validate it. Preserve the private key in the original GNS process.

Construct `NativeSockets` with the initialized interface; construct `Transport`
with that wrapper and the real session policy. Supply the server address and
its known expected Steam identity, then call `poll` regularly. Poll pumps GNS
callbacks. Send or receive only after `State::Connected`. This requires a known
server identity and intentionally does not accept arbitrary/unverified peers.

Still needed for Valheim: its exact 1.0.20 Steamworks.NET ABI and callback bridge,
matching ticket handshake, native/Java process IPC, owned-account integration,
and an unmodified dedicated-server test. No lobby discovery, P2P signaling,
SDR, PlayFab crossplay, game packet framing or game protocol is supplied.

## Verification and its limits

Passed locally:

- Both production `.cpp` files compile syntactically against the exact official
  API headers with `-Wall -Wextra -Werror`.
- Recording-fake tests pass ASan/UBSan: authorization failure before connect,
  invalid inputs, connecting-state restrictions, sends, message release,
  wrong peer, unencrypted/unauthenticated flags, revocation during callbacks,
  disconnects, send/receive failures, repeated close and reconnect.

The fake deliberately uses synthetic identities only inside tests. It opens no
socket and proves neither Steam authentication nor transport interoperability.
The native backend has not been linked to a real GNS library or exercised on a
device/server. Tests do not establish APK ABI compatibility or multiplayer.

Primary contracts:

- [ISteamNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets/blob/d534c19aa760df3fb75fd20db13ba1932b8a5463/include/steam/isteamnetworkingsockets.h)
- [Configuration, identity, state and message ownership types](https://github.com/ValveSoftware/GameNetworkingSockets/blob/d534c19aa760df3fb75fd20db13ba1932b8a5463/include/steam/steamnetworkingtypes.h)
