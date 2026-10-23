#!/usr/bin/env python3
"""Inventory the Steam network/auth ABI without loading a native library.

Run against the checked-in TSV, optionally comparing a text export list created by
`llvm-nm -D --defined-only libsteam_api.so`. Presence is NOT compatibility proof.
No credentials or game binaries are read or transmitted by this script.
"""
import argparse
import json
from pathlib import Path


def inventory(text):
    result = {}
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        library, name, managed, abi = line.split("\t")
        if library != "steam_api":
            continue
        if "Networking" in name or any(token in name for token in (
                "AuthSession", "AuthTicket", "EncryptedAppTicket", "RunCallbacks",
                "RegisterCallback", "UnregisterCallback", "GetSteamID", "BLoggedOn")):
            result[name] = {"managed_return": managed, "abi_return": abi}
    return result


def exported_symbols(text):
    # GNU/LLVM nm output: address, type, symbol (optional ELF version suffix).
    return {line.split()[-1].split("@")[0] for line in text.splitlines() if line.split()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--imports", type=Path,
                        default=Path(__file__).with_name("steamworks_net_imports.tsv"))
    parser.add_argument("--exports", type=Path)
    args = parser.parse_args()
    imports = inventory(args.imports.read_text(encoding="utf-8"))
    report = {"warning": "Export presence does not prove ABI, authentication or server compatibility",
              "argument_signatures_available": False, "imports": imports}
    if args.exports:
        report["missing_exports"] = sorted(set(imports) - exported_symbols(args.exports.read_text()))
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if report.get("missing_exports") else 0


if __name__ == "__main__":
    raise SystemExit(main())
