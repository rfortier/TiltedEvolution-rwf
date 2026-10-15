#!/usr/bin/env python3
"""Keep the strings' placeholders, the languages, and the call sites agreeing.

Transloco substitutes the parameters it is handed and leaves any placeholder it
was not handed as literal text, so a mismatch is something the player reads:

  * a language whose translation drops `{{length}}` renders "s" instead of "30s";
  * a call site that passes the wrong name renders `{{length}}` into the panel;
  * the error panel builds its key from the code - 'SERVICE.ERROR.ERRORS.' +
    code.toUpperCase() - so which parameters that key needs is decided by the
    catalog, while which parameters the panel has is decided by the interface in
    error.service.ts. Nothing connected the two.

Three rules, all mechanical:

  R1  a key's placeholder set is the same in every catalog
  R2  a call site that names a key and hands over parameters hands over the ones
      the key asks for
  R3  a code whose key is built at runtime has an interface that carries the
      parameters that key asks for

The dropdown's `[text]` bindings are deliberately out of R2's scope: that
component injects `{ length: option.value }` itself (dropdown.component.html),
so a bare key there is the intended shape, not a missing parameter.

    python Tools/Scripts/check_i18n_params.py          # the gate
    python Tools/Scripts/check_i18n_params.py --list   # show what is compared
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
I18N = ROOT / 'Code' / 'skyrim_ui' / 'src' / 'assets' / 'i18n'
UI = ROOT / 'Code' / 'skyrim_ui' / 'src'
ERROR_SERVICE = UI / 'app' / 'services' / 'error.service.ts'
FALLBACK = 'en.json'
OVERLAY = 'overwrite.json'  # a build-time overlay, not a language

PLACEHOLDER = re.compile(r'\{\{(\w+)\}\}')
KEY = r"['\"]([A-Z][A-Z0-9_.]{3,})['\"]"
# One level of nesting is enough for the parameter objects in this tree.
OBJ = r'(\{(?:[^{}]|\{[^{}]*\})*\})'
# The three ways a key and its parameters are written here. Each shape anchors on
# the key, so a site matches exactly one of them.
#
#   translate('KEY', { a })        a call, key first
#   'KEY' | transloco: { a }       the pipe, parameters after the colon
#   'KEY', params: { a }           a data object carrying them beside the key
#
# `selectTranslate` has to be spelled out: `\btranslate` does not match it (no word
# boundary inside the name, and a different case) - which is how the first version
# of this gate saw five call sites instead of twelve. The pipe and the data object
# were missing entirely, and between them they cover the settings, the party menu,
# the server list and the popup notifications.
SHAPES = [
    re.compile(r'(?:pushSystemMessage\(|selectTranslate(?:<[^>]*>)?\(|\btranslate(?:<[^>]*>)?\()\s*'
               + KEY + r'\s*,?\s*' + OBJ + r'?'),
    re.compile(KEY + r'\s*\|\s*transloco\s*(?::\s*' + OBJ + r')?'),
    re.compile(KEY + r'\s*,\s*(?:params|messageParams)\s*:\s*' + OBJ),
]
# interface X extends ErrorEvent { error: 'code'; data: { a: T; b: T }; }
INTERFACE = re.compile(r"error:\s*'(\w+)'\s*;[\s\S]{0,400}?data:\s*\{([^}]*)\}", re.S)
DYNAMIC_KEY = re.compile(r"'SERVICE\.ERROR\.ERRORS\.'\s*\+\s*(\w+)\.error\.toUpperCase\(\)")

MIN_KEYS = 10
MIN_SITES = 10


def flatten(obj, prefix: str = '') -> dict[str, str]:
    out: dict[str, str] = {}
    if isinstance(obj, dict):
        for key, value in obj.items():
            out.update(flatten(value, f'{prefix}{key}.'))
    else:
        out[prefix.rstrip('.')] = obj
    return out


def catalogs() -> dict[str, dict[str, str]]:
    out: dict[str, dict[str, str]] = {}
    for path in sorted(I18N.glob('*.json')):
        if path.name == OVERLAY:
            continue
        try:
            out[path.name] = flatten(json.loads(path.read_text(encoding='utf-8')))
        except json.JSONDecodeError:
            continue
    return out


def placeholders(text: str) -> set[str]:
    return set(PLACEHOLDER.findall(str(text)))


def param_names(obj: str) -> set[str]:
    """The names a parameter object supplies.

    Two shapes the first version of this gate did not know, both of which made it
    report working code as broken:

      * shorthand properties - `{ cmds }` is the idiom here, and it supplies a
        parameter named `cmds`;
      * `;` between members, which is how a TypeScript interface body separates
        them, so `data: { version: string; expectedVersion: string }` carries two.
    """
    names: set[str] = set()
    for part in re.split(r'[,;]', obj.strip('{}')):
        part = part.strip()
        if not part:
            continue
        named = re.match(r'([\w$]+)\s*[:,]', part)
        if named:
            names.add(named.group(1))
            continue
        shorthand = re.fullmatch(r'([\w$]+)', part)
        if shorthand:
            names.add(shorthand.group(1))
    return names


def check(failures: list[str], notes: list[str]) -> tuple[int, int]:
    cats = catalogs()
    if FALLBACK not in cats:
        failures.append(f'{I18N.relative_to(ROOT).as_posix()}/{FALLBACK} is missing; the fallback language '
                        f'is what every other one is compared against')
        return 0, 0

    reference = {k: v for k, v in cats[FALLBACK].items() if placeholders(v)}
    if len(reference) < MIN_KEYS:
        failures.append(f'only {len(reference)} key(s) with placeholders were found in {FALLBACK}, below '
                        f'the floor of {MIN_KEYS}; the catalogs or the placeholder syntax changed')
        return 0, 0

    # R1
    for name, keys in sorted(cats.items()):
        if name == FALLBACK:
            continue
        for key, needs in sorted(reference.items()):
            if key not in keys:
                continue  # a missing key falls back to en, and section 137 reported that separately
            if placeholders(keys[key]) != placeholders(needs):
                failures.append(
                    f'{name}: {key} needs {sorted(placeholders(needs))} but this translation has '
                    f'{sorted(placeholders(keys[key])) or "none"}; transloco leaves the placeholder it '
                    f'was not handed in the text the player reads')

    # R2
    sites = 0
    for path in sorted(list(UI.rglob('*.ts')) + list(UI.rglob('*.html'))):
        text = path.read_text(encoding='utf-8', errors='ignore')
        seen: set[int] = set()
        for shape in SHAPES:
            for match in shape.finditer(text):
                if match.start() in seen:
                    continue
                seen.add(match.start())
                key = match.group(1)
                params = match.group(2) if match.lastindex and match.lastindex >= 2 else None
                if key not in reference:
                    continue
                sites += 1
                given = param_names(params) if params else set()
                missing = placeholders(reference[key]) - given
                if missing:
                    line = text[:match.start()].count('\n') + 1
                    failures.append(
                        f'{path.relative_to(ROOT).as_posix()}:{line}: {key} is rendered without '
                        f'{sorted(missing)}; the text asks for it, so the placeholder itself would be shown')
    if sites < MIN_SITES:
        failures.append(f'only {sites} call site(s) naming a parameterised key were parsed, below the '
                        f'floor of {MIN_SITES}; the call shape changed and R2 is checking nothing')

    # R3
    service = ERROR_SERVICE.read_text(encoding='utf-8', errors='ignore') if ERROR_SERVICE.is_file() else ''
    dynamic = DYNAMIC_KEY.search(service)
    if not dynamic:
        failures.append(f'{ERROR_SERVICE.relative_to(ROOT).as_posix()}: the runtime key build '
                        f"('SERVICE.ERROR.ERRORS.' + code) was not found; R3 would compare the wrong things")
        return len(reference), sites

    codes = {code: param_names(body) for code, body in INTERFACE.findall(service)}
    if not codes:
        failures.append(f'{ERROR_SERVICE.relative_to(ROOT).as_posix()}: no error interface declares its '
                        f'data shape, so R3 cannot tell which parameters a code carries')
    for code, given in sorted(codes.items()):
        key = f'SERVICE.ERROR.ERRORS.{code.upper()}'
        if key not in reference:
            continue
        missing = placeholders(reference[key]) - given
        if missing:
            failures.append(
                f'{ERROR_SERVICE.relative_to(ROOT).as_posix()}: the {code!r} interface carries {sorted(given) or "no parameters"}, '
                f'but {key} asks for {sorted(missing)}; the panel builds that key at runtime, so the '
                f'placeholder would be shown')
    notes.append(f'{len(reference)} parameterised key(s), {sites} call site(s), {len(codes)} runtime code(s)')
    return len(reference), sites


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        cats = catalogs()
        reference = {k: v for k, v in cats.get(FALLBACK, {}).items() if placeholders(v)}
        print(f'languages: {sorted(cats)} ({len(cats)})')
        for key, text in sorted(reference.items()):
            print(f'  {key:<48} needs {sorted(placeholders(text))}')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    keys, sites = check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'I18N PARAM CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('I18N PARAM CHECK OK')
    print(f'  {keys} parameterised key(s) agree across languages, call sites and runtime codes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
