# Dedicated-server networking investigation

Status: research, an isolated certificate-encoding component and offline-boundary fixes only. **No dedicated-server connection has
been demonstrated, and this patch does not implement multiplayer.**

Target: latest stable [Valheim 1.0.20](https://www.valheimgame.com/news/patch-1-0-20/), released 15 October 2026, verified against Iron Gate news on 23 October. The matching managed ABI has not yet been obtained.

Inspected baseline: `5517dca13cb265b9df01d8c26e9a2288dffc3bc6` (23 October 2026).

## Why it does not work

The native engine uses ARM64 Mono and the APK's `libsteam_api.so`. The CMake target
builds `app/src/main/cpp/steam_api_stub.c`, generated from the import TSV. This is
explicitly an offline implementation, not Valve's Steamworks runtime:

- `SteamAPI_Init` reports success and interface accessors return dummy storage.
- The SteamID is fixed and `BLoggedOn` returns false.
- Connection creation returns invalid handle zero; receive functions return zero;
  callback dispatch does nothing. There is no reliable UDP transport behind these calls.
- Before this patch, `InitAuthentication` incorrectly returned `Current` (100).
- The TSV records return types, **not argument signatures**. Its generated `(void)`
  exports are not safe forwarding adapters. A real backend needs exact parameters,
  struct packing, callbacks, message release ownership and interface versions.

The launcher already depends on JavaSteam 1.8.0 for downloads/cloud. That session
does not, by itself, implement the game's Steam networking and game-authentication
interfaces. The native Unity player runs in a separate process, so session lifetime
and any authenticated IPC bridge require explicit design.

## This first patch

`steam_networking_offline.h` gives four networking exports real signatures, fills
status outputs, reports `CannotTry`, and rejects sends with `NoConnection` and an
invalid message number. Offline startup behavior is otherwise intended to remain unchanged; game startup has not been tested. The
generator now excludes those typed exports and checks that its inventory includes
them. Both generator and generated output are changed together.

The audit tool inventories 168 networking/authentication/callback imports from the
current TSV. It optionally compares a native library's text symbol list without
loading that library. This helps detect missing exports, but it cannot establish
argument ABI compatibility, Steam authentication, or Valheim interoperability.

## Candidate implementations and their limits

1. [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)
   (BSD-3-Clause): best transport foundation for a direct-IP prototype. Reliable and
   unreliable UDP, message ownership and connection callbacks are implemented.
   Open source does not provide the complete Steam service, SDR and authentication
   environment. A successful two-peer GNS demo is not a Valheim server result.
2. [ValveSockets-CSharp](https://github.com/nxrighthere/ValveSockets-CSharp) (MIT):
   useful managed/native ABI reference, not a drop-in replacement for Valheim's
   existing Steamworks.NET imports.
3. [GameNetworkingSockets.Net Android build](https://github.com/vrimar/GameNetworkingSockets.Net/blob/main/build/build-native-android.sh):
   ARM64/NDK build reference. Its modern .NET wrapper is not a Unity Mono drop-in.
   Do not copy its self-signed-root configuration into an authenticated Steam path.
4. [SteamKit2](https://github.com/SteamRE/SteamKit) (LGPL-2.1): current
   [networking-certificate request](https://github.com/SteamRE/SteamKit/blob/master/SteamKit2/SteamKit2/Steam/Handlers/SteamNetworking/SteamNetworking.cs)
   and [ownership/auth-ticket implementation](https://github.com/SteamRE/SteamKit/blob/master/SteamKit2/SteamKit2/Steam/Handlers/SteamAuthTicket/SteamAuthTicket.cs)
   are research leads for legitimate authenticated sessions. Check JavaSteam parity
   first. Certificate format, app identity, trust roots, registration and expiry
   must be proved against the chosen GNS build and an unmodified server.
5. [Facepunch.Steamworks](https://github.com/Facepunch/Facepunch.Steamworks) (MIT):
   useful API examples but still needs native Steam; not an independent transport.

There is another route worth testing: [Valve's Steam Frame documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
says Steamworks SDK 1.63+ includes Android ARM64 and Linux ARM64 libraries. It would
be incorrect to claim ARM64 binaries do not exist. Availability for Steam Frame
does not prove initialization on an ordinary Android phone without Steam runtime
services. Obtain any SDK lawfully; do not commit proprietary SDK/game binaries.

## Implementation sequence and acceptance gates

1. **Observe the actual game ABI.** With an owned matching Valheim installation,
   inspect Steamworks.NET imports and trace a dedicated direct-IP join. Determine
   whether it ultimately uses `ConnectByIPAddress` or Steam identity/P2P routing.
   Record interface version and layouts. Avoid publishing proprietary assemblies.
2. **Transport proof.** Build reviewed GNS for Android ARM64. Test connect, send,
   receive, message release, disconnect, reconnect, timeouts and reliable ordering
   between two controlled peers. Keep this explicit developer-only functionality.
3. **Typed adapter.** Implement only the observed call set against public headers.
   Keep genuine interfaces separate from dummy offline interfaces. Implement
   `SteamAPI_RunCallbacks`, connection status callbacks, polling and free/release
   semantics before integrating gameplay. Do not patch the generator to forward
   arbitrary untyped calls or return fabricated successful authentication.
4. **Authentication proof.** Use an owned account's legitimate session, Steam-signed
   networking certificate and real game-auth ticket. Verify ticket acceptance on
   the server, expiry/revocation and logout cleanup. Never persist private keys or
   tickets in logs; do not disable server ownership checks or trusted roots.
5. **End-to-end.** On a matching unmodified dedicated server, test password/version
   failures, accepted authentication, joining a world, movement, inventory/world
   synchronization, reconnects and save integrity. Capture redacted client and
   server logs. Only then claim dedicated-server support.
6. **Separate projects.** SDR, lobby discovery and PlayFab crossplay require their
   own verified implementations and tests. A direct-IP result does not cover them.

## Verification performed and remaining limits

- Four newly authored Python audit tests pass.
- Isolated newly authored C status/send tests pass with AddressSanitizer and
  UndefinedBehaviorSanitizer. LeakSanitizer is disabled because the sandbox's
  ptrace environment prevents it running; these tests allocate no heap memory.
- After execution approval, the real generator ran successfully (944 exports),
  and generated-file parity passed. The full generated host shared library compiled
  with warnings-as-errors and loaded successfully; startup/offline/status checks
  passed and all 168 inventoried networking/authentication exports were present.
- No full Gradle/NDK build, APK packaging, device test, Steam login, GNS peer test
  or live Valheim connection was performed. No authentication credentials acquired.

Local isolated tests:

```sh
python3 -m unittest discover -s tools/steam-stub-arm64 -p 'test_audit_networking.py' -v
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tools/steam-stub-arm64/test_networking_offline.c -o /tmp/test_networking_offline
ASAN_OPTIONS=detect_leaks=0 /tmp/test_networking_offline
python3 tools/steam-stub-arm64/audit_networking.py
```

The next decisive evidence is the owned game's actual join call path plus an
authenticated transport experiment. More zero-return stubs cannot supply either.

## Concrete authentication bridge component

`steam_certificate_bridge.c` is a standalone, currently unwired native component
that serializes the exact JavaSteam networking-certificate reply into Valve's
`CMsgSteamDatagramCertificateSigned` wire format. It preserves signed certificate
bytes unchanged, writes the fixed64 CA key ID little-endian, and adds the 64-byte
Ed25519 signature. It cannot accept or serialize private keys. Null inputs,
oversized certificates, wrong signature lengths and insufficient output buffers
are rejected without touching the output. Isolated byte-level and bounds tests
pass with ASan/UBSan. It is not yet linked into the APK and performs no networking.

The exact JavaSteam dependency version, **1.8.0**, already supplies
[certificate requests](https://github.com/Longi94/JavaSteam/blob/1.8.0/src/main/java/in/dragonbra/javasteam/steam/handlers/steamnetworking/SteamNetworking.kt),
[certificate reply fields](https://github.com/Longi94/JavaSteam/blob/1.8.0/src/main/java/in/dragonbra/javasteam/steam/handlers/steamnetworking/callback/NetworkingCertificateCallback.kt),
and [ownership-checked auth-session tickets](https://github.com/Longi94/JavaSteam/blob/1.8.0/src/main/java/in/dragonbra/javasteam/steam/handlers/steamauthticket/SteamAuthTicket.kt).
A separate .NET SteamKit runtime is therefore unnecessary for this experiment.

Proposed next experiment: let GNS generate an ephemeral key via
`GetCertificateRequest`; parse its public key and request the Steam certificate
through the existing JavaSteam session for app 892970. Keep the private key in
GNS's process. Encode the three returned certificate fields with this component
and supply them to `SetCertificate`. Separately obtain/register a genuine game
session ticket for the server's application-level authentication.

**Security gate:** [GNS InternalSetCertificate](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/src/steamnetworkingsockets/clientlib/csteamnetworkingsockets.cpp)
logs a warning and continues if it cannot verify its own certificate chain.
A true return is NOT sufficient proof of trust. Explicit trust-root/signature,
identity, app-ID and expiry verification plus server acceptance are required.
Never weaken those checks or enable unauthenticated mode to make a test pass.
The existing public GNS [certificate schema](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/src/common/steamnetworkingsockets_messages_certs.proto)
is the encoder contract, not a replacement for authentication validation.

## Reproducible aggregate checks and build enablement

`ASAN_OPTIONS=detect_leaks=0 python3 tools/steam-stub-arm64/run_host_tests.py`
passed locally. Omit the sanitizer override on a normal machine to retain leak
checking. A proposed GitHub Actions workflow runs the same host suite on pull
requests; it has not been published or run on GitHub.

The current workspace lacks SDK/NDK, Gradle and CMake. Project requirements are
Android SDK 35, NDK 27.0.12077973 and CMake 3.22.1. Official Android command-line
SDK provisioning requires the [Android SDK License Agreement](https://developer.android.com/studio#terms).
It has not been accepted on the user's behalf. The runtime network allowlist
also excludes the required SDK/Maven/Gradle download hosts. A suitably provisioned
build environment or authorized GitHub CI is needed for a full Android build.

Public GNS currently declares `SteamNetworkingSockets012` and
`SteamNetworkingUtils004`; the ValDroid TSV does not establish which interface
version Valheim 1.0.20 actually requests. Do not assume those versions match.
