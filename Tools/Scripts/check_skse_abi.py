#!/usr/bin/env python3
"""Keep the hand-written SKSE ABI mirrors matching the layout SKSE reads.

Two structs in this tree are written out by hand instead of being included from
SKSE's headers, because the plugin does not link against them:

  * SksePluginVersionData (Code/skse_bootstrap/main.cpp) - SKSE reads this straight
    out of the dll's exports by offset. It never sees the field names. A field
    added, an array resized, or two fields swapped moves everything after it, and
    SKSE then reads the plugin's name as its author and its flags out of the
    version list. Nothing fails: the export is still present and still called
    SKSEPlugin_Version, so the plugin loads with garbage metadata or is rejected
    for a reason that has nothing to do with the edit.
  * SksePluginInfo (same file) - the older SKSEPlugin_Query interface.

The reference layout is SKSE64's skse64/PluginAPI.h, which has been stable since
2.0.x. It is written out here as data, so the check is "does the declaration still
describe the interface SKSE implements", not "does it match itself".

    python Tools/Scripts/check_skse_abi.py          # the gate
    python Tools/Scripts/check_skse_abi.py --list   # show what is compared
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BOOTSTRAP = 'Code/skse_bootstrap/main.cpp'

# name -> (field type, array length or None), in declaration order, from SKSE64's
# skse64/PluginAPI.h (stable since 2.0.x; still the layout in 2.2.x).
EXPECTED: dict[str, list[tuple[str, str, int | None]]] = {
    'SksePluginVersionData': [
        ('uint32_t', 'dataVersion', None),
        ('uint32_t', 'pluginVersion', None),
        ('char', 'name', 256),
        ('char', 'author', 256),
        ('char', 'supportEmail', 252),
        ('uint32_t', 'versionIndependenceEx', None),
        ('uint32_t', 'versionIndependence', None),
        ('uint32_t', 'compatibleVersions', 16),
        ('uint32_t', 'seVersionRequired', None),
    ],
    'SksePluginInfo': [
        ('uint32_t', 'infoVersion', None),
        ('constchar*', 'name', None),
        ('uint32_t', 'version', None),
    ],
}

# The flag values SKSE checks, as (name, expected value, the expression the tree is
# allowed to spell it with). A wrong bit is a plugin SKSE refuses to load, with a
# message about the game version rather than about the plugin. The expression is
# compared rather than the literal because `1 << 1` is how the header states it.
EXPECTED_CONSTANTS = [
    ('kVersion', 1, '1'),
    ('kVersionIndependent_Signatures', 2, '1 << 1'),
    ('kVersionIndependentEx_NoStructUse', 1, '1 << 0'),
]

STRUCT = r'struct\s+{name}\s*\{{(.*?)\n\}};'
FIELD = r'\s*(const\s+char\s*\*|char|uint\d+_t|std::uint\d+_t|int\d+_t)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;'


def read(rel: str) -> str:
    path = ROOT / rel
    return path.read_text(encoding='utf-8', errors='ignore') if path.is_file() else ''


def declared(name: str) -> list[tuple[str, str, int | None]]:
    match = re.search(STRUCT.format(name=name), read(BOOTSTRAP), re.S)
    if not match:
        return []
    out = []
    for line in match.group(1).splitlines():
        m = re.match(FIELD, line)
        if m:
            # normalise `std::uint32_t` / `const char *` spellings
            kind = re.sub(r'\s+', '', m.group(1)).replace('std::', '')
            out.append((kind, m.group(2), int(m.group(3)) if m.group(3) else None))
    return out


def check(failures: list[str], notes: list[str]) -> int:
    text = read(BOOTSTRAP)
    if not text:
        failures.append(f'{BOOTSTRAP} is missing; the SKSE ABI mirrors cannot be checked')
        return 0

    checked = 0
    for name, expected in EXPECTED.items():
        found = declared(name)
        if not found:
            failures.append(
                f'{BOOTSTRAP}: struct {name} could not be read; it is a hand-written mirror of an '
                f'external ABI and nothing else would notice it changing')
            continue
        if len(found) != len(expected):
            failures.append(
                f'{BOOTSTRAP}: {name} declares {len(found)} field(s), SKSE64\'s header declares '
                f'{len(expected)}; every field after the difference is read from the wrong offset')
        for index, (want, got) in enumerate(zip(expected, found)):
            if want == got:
                checked += 1
                continue
            failures.append(
                f'{BOOTSTRAP}: {name} field {index} is {got[0]} {got[1]}'
                f'{f"[{got[2]}]" if got[2] else ""}, but SKSE reads {want[0]} {want[1]}'
                f'{f"[{want[2]}]" if want[2] else ""} there')

    for member, value, expression in EXPECTED_CONSTANTS:
        # The value as declared, whether spelled as a literal or as a shift.
        declared_value = None
        match = re.search(rf'\b{member}\s*=\s*([^,\n}}]+)', text)
        if match:
            raw = match.group(1).strip()
            if re.fullmatch(r'\d+', raw):
                declared_value = int(raw)
            else:
                shift = re.fullmatch(r'(\d+)\s*<<\s*(\d+)', raw)
                if shift:
                    declared_value = int(shift.group(1)) << int(shift.group(2))
        if declared_value == value:
            checked += 1
            continue
        failures.append(
            f'{BOOTSTRAP}: {member} is {match.group(1).strip() if match else "not declared"}, but SKSE '
            f'checks {value} ({expression}); a wrong bit is refused with a message about the game '
            f'version instead of about the plugin')

    if checked == 0:
        failures.append('nothing was checked; the structs or the constants moved')
    notes.append(f'{checked} field/constant(s) matched against the SKSE64 layout')
    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        for name, fields in EXPECTED.items():
            print(f'{name}: {len(fields)} field(s)')
            for kind, field, length in fields:
                print(f'    {kind} {field}{f"[{length}]" if length else ""}')
        for member, value, expression in EXPECTED_CONSTANTS:
            print(f'SksePluginVersionData::{member} = {value} ({expression})')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'SKSE ABI CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('SKSE ABI CHECK OK')
    print('  the hand-written mirrors still describe the interface SKSE implements')
    return 0


if __name__ == '__main__':
    sys.exit(main())
