#!/usr/bin/env python3
"""Keep OStim Together's "unsupported runtime" message equal to what it accepts.

OStimBridge::SelectGraphLayout is the only thing that decides whether a given
OStim.dll runtime is driven or refused, and the critical log it emits when it
refuses is the only thing a player sees. The two drifted apart in the way that
matters: the patch that taught the selector to accept OStimNG 7.5.1.x left the
message naming 7.5.0.2 as the newest supported version, so a player whose
runtime really was refused would be told to install an OStim older than the one
they already had.

Nothing else reads either side. The message is prose, so no compiler checks it;
the selector is code, so no document describes it. Binding them here is what
keeps the refusal path - the one path a stuck player actually reads - honest.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]

# Read through the patch, not the pinned commit: the selector accepts 7.5.1.x
# only once Code/plugins/patches/OStimTogether/0002 is applied, and CI applies
# every patch before this runs.
SOURCE_REL = 'plugins/OStimTogether/src/OStimBridge.cpp'

# The exact form: version.Is(7, 4, 0, 3).
EXACT = re.compile(r'version\.Is\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)')

# The prefix form, which accepts a whole patch series: major/minor/patch equal.
PREFIX = re.compile(
    r'version\.major\s*==\s*(\d+)\s*&&\s*version\.minor\s*==\s*(\d+)'
    r'\s*&&\s*version\.patch\s*==\s*(\d+)', re.S)

MESSAGE = re.compile(r'supported versions are ([^"]+)')

# A token in the message: either an exact version, or a series written "N.N.N.x".
TOKEN = re.compile(r'(\d+)\.(\d+)\.(\d+)(\.\d+|\.x)?')


def selector_body(text: str) -> str:
    """The SelectGraphLayout function, so unrelated version checks do not count."""
    start = text.find('SelectGraphLayout(')
    if start < 0:
        return ''
    end = text.find('static_assert', start)
    return text[start:end if end > 0 else len(text)]


def main() -> int:
    path = ROOT / SOURCE_REL
    if not path.is_file():
        print(f'OSTIM SUPPORT MESSAGE CHECK FAILED (1)')
        print(f'  - {SOURCE_REL} is missing')
        return 1

    text = path.read_text(encoding='utf-8', errors='ignore')
    failures: list[str] = []

    body = selector_body(text)
    if not body:
        print('OSTIM SUPPORT MESSAGE CHECK FAILED (1)')
        print(f'  - {SOURCE_REL}: no SelectGraphLayout function found')
        return 1

    accepted_exact = {tuple(int(v) for v in m) for m in EXACT.findall(body)}
    accepted_prefix = {tuple(int(v) for v in m) for m in PREFIX.findall(body)}

    found = MESSAGE.search(text)
    if not found:
        print('OSTIM SUPPORT MESSAGE CHECK FAILED (1)')
        print(f'  - {SOURCE_REL}: the refusal path no longer names the supported '
              f'versions, so a refused player is told nothing')
        return 1

    claim = found.group(1)
    claimed_exact: set[tuple[int, ...]] = set()
    claimed_prefix: set[tuple[int, ...]] = set()
    for major, minor, patch, suffix in TOKEN.findall(claim):
        triple = (int(major), int(minor), int(patch))
        if suffix == '.x':
            claimed_prefix.add(triple)
        elif suffix:
            claimed_exact.add(triple + (int(suffix[1:]),))
        else:
            claimed_exact.add(triple)

    # The corpus floor: both sides of every comparison below are extracted from
    # the source above. If either extraction comes back empty - the version
    # checks rewritten in another spelling, the message reworded - the four
    # loops below iterate over nothing and the run passes on nothing.
    if not accepted_exact and not accepted_prefix:
        failures.append(
            f'{SOURCE_REL}: SelectGraphLayout no longer states any accepted version, so the '
            f'comparisons below would run against an empty selector side and pass on nothing')
    if not claimed_exact and not claimed_prefix:
        failures.append(
            f'{SOURCE_REL}: the refusal message no longer names any version, so the comparisons '
            f'below would run against an empty message side and pass on nothing')

    # Every version the message names must actually be accepted, or the refusal
    # sends the player to a runtime that is refused too.
    for version in sorted(claimed_exact):
        if version not in accepted_exact:
            failures.append(
                f'{SOURCE_REL}: the message names {version[0]}.{version[1]}.{version[2]}.'
                f'{version[3]} as supported, but SelectGraphLayout does not accept it')
    for triple in sorted(claimed_prefix):
        if triple not in accepted_prefix and triple + (0,) not in accepted_exact:
            failures.append(
                f'{SOURCE_REL}: the message names {triple[0]}.{triple[1]}.{triple[2]}.x as '
                f'supported, but SelectGraphLayout has no prefix rule for it')

    # ...and every version the selector accepts must be named, or the message
    # omits a runtime that works.
    for version in sorted(accepted_exact):
        if version not in claimed_exact:
            failures.append(
                f'{SOURCE_REL}: SelectGraphLayout accepts {version[0]}.{version[1]}.'
                f'{version[2]}.{version[3]}, but the refusal message does not name it')
    for triple in sorted(accepted_prefix):
        if triple not in claimed_prefix:
            failures.append(
                f'{SOURCE_REL}: SelectGraphLayout accepts the whole {triple[0]}.{triple[1]}.'
                f'{triple[2]}.x series, but the refusal message does not name it')

    if failures:
        print(f'OSTIM SUPPORT MESSAGE CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('OSTIM SUPPORT MESSAGE CHECK OK')
    print(f'  message and SelectGraphLayout agree: '
          f'{", ".join(".".join(str(p) for p in v) for v in sorted(accepted_exact))}'
          + (f' and {".".join(str(p) for p in sorted(accepted_prefix)[0])}.x'
             if accepted_prefix else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
