#!/usr/bin/env python3
"""Keep the two Papyrus variable-type tables in agreement.

The Papyrus variable type tag is defined twice in this tree, by two components that
never see each other's code:

  * Code/components/es_loader/Records/Chunks.h - `Type`, used while parsing a .pex
    script's chunks (Chunks.cpp switches on OBJECT_ARRAY);
  * Code/client/Games/Skyrim/Misc/BSScript.h - `BSScript::Variable::Type`, used when
    registering natives with the VM (BSScript.cpp passes kBoolean to
    IsRemotePlayerFunc, and the VM hands return types back through it).

Same tag space, two spellings, no link between them: INVALID/OBJECT/WSTRING/INT/
FLOAT/BOOL on one side and kEmpty/kObject/kString/kInteger/kFloat/kBoolean on the
other. The numbers agree today, and nothing was holding them together. An edit to
either table alone would mis-decode a Papyrus variable - silently, because both
enums still compile and each side still answers its own questions consistently.

Two rules, both mechanical:

  R1  the six scalar tags have the same value on both sides
  R2  every array tag the loader knows is below the client's kMax, because that is
      the table the VM is asked about

    python Tools/Scripts/check_papyrus_types.py          # the gate
    python Tools/Scripts/check_papyrus_types.py --list   # show what is compared
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

LOADER = 'Code/components/es_loader/Records/Chunks.h'
CLIENT = 'Code/client/Games/Skyrim/Misc/BSScript.h'

# The two names for each scalar tag, in the order the tags are numbered. The pairs
# are written out because no name is shared between the tables - that is the whole
# problem this gate exists for.
SCALARS = [
    ('INVALID', 'kEmpty'),
    ('OBJECT', 'kObject'),
    ('WSTRING', 'kString'),
    ('INT', 'kInteger'),
    ('FLOAT', 'kFloat'),
    ('BOOL', 'kBoolean'),
]

MIN_SCALARS = 6
MIN_ARRAYS = 4


def enum_entries(path: str, name: str) -> dict[str, int]:
    text = (ROOT / path).read_text(encoding='utf-8', errors='ignore')
    match = re.search(rf'enum\s+(?:class\s+)?{name}\s*(?::[^{{]+)?\{{(.*?)\}};', text, re.S)
    if not match:
        return {}
    out: dict[str, int] = {}
    position = 0
    for part in match.group(1).split(','):
        part = re.sub(r'//.*', '', part).strip()
        if not part:
            continue
        if '=' in part:
            label, _, value = part.partition('=')
            label, value = label.strip(), value.strip()
            try:
                position = int(value, 0)
            except ValueError:
                continue
        else:
            label = part
        if re.fullmatch(r'\w+', label):
            out[label] = position
            position += 1
    return out


def check(failures: list[str], notes: list[str]) -> int:
    loader = enum_entries(LOADER, 'Type')
    client = enum_entries(CLIENT, 'Type')
    if not loader:
        failures.append(f'{LOADER}: the Type enum could not be read; the loader half of the tag space '
                        f'is unchecked')
        return 0
    if not client:
        failures.append(f'{CLIENT}: the Type enum could not be read; the client half of the tag space '
                        f'is unchecked')
        return 0

    checked = 0
    for loader_name, client_name in SCALARS:
        if loader_name not in loader or client_name not in client:
            failures.append(
                f'{loader_name}/{client_name} is missing from one of the tables; the scalar tags are '
                f'numbered implicitly, so a removed entry renumbers the ones after it')
            continue
        checked += 1
        if loader[loader_name] != client[client_name]:
            failures.append(
                f'{loader_name} is {loader[loader_name]} in {LOADER} but its counterpart {client_name} '
                f'is {client[client_name]} in {CLIENT}; the two components would disagree about what a '
                f'variable of that type is')

    if checked < MIN_SCALARS:
        failures.append(f'only {checked} scalar tag(s) could be compared, below the floor of '
                        f'{MIN_SCALARS}; the enums or the pairing changed')

    arrays = {name: value for name, value in loader.items() if 'ARRAY' in name}
    if len(arrays) < MIN_ARRAYS:
        failures.append(f'only {len(arrays)} array tag(s) were found in {LOADER}, below the floor of '
                        f'{MIN_ARRAYS}; the loader table changed shape')
    if 'kMax' not in client:
        failures.append(f'{CLIENT}: kMax is gone; the array tags can no longer be checked against the '
                        f'range the VM is asked about')
    else:
        for name, value in sorted(arrays.items()):
            if value >= client['kMax']:
                failures.append(
                    f'{name} is {value} in {LOADER}, at or above the client\'s kMax '
                    f'({client["kMax"]}); the VM table it is read against does not reach that far')
            else:
                checked += 1

    notes.append(f'{checked} tag(s) compared between {pathlib.Path(LOADER).name} and '
                 f'{pathlib.Path(CLIENT).name}')
    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        loader = enum_entries(LOADER, 'Type')
        client = enum_entries(CLIENT, 'Type')
        print(f'{LOADER}: {loader}')
        print(f'{CLIENT}: {client}')
        print('paired:')
        for loader_name, client_name in SCALARS:
            print(f'  {loader_name}={loader.get(loader_name)}  <->  {client_name}={client.get(client_name)}')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'PAPYRUS TYPE CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('PAPYRUS TYPE CHECK OK')
    print('  the loader and the client still number the Papyrus variable types the same')
    return 0


if __name__ == '__main__':
    sys.exit(main())
