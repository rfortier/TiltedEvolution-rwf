#!/usr/bin/env python3
"""Keep the address-library fallback from picking a library for another game.

`Code/skse_client/main.cpp` resolves the game version in three steps: the SKSE
module name (authoritative - SKSE is version-locked to the game), the exe's
version resource, and finally the address library file names in
`Data/SKSE/Plugins`. That last step exists because repacks and downgraders strip
or falsify the version resource, which is the case the file's own comment names.

It used to return the *first* matching file a directory_iterator produced. The
enumeration order of a directory is not defined, and the premise behind "the
first is the one" - that a modlist ships exactly one set of bins - is false: this
repository alone ships 23, and the installed game has 10.

Picking the wrong one is not a near miss. Measured over the 777,719 ids that
1.5.16 and 1.5.97 share, 767,644 (98.7%) resolve to a different RVA. A wrong
library therefore mis-resolves almost everything the client asks for.

The fix rests on two facts that this gate pins, because either can quietly stop
being true:

  1. **The two families are separated by file name.** Every `version-*.bin` is
     pre-AE (1.5.x) and every `versionlib-*.bin` is AE (1.6.x/1.7.x). The scan
     relies on that to tell them apart, so a file that breaks the pattern would
     make the filter wrong rather than merely incomplete.
  2. **Each runtime build reads only its own family.** The translation unit is
     compiled twice, and the legacy build carries SKYRIM_TARGET_LEGACY. If that
     define were dropped, the legacy runtime would start accepting `versionlib-*`
     names again.

    python Tools/Scripts/check_library_scan.py          # the gate
    python Tools/Scripts/check_library_scan.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Code' / 'skse_client' / 'main.cpp'
XMAKE = ROOT / 'Code' / 'skse_client' / 'xmake.lua'
LIB_DIRS = [
    ROOT / 'GameFiles' / 'Skyrim' / 'SKSE' / 'Plugins',
    pathlib.Path('D:/game/SkyrimSE/Data/SKSE/Plugins'),
]

# The two name shapes, and the family each one means.
#
# `Selectable` is the shape VersionDb::Load can actually open: it formats
# `versionlib-%d-%d-%d-%d.bin` (and the `version-` spelling) from four numbers, so
# a name with a fifth component can never be selected however it is spelled.
LEGACY_NAME = re.compile(r'^version-\d+-\d+-\d+-\d+\.bin$')
MODERN_NAME = re.compile(r'^versionlib-\d+-\d+-\d+-\d+\.bin$')

# A name that starts like an address library and carries an extra component.
# `versionlib-1-6-1170-0-1.bin` is the one in the tree: 795,210 bytes, tracked
# since the fork's "adapt to different versions" commit, referenced by nothing,
# and unreachable because no code path formats a fifth number. It is left in
# place deliberately - the fork author added it - so the gate records it rather
# than failing, and reports it so the next reader knows it is dead weight rather
# than a library something selects.
UNREACHABLE: dict[str, str] = {
    'versionlib-1-6-1170-0-1.bin': 'five components; VersionDb::Load only ever formats four, so '
                                   'nothing can open it',
}


def scan_function(text: str) -> str | None:
    """The body of ScanLibraryVersion, brace-matched."""
    match = re.search(r'std::string\s+ScanLibraryVersion\s*\([^)]*\)\s*\{', text)
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


def check(failures: list[str], warnings: list[str]) -> int:
    if not SOURCE.is_file():
        failures.append(f'{SOURCE.relative_to(ROOT).as_posix()} is missing')
        return 0

    text = SOURCE.read_text(encoding='utf-8', errors='replace')
    body = scan_function(text)
    if body is None:
        failures.append(
            'ScanLibraryVersion is gone; if the fallback was removed, drop this gate rather than '
            'leaving it covering nothing')
        return 0

    # 1. The scan must not return the first match: it has to consider every
    #    candidate and keep the highest, which is what "best[" marks.
    if 'best[' not in body:
        failures.append(
            'ScanLibraryVersion no longer tracks a best candidate; returning the first match makes '
            'the resolved version depend on directory enumeration order, which is not defined')
    if re.search(r'return\s+buf\s*;', body) and 'best[' not in body:
        failures.append('ScanLibraryVersion returns a value taken straight from the loop')

    # 2. The family filter must be present and driven by the build, not guessed.
    if 'SKYRIM_TARGET_LEGACY' not in body and 'kLegacy' not in body:
        failures.append(
            'ScanLibraryVersion no longer distinguishes the pre-AE and AE name families; without '
            'that, a 1.6.1170 install whose version resource is stripped can resolve to a 1.7 '
            'library, and the id namespaces are not interchangeable')
    elif 'legacyName != kLegacy' not in body.replace(' ', '') and \
            'legacyName!=kLegacy' not in body.replace(' ', ''):
        failures.append(
            'ScanLibraryVersion computes a family but no longer filters on it; the two name '
            'families must not be mixed')

    # 3. The build must actually define the macro the filter reads.
    if XMAKE.is_file():
        xmake = XMAKE.read_text(encoding='utf-8')
        legacy_targets = re.findall(r'build_runtime_dll\([^)]*SKYRIM_TARGET_LEGACY=1', xmake)
        if not legacy_targets:
            failures.append(
                f'{XMAKE.relative_to(ROOT).as_posix()} no longer builds a runtime with '
                f'SKYRIM_TARGET_LEGACY=1, so the family filter would be false in every build and '
                f'the legacy runtime would read AE library names')
    else:
        failures.append(
            f'{XMAKE.relative_to(ROOT).as_posix()} is missing, so the build define cannot be '
            f'checked. That file is tracked under version control, so finding it gone means it moved '
            f'rather than that every runtime still defines SKYRIM_TARGET_LEGACY')

    # 4. The premise the family filter rests on: the two name shapes really do
    #    partition by version. Checked against whatever libraries are present.
    checked = 0
    for directory in LIB_DIRS:
        if not directory.is_dir():
            continue
        for entry in sorted(directory.iterdir()):
            name = entry.name
            if not name.endswith('.bin'):
                continue
            is_legacy = bool(LEGACY_NAME.match(name))
            is_modern = bool(MODERN_NAME.match(name))
            if not is_legacy and not is_modern:
                # A .bin that is neither: fine, it is another library - but a
                # near miss on either pattern is worth knowing about.
                if re.match(r'^version(lib)?-\d', name):
                    if name in UNREACHABLE:
                        continue
                    failures.append(
                        f'{directory.as_posix()}/{name} starts like an address library but matches '
                        f'neither pattern; the family filter would skip it silently. If it is '
                        f'deliberately unselectable, record it in UNREACHABLE with the reason')
                continue
            version = re.match(r'^version(lib)?-(\d+)-(\d+)-', name)
            major, minor = int(version.group(2)), int(version.group(3))
            if is_legacy and (major, minor) != (1, 5):
                failures.append(
                    f'{directory.as_posix()}/{name} is named like a pre-AE library but is version '
                    f'{major}.{minor}; the family filter assumes version-* means 1.5.x')
            if is_modern and (major, minor) == (1, 5):
                failures.append(
                    f'{directory.as_posix()}/{name} is named like an AE library but is version '
                    f'{major}.{minor}; the family filter assumes versionlib-* means 1.6.x or newer')
            checked += 1

    # An empty scan is not a pass. The repository ships these libraries under
    # version control, so finding none of them means they moved or were deleted,
    # and the premise this gate exists for went unverified either way.
    if checked == 0:
        failures.append(
            f'no address library found in any of {", ".join(d.as_posix() for d in LIB_DIRS)}; the '
            f'name-family premise was not checked against a single real file. The repository ships '
            f'these libraries under version control, so an empty scan means they moved rather than '
            f'that the filter is correct')

    # A recorded unreachable file that is gone, or that is now selectable,
    # outlives its reason.
    seen_unreachable = set()
    for directory in LIB_DIRS:
        if directory.is_dir():
            seen_unreachable |= {e.name for e in directory.iterdir() if e.name in UNREACHABLE}
    for name, reason in UNREACHABLE.items():
        if name not in seen_unreachable:
            warnings.append(
                f'{name} is recorded as unreachable ({reason}) but is not present; if it was '
                f'removed, drop the entry')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        present = [d for d in LIB_DIRS if d.is_dir()]
        total = sum(1 for d in present for e in d.iterdir()
                    if LEGACY_NAME.match(e.name) or MODERN_NAME.match(e.name))
        print('checked:')
        print('  ScanLibraryVersion keeps the highest candidate rather than the first match')
        print('  it filters on the pre-AE / AE name family, driven by the build define')
        print('  the build still produces a SKYRIM_TARGET_LEGACY=1 runtime')
        print('  every version-*.bin really is 1.5.x and every versionlib-*.bin is not')
        print(f'  {total} library(ies) across {len(present)} directory(ies) on disk')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    if warnings:
        for warning in warnings:
            print(f'  note: {warning}')

    if failures:
        print(f'LIBRARY SCAN CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('LIBRARY SCAN CHECK OK')
    print(f'  the address-library fallback is family-scoped and picks the highest version '
          f'({checked} library file(s) checked)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
