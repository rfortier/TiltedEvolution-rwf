#!/usr/bin/env python3
"""Keep the xmake the build requires below the xmake CI installs.

Two declarations, in two files, that the submission checklist asks to keep in step
(docs/PITFALLS.md section 0):

  * `xmake.lua` opens with `set_xmakever("3.0.0")` - the floor. xmake itself refuses
    to run a project whose floor is above it, with a message about the project rather
    than about the toolchain;
  * `.github/workflows/windows.yml` pins `xmake-version: 3.1.0` on
    xmake-io/github-action-setup-xmake - which xmake gets installed.

They are not the same kind of declaration, so "keep them equal" is not the rule and
was never achievable: the real invariant is that every installed version is at or
above the floor. Raising the floor without touching CI breaks every build; lowering
the pin below the floor does the same. Nothing checked either direction.

    python Tools/Scripts/check_xmake_version.py          # the gate
    python Tools/Scripts/check_xmake_version.py --list   # show what is compared
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = 'xmake.lua'
WORKFLOWS = ROOT / '.github' / 'workflows'

FLOOR = re.compile(r'set_xmakever\s*\(\s*["\']([\d.]+)["\']\s*\)')
PIN = re.compile(r'xmake-version:\s*([\d.]+)')


def version(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in text.split('.') if part.isdigit())


def check(failures: list[str], notes: list[str]) -> int:
    build = (ROOT / BUILD).read_text(encoding='utf-8', errors='ignore') if (ROOT / BUILD).is_file() else ''
    match = FLOOR.search(build)
    if not match:
        failures.append(
            f'{BUILD} has no set_xmakever(); the floor the project needs is undeclared, so nothing can '
            f'tell whether the CI toolchain satisfies it')
        return 0
    floor_text = match.group(1)
    floor = version(floor_text)

    pins: list[tuple[str, str]] = []
    for path in sorted(WORKFLOWS.glob('*.yml')):
        text = path.read_text(encoding='utf-8', errors='ignore')
        for value in PIN.findall(text):
            pins.append((path.name, value))

    if not pins:
        failures.append(
            f'no xmake-version pin was found under {WORKFLOWS.relative_to(ROOT).as_posix()}; the '
            f'toolchain the builds use is then whatever the runner happens to have, and the floor '
            f'{floor_text} is unenforced')
        return 0

    for name, value in pins:
        if version(value) < floor:
            failures.append(
                f'{name} installs xmake {value}, below the {floor_text} that {BUILD} requires; every '
                f'build in that workflow would fail with a message about the project rather than about '
                f'the toolchain')

    notes.append(f'{BUILD} requires >= {floor_text}; workflows install '
                 + ', '.join(f'{name} {value}' for name, value in pins))
    return len(pins)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        build = (ROOT / BUILD).read_text(encoding='utf-8', errors='ignore')
        print(f'{BUILD}: {FLOOR.search(build).group(1)} (floor)')
        for path in sorted(WORKFLOWS.glob('*.yml')):
            for value in PIN.findall(path.read_text(encoding='utf-8', errors='ignore')):
                print(f'{path.name}: {value} (installed)')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'XMAKE VERSION CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('XMAKE VERSION CHECK OK')
    print('  every workflow installs an xmake at or above the version the build requires')
    return 0


if __name__ == '__main__':
    sys.exit(main())
