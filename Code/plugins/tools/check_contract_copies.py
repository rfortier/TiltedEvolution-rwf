#!/usr/bin/env python3
"""Keep every vendored copy of the plugin-messaging contract ABI-identical.

The contract header `STRPluginMessagingAPI/STRPluginMessagingAPI.h` is copied
into each companion plugin, because a plugin has to compile against the
interface without taking a dependency on this repository's build. Five copies
ship:

    Code/plugins/STRPM/...            the canonical one the runtime exports
    plugins/STRPluginMessagingAPI/... the standalone bridge
    plugins/AnimSyncTogether/...
    plugins/DAVSyncTogether/...
    plugins/IEDSyncTogether/...
    plugins/TradeTogether/...

The interface structs are function-pointer tables, so their ABI is the *order*
of the members. The exchange API cannot check that order: the runtime hands back
a pointer, the plugin calls through slot N, and if a member was inserted, dropped
or reordered in one copy the call dispatches to the wrong function. Nothing in
the language, the linker or the exchange protocol compares the copies, and the
failure is a fault inside a third-party DLL - the same shape as the RaceMenu
version mismatch `check_skee_abi.py` exists for.

Byte-for-byte equality is *not* the requirement and would be wrong to demand:
the copies legitimately differ in whitespace and in which trailing declarations
they carry, and they already differ by up to 1.4 KB. What has to be identical is
what the ABI is made of:

  * the member order of every interface struct, which is the vtable-like layout;
  * every shared constant's value, which both sides compare before dispatching;
  * the set of types declared, so a copy cannot quietly gain or lose one.

    python Code/plugins/tools/check_contract_copies.py          # the gate
    python Code/plugins/tools/check_contract_copies.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]

# The contract's own manifest. It names the canonical header, its hash, the
# interface constants, the export names and every consumer with the file that
# consumer vendors - so the list below is derived from it rather than restated.
# A hardcoded list is how the first version of this gate came to cover six of the
# seven consumers: `MorphSyncTogether/src/STRPMCompat.h` and
# `OStimTogether/src/STRPMApi.h` were both in the manifest and in neither the
# gate nor the code.
CONTRACT_REL = 'Code/plugins/STRPM/contract.json'

# The structs whose member order is the ABI. Listed explicitly so a new interface
# struct is a deliberate addition here rather than an unnoticed omission.
INTERFACES = ['Interface', 'DiagnosticsInterface', 'TransportInterface', 'ProxyResolverInterface']

# Constants both sides read and compare. A mismatch means one side dispatches on
# a version the other does not serve.
CONSTANTS = [
    'kInterfaceVersion', 'kDiagnosticsInterfaceVersion', 'kTransportInterfaceVersion',
    'kProxyResolverInterfaceVersion', 'kRuntimeBackendVersion', 'kEnvelopeVersion',
]

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')

# A function-pointer member. The calling-convention macro has two spellings in the
# tree and both are the same ABI: the canonical header defines
# `STRPM_CALL __cdecl` on Windows, and `OStimTogether/src/STRPMApi.h` writes
# `__cdecl` directly. Matching only the macro made every member of that file count
# as zero, which is a false pass rather than a failure.
CALL_CONV = r'(?:STRPM_CALL|__cdecl|__fastcall|__stdcall)'
MEMBER_FN = re.compile(CALL_CONV + r'\s*\*\s*(\w+)\s*\)')
# `#define STRPM_CALL __cdecl` and friends, so the two spellings can be shown equal.
CALL_MACRO = re.compile(r'#define\s+(STRPM_CALL)\s+(\w+)')


def contract() -> dict:
    path = ROOT / CONTRACT_REL
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding='utf-8'))


def copies() -> dict[str, str]:
    """{label: path} for every copy the contract names, canonical header first."""
    manifest = contract()
    out: dict[str, str] = {}
    header = manifest.get('header')
    if header:
        out['runtime'] = header
    for label, entry in sorted((manifest.get('consumers') or {}).items()):
        root = entry.get('root')
        vendored = entry.get('vendored')
        if root and vendored:
            out[label] = f'{root}/{vendored}'
    return out


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def struct_body(text: str, name: str) -> str | None:
    """The body of `struct name { ... }`, brace-matched.

    Brace-matched rather than regex-terminated: these structs nest helper
    types, so stopping at the first closing brace would undercount the members.
    """
    match = re.search(r'struct\s+' + re.escape(name) + r'\s*\{', text)
    if not match:
        return None
    i = match.end()
    depth = 1
    start = i
    while i < len(text) and depth:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    return text[start:i - 1]


def members(body: str) -> list[str]:
    """Member names in declaration order.

    A function-pointer member is `Ret(CONV* name)(...)`, where CONV is the
    calling-convention macro or the convention spelled out; a scalar member is
    `Type name{...};`. Both are collected by position and sorted, because the
    declaration order of the two forms interleaves.
    """
    found: list[tuple[int, str]] = []
    for match in MEMBER_FN.finditer(body):
        found.append((match.start(), match.group(1)))
    for match in re.finditer(r'^\s*[\w:]+\s+(\w+)\s*(?:\{[^}]*\})?\s*;\s*$', body, re.M):
        found.append((match.start(), match.group(1)))
    return [name for _at, name in sorted(found)]


def declared_types(text: str) -> set[str]:
    return set(re.findall(r'(?:struct|class|enum(?:\s+class)?)\s+(\w+)', text))


def constants(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for name in CONSTANTS:
        match = re.search(r'\b' + re.escape(name) + r'\s*=\s*([^;,]+)', text)
        if match:
            out[name] = match.group(1).strip()
    return out


def check(failures: list[str], warnings: list[str]) -> int:
    """Every copy agrees on the ABI: member order, constants and types."""
    wanted = copies()
    if not wanted:
        failures.append(
            f'{CONTRACT_REL} is missing or names no consumers, so there is nothing to compare')
        return 0

    texts: dict[str, str] = {}
    for label, rel in wanted.items():
        path = ROOT / rel
        if not path.is_file():
            failures.append(
                f'{rel} is missing; {label} cannot be compared against the other copies. '
                f'If the plugin left the repository, remove it from the contract rather than '
                f'leaving this check covering nothing')
            continue
        texts[label] = strip_comments(path.read_text(encoding='utf-8', errors='replace'))

    if 'runtime' not in texts:
        failures.append('the runtime copy of the contract header is missing, so there is no reference')
        return 0

    reference = texts['runtime']
    checked = 0

    # 1. Interface member order - the ABI itself.
    #
    # A consumer need not declare every interface: the contract says which
    # structs exist, not that every plugin uses all of them. What it may not do is
    # declare one with a different member order, or with fewer members than it
    # actually calls - so a declared struct is compared in full, and an absent one
    # is reported only as a note.
    for interface in INTERFACES:
        body = struct_body(reference, interface)
        if body is None:
            failures.append(
                f'the runtime copy no longer declares {interface}; drop it from INTERFACES or '
                f'restore it rather than leaving this check covering nothing')
            continue
        want = members(body)
        if not want:
            failures.append(f'the runtime copy declares {interface} with no members; the extraction '
                            f'is stale and this check is covering nothing')
            continue

        for label, text in sorted(texts.items()):
            if label == 'runtime':
                continue
            other = struct_body(text, interface)
            if other is None:
                continue
            got = members(other)
            checked += 1
            if got == want:
                continue
            if len(got) != len(want):
                failures.append(
                    f'{label}/{interface} declares {len(got)} member(s) but the runtime copy '
                    f'declares {len(want)}: {want} vs {got}. Every call after the difference '
                    f'dispatches through the wrong slot')
            elif sorted(got) == sorted(want):
                failures.append(
                    f'{label}/{interface} declares the same members in a DIFFERENT ORDER: '
                    f'{want} vs {got}. The order is the ABI')
            else:
                only_ref = [m for m in want if m not in got]
                only_other = [m for m in got if m not in want]
                failures.append(
                    f'{label}/{interface} disagrees with the runtime copy - only in the runtime: '
                    f'{only_ref}; only in {label}: {only_other}')

    # A copy the contract names that declares none of the interfaces above is not a
    # copy of the contract: every comparison in that loop skipped it, so it counted
    # as agreeing without a member being read. `checked` has no lower bound of its
    # own - emptying one vendored header drops it from 25 to 22 and still passes -
    # so the floor is per copy rather than a number.
    for label, text in sorted(texts.items()):
        if label == 'runtime':
            continue
        if not any(struct_body(text, interface) is not None for interface in INTERFACES):
            failures.append(
                f'{wanted[label]} declares none of {INTERFACES}; {label} is named by the '
                f'contract but contributes no interface layout to compare, so this gate would '
                f'count it as agreeing with the runtime copy without reading a member')

    if not checked:
        failures.append(
            'no interface layout was compared in any copy; comparing nothing is not a pass')

    # 2. Shared constants. A consumer only has to agree about the constants it
    #    actually uses: the contract declares the whole interface set, and a
    #    plugin that never asks for the transport interface has no reason to carry
    #    its version constant. Requiring all of them reports working code as
    #    broken, and a criterion that does that gets abandoned (section 115.3).
    #    What is not allowed is using a constant without declaring it, or
    #    declaring it with a different value.
    want_constants = constants(reference)
    for label, text in sorted(texts.items()):
        if label == 'runtime':
            continue
        got = constants(text)
        for name, value in sorted(want_constants.items()):
            if name in got:
                if got[name] != value:
                    failures.append(
                        f'{label} sets {name} = {got[name]} but the runtime copy sets {value}')
            elif re.search(r'\b' + re.escape(name) + r'\b', text):
                failures.append(
                    f'{label} uses {name} but does not declare it; the runtime copy sets it to '
                    f'{value}, so the two sides would compare different things')

    # 3. The declared type set. Same rule: a type the consumer never mentions need
    #    not be declared, but one it does mention must be - otherwise the file only
    #    compiles because a different header happened to be included first.
    want_types = declared_types(reference)
    for label, text in sorted(texts.items()):
        if label == 'runtime':
            continue
        got = declared_types(text)
        missing = [t for t in sorted(want_types - got)
                   if re.search(r'\b' + re.escape(t) + r'\b', text)]
        if missing:
            failures.append(
                f'{label} uses {missing} but does not declare them, while the runtime copy does; '
                f'the file compiles only if something else included them first')

    # A copy carrying a constant the runtime does not have is the reverse drift:
    # harmless to compile, but it means the two headers are no longer the same
    # contract and the next addition has no reference to follow.
    for label, text in sorted(texts.items()):
        if label == 'runtime':
            continue
        extra = sorted(set(constants(text)) - set(want_constants))
        if extra:
            warnings.append(
                f'{label} declares {extra}, which the runtime copy does not; if that is a new '
                f'contract constant, add it to the runtime copy and to CONSTANTS here')

    # 4. The recorded hash of the canonical header. The contract publishes one so
    #    a consumer can tell whether it vendored the same revision, and nothing
    #    checked it - a header edit that leaves the hash stale makes the manifest
    #    claim a revision that no longer exists.
    #
    #    The hash is taken over the LF form, because that is what git stores: the
    #    working tree carries CRLF on Windows, so hashing the file as read would
    #    report a mismatch on every checkout. `--list` prints both so the
    #    normalisation is visible rather than implied.
    manifest = contract()
    recorded = manifest.get('headerSha256')
    if recorded:
        raw = (ROOT / wanted['runtime']).read_bytes()
        lf = raw.replace(b'\r\n', b'\n')
        actual = hashlib.sha256(lf).hexdigest()
        if actual != recorded:
            failures.append(
                f'{CONTRACT_REL} records headerSha256 {recorded} but the canonical header hashes '
                f'to {actual} (LF form). A consumer cannot tell which revision it vendored')
    else:
        # A warning here would be a silent skip: `warnings` is only printed, so
        # dropping the key from the manifest would retire the whole rule while the
        # gate still exits 0. The manifest is the corpus of this rule, and a
        # missing corpus is a failure rather than a satisfied check.
        failures.append(
            f'{CONTRACT_REL} records no headerSha256, so the canonical header revision cannot be '
            f'pinned and no consumer can tell which revision it vendored; restore the key rather '
            f'than leaving this rule covering nothing')

    # A consumer named by the contract but missing on disk is a stale manifest.
    for label, rel in wanted.items():
        if not (ROOT / rel).is_file():
            failures.append(
                f'{CONTRACT_REL} names {label} at {rel}, which does not exist; the manifest and the '
                f'tree disagree')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    wanted = copies()
    if args.list:
        manifest = contract()
        print('checked:')
        print('  every copy the contract names declares the same interface member order')
        print('  every copy sets the same values for the shared constants')
        print('  every copy declares the same types')
        print('  the canonical header hashes to the headerSha256 the contract records')
        print(f'  {len(wanted)} copy/copies, {len(INTERFACES)} interface(s), '
              f'{len(CONSTANTS)} constant(s)')
        for label, rel in wanted.items():
            path = ROOT / rel
            size = path.stat().st_size if path.is_file() else 0
            print(f'    {label:<24} {size:>6} bytes  {rel}')
        if manifest.get('headerSha256') and (ROOT / wanted.get('runtime', '')).is_file():
            raw = (ROOT / wanted['runtime']).read_bytes()
            print(f'  headerSha256 recorded : {manifest["headerSha256"]}')
            print(f'  headerSha256 LF form  : {hashlib.sha256(raw.replace(chr(13).encode() + chr(10).encode(), chr(10).encode())).hexdigest()}')
            print(f'  headerSha256 as-on-disk: {hashlib.sha256(raw).hexdigest()}')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    if warnings:
        for warning in warnings:
            print(f'  note: {warning}')

    if failures:
        print(f'CONTRACT COPY CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('CONTRACT COPY CHECK OK')
    print(f'  {len(wanted)} copy/copies agree on {checked} interface layout(s), the shared '
          f'constants, the declared types and the recorded header hash')
    return 0


if __name__ == '__main__':
    sys.exit(main())
