#!/usr/bin/env python3
"""Compile against supplied official GNS headers; execute recording-fake tests only."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gns-include", type=Path, required=True,
                        help="GNS include directory containing steam/isteamnetworkingsockets.h")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    include = args.gns_include.resolve()
    if not (include / "steam/isteamnetworkingsockets.h").is_file():
        parser.error("Expected official GameNetworkingSockets headers under --gns-include")
    compiler = os.environ.get("CXX", "g++")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(include)]
    # Verifies actual NativeSockets calls against Valve's declarations, not mocks.
    subprocess.run([compiler, *flags, "-fsyntax-only", str(root / "gns_native.cpp"),
                    str(root / "gns_transport.cpp")], check=True)
    with tempfile.TemporaryDirectory(prefix="valdroid-gns-test-") as tmp:
        binary = Path(tmp) / "test_transport"
        subprocess.run([compiler, *flags, "-fsanitize=address,undefined",
                        str(root / "gns_transport.cpp"), str(root / "test_transport.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Official-header syntax checks and recording-fake tests passed.")
    print("No real GNS library was linked; no socket, certificate or Valheim server was tested.")


if __name__ == "__main__":
    main()
