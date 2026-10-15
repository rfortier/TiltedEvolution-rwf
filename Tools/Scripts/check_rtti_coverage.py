#!/usr/bin/env python3
"""Hold the client's RTTI registration table against every shipped address library.

`Cast<T>(ptr)` (Code/client/Games/Skyrim/RTTI.h:17-27) resolves two ids at runtime,
one for the target type `T` and one for the type the pointer is written as, through
`internal::RttiLocator<X>::Get()`. The locator is built with aAllowUnresolvedStub
false (Code/client/Games/Skyrim/RTTI.cpp:12), so an id the loaded library does not
carry stays null and `Cast()` returns nullptr: it does not crash, it does not log,
and it leaves the caller to decide what a missing engine object means.

Whether an id is carried is a property of the shipped `versionlib-*.bin` and
`versionlib-ae-to-se-*.map` files, not of this tree, and the gaps cannot be closed
here: those are official SKSE release artifacts, and rewriting one breaks its hash,
which `CheckAddressLibrary` may reject. So the gate records the gaps and holds them:

  * an id a shipped library does not carry and nothing declares is a failure - a new
    registration that no runtime can resolve would otherwise pass unnoticed;
  * a declaration whose gap is gone is a failure - a stale entry keeps a reader
    believing a runtime is short of an id it now carries;
  * a declared gap reached by a `Cast<>` argument is a failure at the call site -
    this is what turns "watch it at review time" into something a machine checks.

What the gate cannot do:
  * it cannot close a gap (above), only record it;
  * it cannot see the source side of a cast. `Cast<T, U>` deduces `U` from the pointer
    at the call site, so the id resolved for `U` is nowhere in the tree; only `T` is
    written down, and only `T` is checked here;
  * it cannot say what a null answer costs a caller. It names the site and the
    runtimes; whether that call site survives the null is a review question;
  * it reads the RTTI table only. Ids reached through POINTER_SKYRIMSE belong to
    check_map_coverage.py's corpus, not to this one's.

    python Tools/Scripts/check_rtti_coverage.py          # the gate
    python Tools/Scripts/check_rtti_coverage.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools' / 'Scripts'))

from gen_ae_to_se_map import parse_bin, parse_overrides  # noqa: E402

RTTI_SOURCE = ROOT / 'Code' / 'client' / 'Games' / 'Skyrim' / 'RTTI.cpp'
CODE = ROOT / 'Code'
LIB_DIR = ROOT / 'GameFiles' / 'Skyrim' / 'SKSE' / 'Plugins'
SOURCE_SUFFIXES = ('.cpp', '.h', '.hpp')

# The tables a runtime can resolve an id through: the AE libraries the client loads
# by name (VersionDb.h:210) and the ae-to-se maps it falls back to (VersionDb.h:404).
# The second pattern covers the per-version maps and the generic fallback alike.
AE_LIBRARY = 'versionlib-*.bin'
LEGACY_MAP = 'versionlib-ae-to-se*.map'

# Registration lines are one type per two lines: `template struct
# internal::RttiLocator<T>;` then `internal::RttiLocator<T> registerRtti_T(<id>);`.
# Requiring the id makes this miss the template declarations in RTTI.h and the
# RttiLocator<T>::RttiLocator(uint32_t) / ::Get() definitions in RTTI.cpp.
REGISTRATION = re.compile(r'RttiLocator<\s*([\w:]+)\s*>\s*\w+\s*\(\s*(\d+)\s*\)')
CAST_SITE = re.compile(r'\bCast\s*<\s*([^<>]*?)\s*>\s*\(')
# Used only to notice that the argument pattern above stopped matching the tree.
CAST_MENTION = re.compile(r'\bCast\s*<')

# Code/client/Utils.h:54 is `return Cast<T>(pForm);` inside
# `template <class T> T* GetByServerId(...)`: the argument is the template's own
# parameter, so the id it resolves to is chosen at the call site and no id can be
# named at the definition. A single capital letter is the shape of a template
# parameter and not of a Skyrim type name, so those sites are skipped and counted.
TEMPLATE_PARAMETER = re.compile(r'^[A-Z]$')

# Gaps between the RTTI table and the shipped libraries, judged and held rather than
# fixed. Each entry is id -> reason. The rule for adding one: find the type the id
# belongs to, confirm no Cast<> reaches it (rule 3 fails the gate the moment one
# does), and write down why that id being null on those runtimes is acceptable. An
# entry that stops being a gap fails too, so a declaration cannot outlive its reason.
GAPS: dict[int, str] = {
    392519: (
        'NiObjectNET; absent from the 1.6.1130, 1.6.1170, 1.6.1179, 1.7.99 and 1.7.104 '
        'generations. The NetImmerse headers use the type as a base class only, so nothing casts '
        'to it and no Cast<> resolves this id today; the entry is here so that the first '
        'Cast<NiObjectNET> fails this gate instead of quietly answering nullptr there.'
    ),
    394235: (
        'CreationClubMenu; absent from the same 1.6.1130/1170/1179 and 1.7.99/104 generations as '
        'NiObjectNET, and the type appears nowhere outside the RTTI table. No call site resolves '
        'the id, so its null locator has nothing to degrade.'
    ),
    398437: (
        'NiParticleSystem; absent from the same 1.6.1130/1170/1179 and 1.7.99/104 generations, and '
        'the type appears nowhere outside the RTTI table. Registered for the table and never cast '
        'to, so the missing id costs nothing until a call site appears - which rule 3 watches for.'
    ),
    400159: (
        'CCallbackBase; absent from 1.6.1170-0-1, 1.6.1179 and 1.6.659, and the type appears nowhere '
        'outside the RTTI table. Nothing casts to it, so the null locator has no call site; the '
        'entry keeps that true.'
    ),
    400180: (
        'BSWin32GameDataSystemUtility; absent from all ten versionlib-ae-to-se-*.map tables, so the '
        'pre-AE runtimes cannot resolve it, and the type appears nowhere outside the RTTI table. The '
        'legacy-map side of this same id is declared and held separately by '
        'Tools/missing_ids_by_runtime.tsv through Tools/Scripts/check_map_coverage.py; this entry is '
        'the RTTI table\'s side of it, and the two do not disagree.'
    ),
}


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding='utf-8', errors='ignore')


def blank_comments(text: str) -> str:
    """Replace // and /* */ comments with spaces, keeping every offset and newline.

    Code/client/Games/Skyrim/Actor.cpp:1157 keeps a retired `Cast<TESNPC>` inside a
    block comment; the repo's other gates drop comments for the same reason
    (check_facegen_scope.py:62), so a site that no longer runs cannot fire a rule.
    Blanking rather than deleting is what keeps the byte offsets, and so the source
    line a failure has to name.
    """
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        if text.startswith('//', i):
            end = text.find('\n', i)
            end = n if end < 0 else end
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            end = n if end < 0 else end + 2
        else:
            i += 1
            continue

        for k in range(i, end):
            if text[k] != '\n':
                out[k] = ' '
        i = end

    return ''.join(out)


def parse_registrations() -> dict[int, str]:
    """id -> type name, from the RttiLocator table in RTTI.cpp."""
    return {int(ae_id): name for name, ae_id in REGISTRATION.findall(read(RTTI_SOURCE))}


def source_files() -> list[pathlib.Path]:
    return sorted(path for suffix in SOURCE_SUFFIXES for path in CODE.rglob('*' + suffix))


def scan_cast_sites() -> tuple[list[tuple[str, int, str]], int]:
    """Every `Cast<X>(...)` site under Code/, as (relative path, line, argument).

    The second value is how many `Cast<` mentions the tree holds at all, so the
    caller can tell an empty scan from a pattern that stopped matching.
    """
    sites: list[tuple[str, int, str]] = []
    mentions = 0
    for path in source_files():
        text = blank_comments(read(path))
        mentions += len(CAST_MENTION.findall(text))
        for match in CAST_SITE.finditer(text):
            line = text.count('\n', 0, match.start()) + 1
            sites.append((path.relative_to(ROOT).as_posix(), line, match.group(1).strip()))
    return sites, mentions


def library_paths() -> list[pathlib.Path]:
    return sorted(LIB_DIR.glob(AE_LIBRARY)) + sorted(LIB_DIR.glob(LEGACY_MAP))


def shipped_libraries() -> list[tuple[str, dict[int, int]]]:
    """(file name, {id: offset}) for every shipped table a runtime can resolve through."""
    return [(path.name, parse_bin(path) if path.suffix == '.bin' else parse_overrides(path))
            for path in library_paths()]


def brief(names: list[str], limit: int = 4) -> str:
    """Name a few of the offenders, so a failure stays readable when it lists hundreds."""
    if len(names) <= limit:
        return ', '.join(names)
    return f'{", ".join(names[:limit])} and {len(names) - limit} more'


def check(failures: list[str], warnings: list[str]) -> int:
    by_id = parse_registrations()
    by_name = {name: ae_id for ae_id, name in by_id.items()}
    sites, mentions = scan_cast_sites()
    libraries = shipped_libraries()

    # check_gate_integrity.py R2 exists because a gate that compares nothing and
    # prints OK is worse than no gate at all; these guards are that rule applied here.
    if not by_id:
        failures.append(
            f'{RTTI_SOURCE.relative_to(ROOT).as_posix()} declares no RttiLocator id, so every library '
            f'would agree with an empty table; the registration shape changed or the file moved')
    if not sites:
        if mentions:
            failures.append(
                f'{mentions} `Cast<` mention(s) under {CODE.relative_to(ROOT).as_posix()} parsed into no '
                f'argument at all; the argument pattern stopped matching the tree, so the call-site rules '
                f'are covering nothing')
        else:
            failures.append(
                f'no `Cast<` mention under {CODE.relative_to(ROOT).as_posix()} at all; the client resolves '
                f'every engine pointer through Cast, so an empty scan means the sources moved, not that '
                f'nothing casts')
    elif mentions != len(sites):
        failures.append(
            f'{mentions} `Cast<` mention(s) under {CODE.relative_to(ROOT).as_posix()} parsed into '
            f'{len(sites)} argument(s); the pattern reads one template argument, so a cast it cannot read '
            f'is a cast whose target id nothing checks')
    if not libraries:
        failures.append(
            f'no {AE_LIBRARY} or {LEGACY_MAP} under {LIB_DIR.relative_to(ROOT).as_posix()}; there is '
            f'nothing left to check, which is not the same as every id resolving')
    if failures:
        return 0

    # Rule 2's corpus: every registered id against every shipped table. Membership is
    # the whole question - a locator either finds its id or stays null.
    compared = 0
    gaps: dict[int, list[str]] = {}
    for ae_id in sorted(by_id):
        compared += len(libraries)
        missing = [name for name, table in libraries if ae_id not in table]
        if missing:
            gaps[ae_id] = missing

    undeclared = {ae_id: missing for ae_id, missing in gaps.items() if ae_id not in GAPS}
    if undeclared:
        affected = sorted({name for missing in undeclared.values() for name in missing})
        failures.append(
            f'{len(undeclared)} id(s) registered in {RTTI_SOURCE.relative_to(ROOT).as_posix()} are not '
            f'carried by every shipped library and GAPS does not declare them: '
            f'{brief([f"{ae_id} {by_id[ae_id]}" for ae_id in sorted(undeclared)])}; affected: '
            f'{brief(affected)}; on those runtimes the locator stays null, so a Cast<> to the type '
            f'answers nullptr without crashing or logging - close the gap upstream or write it into GAPS '
            f'with the reason it is safe')

    for ae_id in sorted(GAPS):
        if ae_id in gaps:
            continue
        name = by_id.get(ae_id)
        if name is None:
            failures.append(
                f'GAPS declares {ae_id} as a gap, but {RTTI_SOURCE.relative_to(ROOT).as_posix()} does not '
                f'register that id any more; the declaration outlived the binding it describes')
        else:
            failures.append(
                f'GAPS declares {ae_id} ({name}) as a gap, but all {len(libraries)} shipped libraries '
                f'carry it; the entry is stale, and a stale entry keeps a reader believing a runtime is '
                f'short of an id it now has')

    # Rules 1 and 3 over the call sites. A registered argument whose id is declared a
    # gap is rule 3's business; one whose id is not declared is rule 1's.
    unresolved: dict[str, list[str]] = {}
    reachable: dict[int, list[str]] = {}
    template_parameters = 0
    for relative, line, argument in sites:
        ae_id = by_name.get(argument)
        if ae_id is None:
            if TEMPLATE_PARAMETER.match(argument):
                template_parameters += 1
                continue
            failures.append(
                f'{relative}:{line}: Cast<{argument}> names a type that '
                f'{RTTI_SOURCE.relative_to(ROOT).as_posix()} does not register, so RttiLocator<'
                f'{argument}>::Get() is the null stub and this cast returns nullptr on every runtime')
        elif ae_id in GAPS:
            reachable.setdefault(ae_id, []).append(f'{relative}:{line}')
        elif gaps.get(ae_id):
            unresolved.setdefault(argument, []).append(f'{relative}:{line}')

    for argument, where in sorted(unresolved.items()):
        ae_id = by_name[argument]
        failures.append(
            f'Cast<{argument}> resolves id {ae_id}, which {brief(gaps[ae_id])} do not carry: '
            f'{brief(where)}; on those runtimes the locator stays null and the cast returns nullptr '
            f'instead of a {argument}, so the call site degrades silently')

    for ae_id, where in sorted(reachable.items()):
        name = by_id[ae_id]
        failures.append(
            f'{brief(where)}: Cast<{name}> resolves id {ae_id}, which {brief(gaps[ae_id])} do not carry; '
            f'RttiLocator<{name}>::Get() is null there, so Cast() returns nullptr before it ever reaches '
            f'the game - not a crash, a silent no-op, and the id is declared in GAPS precisely because '
            f'nothing was supposed to reach it')

    if template_parameters:
        warnings.append(
            f'{template_parameters} Cast<> argument(s) are template parameters rather than type names '
            f'(Code/client/Utils.h:54 is Cast<T> inside template <class T> GetByServerId); the id they '
            f'resolve to is chosen at the call site and cannot be checked here')

    if compared == 0:
        failures.append(
            'no registration was compared against any library, so the gap rule covered nothing')

    return compared


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        by_id = parse_registrations()
        libraries = shipped_libraries()
        print('checked:')
        print(f'  every RttiLocator id registered in {RTTI_SOURCE.relative_to(ROOT).as_posix()} resolves '
              f'on every shipped AE library and every ae-to-se map')
        print('  every gap between that table and those libraries is declared in GAPS, and every GAPS '
              'entry is still a gap')
        print('  no declared gap is reachable from a Cast<> argument anywhere under Code/')
        print('  registrations, cast sites and libraries are read from the tree, and an empty corpus fails')
        print(f'  registrations: {len(by_id)}  libraries: {len(libraries)}  declared gap(s): {len(GAPS)}')
        for ae_id in sorted(GAPS):
            missing = [name for name, table in libraries if ae_id not in table]
            print(f'  {ae_id}  {by_id.get(ae_id, "<not registered>"):<32} '
                  f'absent from {len(missing)} of {len(libraries)}  {GAPS[ae_id]}')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    compared = check(failures, warnings)

    for warning in warnings:
        print(f'  note: {warning}')

    if failures:
        print(f'RTTI COVERAGE CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    sites, _mentions = scan_cast_sites()
    arguments = sorted({argument for _path, _line, argument in sites})
    print('RTTI COVERAGE CHECK OK')
    print(f'  {len(parse_registrations())} registration(s) x {len(library_paths())} shipped library(ies) '
          f'= {compared} id/runtime pair(s); {len(arguments)} distinct Cast<> argument(s) over '
          f'{len(sites)} site(s) resolve everywhere; {len(GAPS)} declared gap(s) are real and reachable '
          f'from no cast')
    return 0


if __name__ == '__main__':
    sys.exit(main())
