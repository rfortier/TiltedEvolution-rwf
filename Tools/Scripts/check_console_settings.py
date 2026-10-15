#!/usr/bin/env python3
"""Keep every console setting name unique, because a name resolves to one object.

Console::Setting objects register themselves into a single global list as they are
constructed (SettingBase's constructor links itself onto ROOT()), and
RegisterServerSettings binds that whole list into the console registry. Two
settings that share a name are therefore two objects with one address, and:

  * ConsoleRegistry::FindSetting resolves a name with std::find_if, so the FIRST
    entry wins and the second is unreachable by name - the ini loader and every
    console command write to the first;
  * construction order is static-initialisation order, which is unspecified across
    translation units, so which of the two wins is not even stable between builds;
  * the loser keeps its construction-time default for the life of the process.

LiveServices:bAnnounceServer was declared in both GameServer.cpp and
CommandService.cpp. Both copies were read by real logic - IsPublicServer() feeds the
public-server decision and AllowsAutoPartyJoin(), and CommandService gates a party
leader's /settime on it - so the setting silently did one of two different things
depending on the link order. See docs/PITFALLS.md section 136.

    python Tools/Scripts/check_console_settings.py          # the gate
    python Tools/Scripts/check_console_settings.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'Code'

# Console::Setting <name>{"<key>"[, ...]}. The name is the first string literal:
# every declaration in the tree opens that way.
SETTING = re.compile(r'Console::Setting\b[^;{]*\{\s*"([^"]+)"')

# Comments are stripped before matching. The first version of this gate did not,
# and the comment that explains the removal of the CommandService duplicate quotes
# the declaration it removed - so the gate reported the duplicate as still present.
# This repository has already paid for this once: the address-library id collector
# used to count commented-out calls as live (docs/PITFALLS.md section 3.1).
LINE_COMMENT = re.compile(r'//[^\n]*')
BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)

# Named settings the tree declares today. A drop below this means the pattern
# stopped matching (a different spelling, a macro, a moved directory) and the gate
# would otherwise pass by finding nothing to compare.
MIN_SETTINGS = 15


def check(failures: list[str], notes: list[str]) -> int:
    found: dict[str, list[str]] = collections.defaultdict(list)
    for path in sorted(CLIENT.rglob('*')):
        if path.suffix not in ('.cpp', '.h', '.hpp', '.inl'):
            continue
        text = path.read_text(encoding='utf-8', errors='ignore')
        text = LINE_COMMENT.sub('', BLOCK_COMMENT.sub('', text))
        for match in SETTING.finditer(text):
            line = text[:match.start()].count('\n') + 1
            found[match.group(1)].append(f'{path.relative_to(ROOT).as_posix()}:{line}')

    total = sum(len(v) for v in found.values())
    if total < MIN_SETTINGS:
        failures.append(
            f'only {total} Console::Setting declaration(s) were found, below the floor of '
            f'{MIN_SETTINGS}; the declaration shape changed and this gate is no longer looking at '
            f'the settings it claims to')
        return total

    for name, sites in sorted(found.items()):
        if len(sites) > 1:
            failures.append(
                f'"{name}" is declared {len(sites)} times ({", ".join(sites)}); a name resolves to the '
                f'first registration, so the others never receive the ini value and keep their default '
                f'- and which one wins depends on static initialisation order')

    notes.append(f'{total} setting(s) across {len({s.split(":")[0] for v in found.values() for s in v})} file(s), '
                 f'{len(found)} distinct name(s)')
    return total


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  every Console::Setting name is declared exactly once')
        print(f'  (a name that appears twice resolves to whichever registers first, which is static-init order)')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    total = check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'CONSOLE SETTING CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('CONSOLE SETTING CHECK OK')
    print(f'  {total} setting declaration(s), every name unique')
    return 0


if __name__ == '__main__':
    sys.exit(main())
