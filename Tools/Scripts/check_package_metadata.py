#!/usr/bin/env python3
"""Keep the packaged installer's metadata in step with the release it ships.

`GameFiles/Skyrim/fomod/info.xml` is what Mod Organizer shows at the top of the
FOMOD wizard: name, author, version, website. The release workflow stamps the
version from the tag while the zip is assembled, so the number in the committed
file is only what a hand-made package shows - and its own comment says to keep it
in step with the newest tag.

It had drifted four releases behind: the file still said 1.1.5 while the newest
tag this repository cut was v1.1.9. Nothing noticed, because "keep it in step"
was a comment, and a comment does not run. Anyone assembling a package by hand,
or reading the file to see what version it describes, got a number from four
releases ago.

Two claims are checked, and the second is the one that matters for this goal:

  * the committed version is the newest tag of this repository's own series, and
  * the description names the game versions the build actually supports.

The tag namespace is shared with upstream (see check_docs.py's OWN_TAG_SERIES):
a local clone carries upstream's v1.0.x ... v1.8.x as well, so "the newest tag"
is not the answer - "the newest tag of this repository's series" is.

    python Tools/Scripts/check_package_metadata.py          # the gate
    python Tools/Scripts/check_package_metadata.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
INFO_XML = ROOT / 'GameFiles' / 'Skyrim' / 'fomod' / 'info.xml'

# The tag series this repository cuts itself. Same list and same reason as
# check_docs.py: upstream's tags share the namespace, so a tag name alone cannot
# say whose release it is.
OWN_TAG_SERIES = ('1.1.',)

# The game versions the build supports, as the runtime's own version gate spells
# them (Code/client/main.cpp checks a "1.5." / "1.6." / "1.7." prefix). The
# description is player-facing, so a stale range tells a 1.7.x player the mod is
# not for them.
SUPPORTED_PREFIXES = ('1.5.', '1.6.', '1.7.')

VERSION_TAG = re.compile(r'^v(\d+\.\d+\.\d+)$')


def tags() -> list[str]:
    try:
        result = subprocess.run(['git', 'tag', '--list', 'v*'], cwd=str(ROOT),
                                capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return []
    return [t.strip() for t in result.stdout.splitlines() if t.strip()]


def newest_own_version() -> str | None:
    """The highest tag of this repository's own series, without the leading v."""
    best: tuple[int, ...] | None = None
    for tag in tags():
        match = VERSION_TAG.match(tag)
        if not match:
            continue
        version = match.group(1)
        if not version.startswith(OWN_TAG_SERIES):
            continue
        parts = tuple(int(p) for p in version.split('.'))
        if best is None or parts > best:
            best = parts
    return '.'.join(str(p) for p in best) if best else None


def check(failures: list[str], warnings: list[str]) -> int:
    if not INFO_XML.is_file():
        failures.append(
            f'{INFO_XML.relative_to(ROOT).as_posix()} is missing; the installer wizard would show '
            f'empty name/author/version fields')
        return 0

    try:
        root = ET.parse(INFO_XML).getroot()
    except ET.ParseError as exc:
        failures.append(f'{INFO_XML.relative_to(ROOT).as_posix()} is not valid XML: {exc}')
        return 0

    checked = 0

    # 1. The version matches the newest tag this repository cut.
    node = root.find('Version')
    committed = (node.text or '').strip() if node is not None else ''
    expected = newest_own_version()
    if expected is None:
        failures.append(
            'no tag of this repository\'s own series is visible, so the committed version was '
            'never compared against a release; a checkout without tags is not evidence that the '
            'metadata is in step. The workflow fetches them (fetch-depth: 0, fetch-tags: true) '
            'because check_docs.py reads the same list and refuses to pass on an empty one')
    elif not committed:
        failures.append(f'{INFO_XML.relative_to(ROOT).as_posix()} has no <Version>; the wizard '
                        f'would show a blank version')
    else:
        checked += 1
        if committed != expected:
            failures.append(
                f'{INFO_XML.relative_to(ROOT).as_posix()} says version {committed} but the newest '
                f'tag of this repository\'s series is v{expected}. A hand-made package, or anyone '
                f'reading the file, then gets a version from an older release')

    # 2. The description names the game versions the build supports. This is the
    #    compatibility claim a player reads before installing, and it is prose, so
    #    nothing else checks it.
    description = root.find('Description')
    text = ''.join(description.itertext()) if description is not None else ''
    if not text.strip():
        failures.append(f'{INFO_XML.relative_to(ROOT).as_posix()} has no <Description>; a player '
                        f'installing the package sees nothing about which game versions it is for')
    else:
        checked += 1
        missing = [p for p in SUPPORTED_PREFIXES if p not in text]
        if missing:
            failures.append(
                f'{INFO_XML.relative_to(ROOT).as_posix()} does not name the supported game version '
                f'prefix(es) {missing}; the runtime accepts {list(SUPPORTED_PREFIXES)}, so a player '
                f'on one of them is told the package is not for them')
        # The reverse: a prefix the description claims but the runtime rejects.
        claimed = set(re.findall(r'\b(\d+\.\d+)\.x\b', text))
        supported = {p.rstrip('.') for p in SUPPORTED_PREFIXES}
        extra = sorted(claimed - supported)
        if extra:
            failures.append(
                f'{INFO_XML.relative_to(ROOT).as_posix()} claims support for {extra}, which the '
                f'runtime\'s version gate does not accept ({sorted(supported)})')

    # 3. The two fields the wizard needs to not be blank.
    for field in ('Name', 'Author'):
        node = root.find(field)
        if node is None or not (node.text or '').strip():
            failures.append(f'{INFO_XML.relative_to(ROOT).as_posix()} has no <{field}>; the wizard '
                            f'shows that field blank')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        expected = newest_own_version()
        print('checked:')
        print(f'  {INFO_XML.relative_to(ROOT).as_posix()} names the newest tag of this repository\'s '
              f'series')
        print('  its description names the game versions the runtime accepts, and no others')
        print('  the fields the wizard shows are present and non-empty')
        print(f'  own tag series: {list(OWN_TAG_SERIES)}')
        print(f'  runtime accepts: {list(SUPPORTED_PREFIXES)}')
        print(f'  newest own tag : {("v" + expected) if expected else "(none found)"}')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    if warnings:
        for warning in warnings:
            print(f'  note: {warning}')

    if failures:
        print(f'PACKAGE METADATA CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('PACKAGE METADATA CHECK OK')
    print(f'  installer metadata matches the newest release and names the supported game versions '
          f'({checked} field group(s) checked)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
