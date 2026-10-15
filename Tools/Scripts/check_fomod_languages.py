#!/usr/bin/env python3
"""Keep the FOMOD wizard's language branches offering the same thing.

GameFiles/Skyrim/fomod/ModuleConfig.xml is written twice: one set of install steps
gated on `lang = zh` and one on `lang = en`. The submission checklist asks that an
edit to one branch be mirrored in the other (docs/PITFALLS.md section 0), because a
player who reads the wizard in one language must be able to install exactly what a
player reading it in the other language can.

The plugin NAMES are translated, so they cannot be compared - what can be compared
is the structure and the payload:

  R1  both branches declare the same number of install steps
  R2  step by step, both branches declare the same number of plugin options
  R3  step by step, both branches install the same folders and files

R3 is the one that matters: a plugin option added to the Chinese branch alone is
invisible to an English reader, and they simply cannot install that component - with
no error, because the wizard is doing exactly what its own file says.

    python Tools/Scripts/check_fomod_languages.py          # the gate
    python Tools/Scripts/check_fomod_languages.py --list   # show what is compared
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFIG = ROOT / 'GameFiles' / 'Skyrim' / 'fomod' / 'ModuleConfig.xml'

# The wizard must offer something; an empty or unparsed file is not a pass.
MIN_STEPS = 2
MIN_PLUGINS = 4


def branches(root: ET.Element) -> dict[str, list[dict]]:
    """installStep list per language flag, in document order."""
    out: dict[str, list[dict]] = {}
    for step in root.iter('installStep'):
        dependencies = [(f.get('flag'), f.get('value')) for f in step.iter('flagDependency')]
        lang = next((value for (flag, value) in dependencies if flag == 'lang'), None)
        if lang is None:
            continue
        payload = sorted(
            [f.get('source') for f in step.iter('file')]
            + [f.get('source') for f in step.iter('folder')]
        )
        out.setdefault(lang, []).append({
            'name': step.get('name') or '',
            'plugins': len(list(step.iter('plugin'))),
            'payload': payload,
        })
    return out


def check(failures: list[str], notes: list[str]) -> int:
    if not CONFIG.is_file():
        failures.append(f'{CONFIG.relative_to(ROOT).as_posix()} is missing; the wizard cannot be checked')
        return 0
    try:
        root = ET.parse(CONFIG).getroot()
    except ET.ParseError as exc:
        failures.append(f'{CONFIG.relative_to(ROOT).as_posix()} is not valid XML ({exc})')
        return 0

    langs = branches(root)
    if len(langs) < 2:
        failures.append(
            f'{CONFIG.relative_to(ROOT).as_posix()} declares {len(langs)} language branch(es); the '
            f'wizard is bilingual and this gate compares the branches against each other')
        return 0

    names = sorted(langs)
    checked = 0
    base_name = names[0]
    base = langs[base_name]
    if len(base) < MIN_STEPS:
        failures.append(f'{base_name} has {len(base)} install step(s), below the floor of {MIN_STEPS}')
    for other_name in names[1:]:
        other = langs[other_name]
        if len(base) != len(other):
            failures.append(
                f'{base_name} declares {len(base)} install step(s) and {other_name} declares '
                f'{len(other)}; a step present in one branch is invisible in the other')
            continue
        for index, (a, b) in enumerate(zip(base, other)):
            if a['plugins'] < MIN_PLUGINS and index == 0:
                pass  # the language picker itself is small; the floor is checked on the branch
            if a['plugins'] != b['plugins']:
                failures.append(
                    f'step {index} ({a["name"]!r} / {b["name"]!r}) offers {a["plugins"]} plugin '
                    f'option(s) in {base_name} and {b["plugins"]} in {other_name}; one branch is '
                    f'missing an option the other has')
                continue
            if a['payload'] != b['payload']:
                only_a = sorted(set(a['payload']) - set(b['payload']))
                only_b = sorted(set(b['payload']) - set(a['payload']))
                failures.append(
                    f'step {index} ({a["name"]!r} / {b["name"]!r}) installs different things: '
                    f'only in {base_name}: {only_a}, only in {other_name}: {only_b}; a player reading '
                    f'one language cannot install what the other can')
                continue
            checked += 1

    total_plugins = sum(step['plugins'] for step in base)
    if total_plugins < MIN_PLUGINS:
        failures.append(f'{base_name} offers {total_plugins} plugin option(s), below the floor of '
                        f'{MIN_PLUGINS}; the file shape changed')

    notes.append(f'{len(names)} language branch(es), {len(base)} step(s) each, '
                 f'{total_plugins} plugin option(s) compared')
    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        if CONFIG.is_file():
            root = ET.parse(CONFIG).getroot()
            for lang, steps in sorted(branches(root).items()):
                print(f'{lang}:')
                for i, step in enumerate(steps):
                    print(f'  step {i}: {step["plugins"]:2} plugin(s), {len(step["payload"]):2} '
                          f'install target(s)  {step["name"]!r}')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'FOMOD LANGUAGE CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('FOMOD LANGUAGE CHECK OK')
    print('  both wizard languages offer the same options and install the same files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
