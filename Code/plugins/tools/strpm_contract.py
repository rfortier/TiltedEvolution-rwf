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

# A field of an interface struct. Two declaration forms appear in this header:
#
#   Type name{ init };                                  a data member
#   Result(STRPM_CALL* send)(const char*, Target, ...); a function-pointer member
#
# The first version of this pattern only matched a bare `type name;`, so it missed
# every member whose type carries a `*` inside the name (`const char* channel`) and
# every function pointer - Interface came out with 1 field instead of 7, Message with
# 4 instead of 6. An incomplete layout would have let a real change through while the
# check still reported OK.
def _split_members(body: str) -> list[str]:
    """Split a struct body into member declarations, honouring nesting.

    Linear on purpose. The first version of this used one regex to match a whole
    declaration, with a repetition over alternatives (`identifier | const | stars`)
    that overlap each other - `const` is also an identifier - and a non-matching line
    then had exponentially many ways to fail. Reading the canonical header did not
    finish in five minutes; a gate that hangs is worse than one that is wrong,
    because nothing tells you it is still thinking.
    """
    out, current, depth = [], [], 0
    for char in body:
        if char in "{([":
            depth += 1
        elif char in "})]":
            depth -= 1
        if char == ";" and depth == 0:
            out.append("".join(current))
            current = []
        else:
            current.append(char)
    return [entry.strip() for entry in out if entry.strip()]


def _member_signature(entry: str) -> str | None:
    """`type name`, or `ret (call*name)` for a function pointer, or None."""
    # Drop an initialiser and an array suffix; neither is layout.
    array = ""
    match = re.search(r"\[\s*(\d+)\s*\]\s*$", entry)
    if match:
        array = f"[{match.group(1)}]"
        entry = entry[: match.start()].rstrip()
    entry = re.sub(r"\{[^{}]*\}\s*$", "", entry).strip()
    if not entry or entry.startswith(("//", "#")):
        return None

    # A function-pointer member: Result(STRPM_CALL* send)(...)
    pointer = re.match(r"^([A-Za-z_]\w*)\s*\(\s*([\w\s]*?\*)\s*(\w+)\s*\)", entry)
    if pointer:
        # The calling convention is spelled two ways in the vendored copies: the
        # canonical header uses the macro and one consumer expanded it. Both are the
        # same ABI - the macro *is* `__cdecl` (line 7 of the canonical header) - so
        # comparing the raw text would report a layout difference where there is
        # none, and a gate that cries wolf on the correct spelling is one nobody
        # keeps. Normalised to the macro name, which is the interface's own word for
        # it.
        call = re.sub(r"\s+", "", pointer.group(2)).replace("__cdecl", "STRPM_CALL")
        return f"{pointer.group(1)} ({call}{pointer.group(3)})"

    if "(" in entry or ")" in entry:
        return None  # a method, not a member

    tokens = entry.split()
    if len(tokens) < 2:
        return None
    name_token = tokens[-1]
    stars_before = len(name_token) - len(name_token.lstrip("*"))
    stars_after = len(name_token) - len(name_token.rstrip("*"))
    name = name_token.strip("*")
    if not re.fullmatch(r"\w+", name):
        return None
    field_type = " ".join(tokens[:-1])
    if stars_before:
        field_type = f"{field_type} {'*' * stars_before}"
    if stars_after:
        field_type = f"{field_type}{'*' * stars_after}"
    return re.sub(r"\s+", " ", f"{field_type} {name}").strip() + array


def struct_layouts(text: str) -> dict[str, list[str]]:
    """Every struct's field list, in declaration order.

    The interface structs are a C ABI that independent plugins vendor a copy of and
    are built against. They pass `Target` by value into our send() and read the
    fields of the `Message*` and `RuntimeStatus*` we hand back, so the layout *is*
    the interface - but the contract only pinned the header's hash, its constants
    and its type NAMES. A field reordered while re-recording the hash changed the ABI
    with every check still green, and a consumer would then read our sender's
    displayName from the wrong offset and follow it as a pointer.
    """
    clean = strip_comments(text)
    layouts: dict[str, list[str]] = {}
    for match in re.finditer(r"\bstruct\s+([A-Z]\w*)\s*\{", clean):
        name = match.group(1)
        depth, index = 1, match.end()
        while index < len(clean) and depth:
            if clean[index] == "{":
                depth += 1
            elif clean[index] == "}":
                depth -= 1
            index += 1
        fields = [s for s in (_member_signature(e) for e in _split_members(clean[match.end(): index - 1]))
                  if s]
        if fields:
            layouts[name] = fields
    return layouts


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
        "layouts": struct_layouts(clean),
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
        # C5's subject: the field list of every interface struct, in order. The hash
        # above pins the file; this pins what the file means to a consumer that was
        # built against an older copy of it.
        "interfaceLayouts": {name: fields for name, fields in sorted(surface["layouts"].items())},
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
    print(f"  interfaceLayouts  {len(contract['interfaceLayouts'])} struct(s), "
          f"{sum(len(v) for v in contract['interfaceLayouts'].values())} field(s)")
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

    # ---- C5: the interface layouts are still the pinned ones --------------
    # C1 pins the header's bytes, so any edit trips it once - and then someone
    # re-records the hash and it is green again. This compares what the header
    # *means*: the field list of every struct a consumer reads across the DLL
    # boundary. A field reordered, inserted or resized changes the ABI even though
    # the header is still the header, and re-running 'dump' to clear C1 would
    # otherwise carry the change in silently.
    pinned = contract.get("interfaceLayouts")
    if pinned is None:
        failures.append(
            "C5 the contract has no interfaceLayouts; re-run 'dump' to record them, or the layout of "
            "every struct a consumer reads by offset is unpinned"
        )
    else:
        current = surface["layouts"]
        for name, fields in sorted(current.items()):
            if name not in pinned:
                failures.append(
                    f"C5 struct {name} is new since the contract was recorded; a consumer built "
                    f"against the pinned header does not have it"
                )
                continue
            if pinned[name] != fields:
                failures.append(
                    f"C5 struct {name} changed layout\n"
                    f"    pinned  {pinned[name]}\n"
                    f"    current {fields}\n"
                    f"    a consumer built against the pinned header reads every field after the "
                    f"change from the wrong offset"
                )
        for name in sorted(set(pinned) - set(current)):
            failures.append(
                f"C5 struct {name} was removed; consumers built against the pinned header still "
                f"declare it and still pass it to us"
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

        # C5 per consumer: a vendored copy whose structs do not have the same field
        # order is the failure this contract exists for. The consumer compiles
        # against its copy, we compile against ours, and the two disagree about
        # where every field is - `Target` goes in by value and the `Message*` we hand
        # back is read field by field, so a reordered copy reads our pointer as an
        # integer or follows an integer as a pointer.
        #
        # Every struct both headers declare is compared. An earlier version only
        # compared structs the consumer's own sources named, on the theory that the
        # rest were noise - and that filter is what let a reordered `Sender` through:
        # a consumer never spells `Sender` (it arrives inside the `Message` we hand
        # it), so the struct it would misread was the one struct not checked. A
        # vendored copy's declarations are the ABI whether or not the consumer names
        # them.
        for struct, fields in sorted(surface["layouts"].items()):
            theirs = vendored["layouts"].get(struct)
            if theirs is None:
                continue
            if theirs != fields:
                failures.append(
                    f"C5 [{name}] struct {struct} disagrees with the authoritative layout\n"
                    f"    canonical {fields}\n"
                    f"    vendored  {theirs}"
                )

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
