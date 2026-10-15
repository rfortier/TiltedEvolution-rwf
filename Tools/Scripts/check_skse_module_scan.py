#!/usr/bin/env python3
"""Keep the SKSE module-name scan from stopping on a module that names no version.

The client resolves the game version in three steps, and the SKSE module name is
the authoritative one: SKSE is version-locked to the game, and a downgrader
replaces the exe without replacing the module, so the module name survives
exactly the cases where the exe's version resource does not. The other two steps
- the exe resource and the address-library file names - exist only as fallbacks.

The scan looks for a loaded module whose name starts with `skse64_` and parses
`%d_%d_%d` out of it. The prefix alone does not identify the runtime:
`skse64_steam_loader.dll` is mapped into the game process as well - it is what
patches the game's IAT and hooks its WinMain before the runtime loads - and it
carries the same prefix while naming no version.

The scan therefore has to keep looking when a candidate does not parse. It used
to `break` unconditionally after the first `skse64_` module, outside the parse,
so a loader enumerated first ended the search with no version at all and
silently demoted the authoritative signal to the two fallbacks this step exists
to replace. The enumeration order of the module list is not ours to choose, so
"the runtime happens to come first" is not something to rely on.

Two translation units ask this same question of the same module list:

    Code/skse_client/main.cpp      which address library / runtime to use
    Code/skse_bootstrap/main.cpp   which per-version runtime DLL to load

and they must not disagree, so this checks both: the parse must be what decides
whether to stop, and neither may fall back to `break` on the prefix alone.

    python Tools/Scripts/check_skse_module_scan.py          # the gate
    python Tools/Scripts/check_skse_module_scan.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# The two callers of the scan, and the function that performs it in each.
IMPLEMENTATIONS = [
    ('Code/skse_client/main.cpp', 'GetSKSEModuleVersion'),
    ('Code/skse_bootstrap/main.cpp', 'IsLegacyGame'),
]

# The module the scan must tolerate. Named here so the reason is visible at the
# point of failure rather than only in the prose above.
NO_VERSION_MODULE = 'skse64_steam_loader.dll'

BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
LINE_COMMENT = re.compile(r'//[^\n]*')


def strip_comments(text: str) -> str:
    """Prose must not satisfy a check that code continues past a module."""
    return LINE_COMMENT.sub('', BLOCK_COMMENT.sub('', text))


def body_of(text: str, name: str) -> str | None:
    """The body of `name(...)`, brace-matched, or None when absent."""
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{]*\)\s*(?:noexcept)?\s*\{', text)
    if not match:
        return None
    depth = 0
    for i in range(match.end() - 1, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[match.end():i]
    return None


def check(failures: list[str], warnings: list[str]) -> int:
    checked = 0
    for rel, func in IMPLEMENTATIONS:
        path = ROOT / rel
        if not path.is_file():
            failures.append(
                f'{rel} is missing; if the scan moved, move it here rather than leaving this '
                f'check covering nothing')
            continue

        text = strip_comments(path.read_text(encoding='utf-8', errors='replace'))
        body = body_of(text, func)
        if body is None:
            failures.append(
                f'{rel}: {func}() is no longer defined here; the SKSE module-name scan is what '
                f'makes the game version authoritative, so if it moved, move this entry')
            continue

        # 1. The scan must look for the prefix...
        if 'skse64_' not in body:
            failures.append(
                f'{rel}: {func}() no longer matches the `skse64_` module prefix, so it cannot '
                f'find the runtime that names the game version')
            continue

        checked += 1

        # 2. ...and it must parse the version out of the name it found.
        parses = re.search(r'swscanf_s\s*\(\s*name\.c_str\(\)\s*\+\s*\d+', body)
        if not parses:
            failures.append(
                f'{rel}: {func}() no longer parses the version out of the module name; the '
                f'name is the authoritative signal and nothing else reads it')
            continue

        # 3. The decision to stop must be the parse, not the prefix. A `break`
        #    that is not inside the parse's success branch ends the search on a
        #    module that names no version - which is what
        #    skse64_steam_loader.dll is.
        parse_block = None
        start = parses.start()
        brace = body.find('{', start)
        if brace >= 0:
            depth = 0
            for i in range(brace, len(body)):
                if body[i] == '{':
                    depth += 1
                elif body[i] == '}':
                    depth -= 1
                    if depth == 0:
                        parse_block = body[brace:i]
                        break

        if parse_block is None:
            failures.append(
                f'{rel}: {func}() parses the module name but the parse has no block, so the '
                f'result cannot be conditioned on; this gate cannot tell what it does next')
            continue

        # 3a. Nothing may end the loop before the parse is reached. A `break`
        #     between the prefix test and the parse is the same defect as an
        #     unconditional break after it: the search stops on a module whose
        #     name does not carry a version, so the parse is never consulted.
        before = body[:body.find(parse_block)]
        stray = re.search(r'^\s*break\s*;', before, re.M)
        if stray:
            line = before[:stray.start()].count('\n') + 1
            failures.append(
                f'{rel}: {func}() breaks on line {line} of the loop, before the version parse '
                f'is reached. The prefix alone does not identify the runtime: '
                f'{NO_VERSION_MODULE} carries it while naming no version, so this stops the '
                f'search on a module that cannot answer and the scan returns nothing')

        if not re.search(r'\b(break|return)\s*;', parse_block) and \
                not re.search(r'\breturn\s+[^;]+;', parse_block):
            failures.append(
                f'{rel}: {func}() parses the module name but does not stop on a successful '
                f'parse. Continuing after a name that parsed means a later module overwrites '
                f'the answer, and the version is then whichever `skse64_*` module the loader '
                f'happened to enumerate last')
        else:
            # Everything after the parse block must not contain a bare `break`,
            # which would be the prefix deciding again.
            after = body[body.find(parse_block) + len(parse_block):]
            if re.search(r'^\s*break\s*;', after, re.M):
                failures.append(
                    f'{rel}: {func}() breaks after the loop body as well as inside the parse; '
                    f'the unconditional break stops the search on the first `skse64_` module, '
                    f'and {NO_VERSION_MODULE} carries that prefix while naming no version, so '
                    f'the scan would return no version whenever the loader is enumerated first')

        # 4. The loop must actually iterate: a `for`/`while` whose body cannot
        #    advance would make the "keep looking" rule vacuous.
        if not re.search(r'\b(for|while)\s*\(', body):
            failures.append(
                f'{rel}: {func}() no longer walks the module list, so "keep looking past a '
                f'module that names no version" has nothing to apply to')

    # The module the rule exists for must still be the one named in the prose, so
    # a future rename of it is a deliberate edit here rather than a silent gap.
    client = ROOT / 'Code' / 'skse_client' / 'main.cpp'
    if client.is_file():
        raw = client.read_text(encoding='utf-8', errors='replace')
        if NO_VERSION_MODULE not in raw:
            warnings.append(
                f'{client.relative_to(ROOT).as_posix()} no longer names {NO_VERSION_MODULE}; '
                f'that module is the reason this rule exists, so if it is gone the rule can go')

    if checked == 0:
        failures.append(
            'neither SKSE module-name scan was found, so nothing was compared; an empty scan '
            'means the sources moved, not that the rule holds')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  each SKSE module-name scan matches the `skse64_` prefix')
        print('  each parses the version out of the module name it found')
        print('  each stops on a successful parse, not on the prefix alone')
        print(f'  {NO_VERSION_MODULE} is still the module named as the reason')
        for rel, func in IMPLEMENTATIONS:
            path = ROOT / rel
            state = 'present' if path.is_file() else 'MISSING'
            print(f'    {rel:<40} {func:<22} {state}')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    for warning in warnings:
        print(f'  note: {warning}')

    if failures:
        print(f'SKSE MODULE SCAN CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('SKSE MODULE SCAN CHECK OK')
    print(f'  {checked} module-name scan(s) keep looking past a module that names no version, '
          f'so the game version stays authoritative')
    return 0


if __name__ == '__main__':
    sys.exit(main())
