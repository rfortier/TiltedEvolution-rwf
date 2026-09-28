#!/usr/bin/env python3
"""STRPM consumer contract: one authoritative header, four vendored copies.

The STR Plugin Messaging API is a C ABI consumed by independent SKSE plugins
that live in their own repositories. Each consumer has to vendor the interface
header, because a consumer is built against a fixed ABI and must not silently
follow a newer SDK. That vendoring is also the only place this project can
break: a consumer that ships an older copy of the header can register a
channel whose layout no longer matches the DLL that loads it.

This tool makes that failure mode impossible to reach unnoticed.

Checks
------
C1  The authoritative header hash matches the pin recorded in contract.json.
    Any layout edit forces the pin to be re-recorded, so it can never drift
    silently.
C2  Every interface symbol a consumer's sources reference is declared by the
    header that consumer actually compiles against.
C3  The exported entry point names agree between the header and the contract.
C4  Constant values (interface versions, limits) that a vendored copy shares
    with the authoritative header carry the same value. This is the mismatch
    that produces runtime garbage rather than a build error.

Usage
-----
    python strpm_contract.py dump    # regenerate contract.json from the header
    python strpm_contract.py check   # verify the contract (default)
    python strpm_contract.py sync    # copy the header into every consumer
                                     # and emit a patch per consumer
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

HEADER_REL = "Code/plugins/STRPM/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h"
CONTRACT_REL = "Code/plugins/STRPM/contract.json"
PATCH_DIR_REL = "Code/plugins/STRPM/patches"

# Vendored copy each consumer compiles against, relative to the consumer root.
CONSUMERS = {
    "STRPluginMessagingAPI": {
        "root": "plugins/STRPluginMessagingAPI",
        "sources": ["src", "include"],
        "vendored": "include/STRPluginMessagingAPI/STRPluginMessagingAPI.h",
    },
    "IEDSyncTogether": {
        "root": "plugins/IEDSyncTogether",
        "sources": ["src", "include"],
        "vendored": "include/STRPluginMessagingAPI/STRPluginMessagingAPI.h",
    },
    "OStimTogether": {
        "root": "plugins/OStimTogether",
        "sources": ["src"],
        "vendored": "src/STRPMApi.h",
    },
    "MorphSyncTogether": {
        "root": "plugins/MorphSyncTogether",
        "sources": ["src"],
        "vendored": "src/STRPMCompat.h",
    },
    "AnimSyncTogether": {
        "root": "plugins/AnimSyncTogether",
        "sources": ["src", "include"],
        "vendored": "include/STRPluginMessagingAPI/STRPluginMessagingAPI.h",
    },
    "DAVSyncTogether": {
        "root": "plugins/DAVSyncTogether",
        "sources": ["src", "include"],
        "vendored": "include/STRPluginMessagingAPI/STRPluginMessagingAPI.h",
    },
    "TradeTogether": {
        "root": "plugins/TradeTogether",
        "sources": ["src"],
        "vendored": "src/STRPluginMessagingAPI/STRPluginMessagingAPI.h",
    },
}

SOURCE_GLOBS = ("*.cpp", "*.h", "*.hpp")

RE_CONSTEXPR = re.compile(
    r"inline\s+constexpr\s+(?:std::uint32_t|std::uint64_t|std::size_t|char|auto|std::uint16_t|bool)\s+"
    r"(k\w+)\s*(\[\])?\s*=\s*([^;]+);"
)
RE_STRUCT = re.compile(r"\bstruct\s+([A-Z]\w*)")
RE_ENUM = re.compile(r"\benum\s+class\s+([A-Z]\w*)")
RE_USING = re.compile(r"\busing\s+([A-Z]\w*)\s*=")
RE_ENUM_BODY = re.compile(r"\benum\s+(?:class\s+)?\w+\s*(?::[^{]+)?\{([^}]*)\}")
RE_EXPORT_NAME = re.compile(r"kQuery\w*ExportName\[\]\s*=\s*\"([^\"]+)\"")
RE_TOKEN = re.compile(r"\b(k[A-Z]\w*|[A-Z]\w*(?:Interface|Handle|Event|Callback|Message|Status|Kind|Type|Backend|Mode))\b")


def header_text(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def extract_namespace(text: str, name: str) -> str:
    """Return the body of 'namespace <name> { ... }' using brace matching."""
    clean = strip_comments(text)
    match = re.search(r"\bnamespace\s+" + re.escape(name) + r"\s*\{", clean)
    if not match:
        return ""
    index = match.end()
    depth = 1
    while index < len(clean) and depth:
        char = clean[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        index += 1
    return clean[match.end(): index - 1]


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def scan_header(text: str) -> dict:
    """Extract the declared surface of a header."""
    clean = strip_comments(text)

    constants: dict[str, str] = {}
    for name, is_array, raw in RE_CONSTEXPR.findall(clean):
        value = raw.strip()
        if is_array:
            value = value.strip('"')
        else:
            value = re.sub(r"\s+", "", value)
        constants[name] = value

    enum_values: dict[str, int] = {}
    for body in RE_ENUM_BODY.findall(clean):
        position = 0
        for entry in body.split(","):
            entry = entry.strip()
            if not entry:
                continue
            if "=" in entry:
                name, _, literal = entry.partition("=")
                name, literal = name.strip(), literal.strip()
                try:
                    position = int(literal, 0)
                except ValueError:
                    position += 1
            else:
                name = entry
            if re.fullmatch(r"\w+", name):
                enum_values[name] = position
                position += 1

    types = set(RE_STRUCT.findall(clean)) | set(RE_ENUM.findall(clean)) | set(RE_USING.findall(clean))

    symbols = set(constants) | set(enum_values) | types
    return {
        "constants": constants,
        "enumerators": enum_values,
        "types": sorted(types),
        "symbols": sorted(symbols),
        "exports": sorted(RE_EXPORT_NAME.findall(clean)),
    }


def normalized_bytes(rel: str) -> bytes:
    """File content with line endings forced to LF.

    The pin must describe the interface, not the checkout. With the repository's
    "text=auto" attribute and core.autocrlf enabled on Windows, the same commit
    checks out as CRLF for one developer and LF for another, and a hash taken
    over raw bytes would disagree between them and with CI.
    """
    data = (ROOT / rel).read_bytes()
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def sha256_of(rel: str) -> str:
    return hashlib.sha256(normalized_bytes(rel)).hexdigest()


def consumer_used_symbols(spec: dict) -> set[str]:
    used: set[str] = set()
    for sub in spec["sources"]:
        base = ROOT / spec["root"] / sub
        if not base.is_dir():
            continue
        for pattern in SOURCE_GLOBS:
            for path in base.rglob(pattern):
                used.update(RE_TOKEN.findall(strip_comments(path.read_text(encoding="utf-8", errors="replace"))))
    return used


def build_contract() -> dict:
    surface = scan_header(header_text(HEADER_REL))
    return {
        "header": HEADER_REL,
        "headerSha256": sha256_of(HEADER_REL),
        "interfaceVersion": int(surface["constants"].get("kInterfaceVersion", "0")),
        "exportNames": surface["exports"],
        "interfaceConstants": {
            key: value
            for key, value in sorted(surface["constants"].items())
            if key.startswith("k") and not key.startswith("kQuery")
        },
        "interfaceTypes": surface["types"],
        "consumers": {
            name: {"root": spec["root"], "vendored": spec["vendored"]}
            for name, spec in sorted(CONSUMERS.items())
        },
    }


def cmd_dump(_args: argparse.Namespace) -> int:
    contract = build_contract()
    (ROOT / CONTRACT_REL).write_text(
        json.dumps(contract, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"wrote {CONTRACT_REL}")
    print(f"  headerSha256      {contract['headerSha256']}")
    print(f"  interfaceVersion  {contract['interfaceVersion']}")
    print(f"  exportNames       {len(contract['exportNames'])}")
    print(f"  interfaceTypes    {len(contract['interfaceTypes'])}")
    return 0


def cmd_check(args: argparse.Namespace) -> int:
    contract_path = ROOT / CONTRACT_REL
    if not contract_path.is_file():
        print(f"FAIL: {CONTRACT_REL} is missing; run 'dump' first")
        return 1
    contract = json.loads(contract_path.read_text(encoding="utf-8"))

    failures: list[str] = []
    notes: list[str] = []

    # ---- C1: authoritative header is exactly what was reviewed -------------
    actual = sha256_of(HEADER_REL)
    if actual != contract["headerSha256"]:
        failures.append(
            "C1 authoritative header changed without re-recording the pin\n"
            f"    expected {contract['headerSha256']}\n"
            f"    actual   {actual}\n"
            "    re-run 'dump' once the ABI change is intended and reviewed"
        )

    surface = scan_header(header_text(HEADER_REL))

    # Symbols STRPM owns. A consumer is free to have its own kSomething
    # constants; only names the API namespace declares are contract-bound.
    owned = scan_header(extract_namespace(header_text(HEADER_REL), "STRPM"))
    canonical_symbols = set(owned["symbols"]) or set(surface["symbols"])

    # ---- C3: exported entry points agree ----------------------------------
    if sorted(surface["exports"]) != sorted(contract["exportNames"]):
        failures.append(
            "C3 exported entry point names differ from the contract\n"
            f"    contract {contract['exportNames']}\n"
            f"    header   {sorted(surface['exports'])}"
        )

    # ---- per consumer: C2 + C4 -------------------------------------------
    for name, spec in sorted(CONSUMERS.items()):
        vendored_rel = f"{spec['root']}/{spec['vendored']}"
        if not (ROOT / vendored_rel).is_file():
            notes.append(f"[{name}] vendored header missing: {vendored_rel}")
            continue

        vendored = scan_header(header_text(vendored_rel))
        vendored_symbols = set(vendored["symbols"])
        used = consumer_used_symbols(spec)

        # C2: nothing STRPM-owned is used that the compiled header does not declare
        undeclared = sorted((used & canonical_symbols) - vendored_symbols)
        if undeclared:
            failures.append(
                f"C2 [{name}] uses {len(undeclared)} STRPM symbol(s) its header does not declare: "
                + ", ".join(undeclared[:12])
                + (" ..." if len(undeclared) > 12 else "")
            )

        # C4: shared constants carry identical values
        shared = set(vendored["constants"]) & set(surface["constants"])
        mismatched = [
            f"{key}: vendored={vendored['constants'][key]} canonical={surface['constants'][key]}"
            for key in sorted(shared)
            if vendored["constants"][key] != surface["constants"][key]
        ]
        if mismatched:
            failures.append(f"C4 [{name}] constant value mismatch -> " + "; ".join(mismatched))

        present = len(vendored_symbols & set(surface["symbols"]))
        notes.append(
            f"[{name}] vendored={vendored_rel} "
            f"symbols={len(vendored_symbols)} shared-with-canonical={present} "
            f"constants={len(vendored['constants'])}"
        )

    for note in notes:
        print(note)
    print()

    if failures:
        print(f"CONTRACT FAILED ({len(failures)} check(s))")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("CONTRACT OK")
    print(f"  header pin       {contract['headerSha256'][:16]}...")
    print(f"  interfaceVersion {contract['interfaceVersion']}")
    print(f"  consumers        {len(contract['consumers'])}")
    return 0


def cmd_sync(_args: argparse.Namespace) -> int:
    """Copy the authoritative header over every vendored copy.

    Only the consumer that vendored the header verbatim can be synced
    mechanically. A consumer whose file also defines helpers the header only
    declares needs a review pass, so the change is emitted as a patch instead
    of being committed blindly.
    """
    canonical = (ROOT / HEADER_REL).read_text(encoding="utf-8")
    patch_dir = ROOT / PATCH_DIR_REL
    patch_dir.mkdir(parents=True, exist_ok=True)

    for name, spec in sorted(CONSUMERS.items()):
        if name == "STRPluginMessagingAPI":
            continue
        vendored_rel = f"{spec['root']}/{spec['vendored']}"
        vendored_path = ROOT / vendored_rel
        if not vendored_path.is_file():
            print(f"[{name}] skip: {vendored_rel} not found")
            continue

        current = vendored_path.read_text(encoding="utf-8", errors="replace")
        if current == canonical:
            print(f"[{name}] already canonical")
            continue

        vendored_path.write_text(canonical, encoding="utf-8")
        diff = subprocess.run(
            ["git", "-C", str(ROOT / spec["root"]), "diff", "--", spec["vendored"]],
            capture_output=True,
            text=True,
        ).stdout
        patch_path = patch_dir / f"{name}.vendored-header.patch"
        patch_path.write_text(diff, encoding="utf-8")
        print(f"[{name}] synced {vendored_rel} -> patch {patch_path.relative_to(ROOT)} ({len(diff.splitlines())} diff lines)")

    print()
    print("Review each patch before committing it in the consumer repository:")
    print("the authoritative header only *declares* ResultToString; a consumer")
    print("that also defines it locally must keep that definition.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", nargs="?", default="check", choices=["dump", "check", "sync"])
    args = parser.parse_args()
    return {"dump": cmd_dump, "check": cmd_check, "sync": cmd_sync}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
