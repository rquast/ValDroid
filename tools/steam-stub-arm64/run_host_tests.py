#!/usr/bin/env python3
"""Run host-only networking checks. These do not prove Android/server compatibility."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
CPP = ROOT / "app/src/main/cpp"


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    with tempfile.TemporaryDirectory(prefix="valdroid-networking-") as tmp:
        tmp = Path(tmp)
        generated = tmp / "steam_api_stub.c"
        run(sys.executable, HERE / "gen_steam_stub.py", HERE / "steamworks_net_imports.tsv", generated)
        if generated.read_bytes() != (CPP / "steam_api_stub.c").read_bytes():
            raise RuntimeError("Generated shim is stale; regenerate and review the diff")
        compiler = os.environ.get("CC", "cc")
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror"]
        for name, extra in [("networking_offline", []),
                            ("certificate_bridge", [CPP / "steam_certificate_bridge.c"])]:
            output = tmp / name
            run(compiler, *flags, "-fsanitize=address,undefined", *extra,
                HERE / ("test_" + name + ".c"), "-o", output)
            run(output)  # Preserve the caller's sanitizer settings.
        lib_path = tmp / "libsteam_api.so"
        run(compiler, *flags, "-fPIC", "-shared", "-I", CPP, generated, "-o", lib_path)
        symbols = tmp / "exports.txt"
        with symbols.open("w") as out:
            run("nm", "-D", "--defined-only", lib_path, stdout=out)
        with (tmp / "audit.json").open("w") as out:
            run(sys.executable, HERE / "audit_networking.py", "--exports", symbols, stdout=out)
        lib = ctypes.CDLL(str(lib_path))
        lib.SteamAPI_Init.restype = ctypes.c_uint64
        assert lib.SteamAPI_Init() == 1
        lib.SteamAPI_ISteamUser_BLoggedOn.restype = ctypes.c_uint64
        assert lib.SteamAPI_ISteamUser_BLoggedOn() == 0
        class AuthStatus(ctypes.Structure):
            _fields_ = [("availability", ctypes.c_int32), ("debug", ctypes.c_char * 256)]
        status = AuthStatus()
        fn = lib.SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus
        fn.argtypes = [ctypes.c_void_p, ctypes.POINTER(AuthStatus)]
        fn.restype = ctypes.c_int32
        assert fn(None, ctypes.byref(status)) == -102
        assert status.availability == -102 and b"not implemented" in status.debug
        run(sys.executable, "-m", "unittest", "discover", "-s", HERE,
            "-p", "test_audit_networking.py", "-v")
    print("Host checks passed. Android packaging, authentication and multiplayer remain untested.")


if __name__ == "__main__":
    main()
