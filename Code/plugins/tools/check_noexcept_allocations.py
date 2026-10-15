#!/usr/bin/env python3
"""Keep every `noexcept` frame in the plugin layer from terminating the game.

An allocation failure escaping a `noexcept` frame calls `std::terminate`, which
ends the game with no crash dump and nothing in any log. The repository has now
fixed that shape five times: four censuses over `Code/client`, `Code/server`,
`Code/encoding` and `plugins/STRPluginMessagingAPI` (patches 0004..0007), and then
the companion plugins' own load paths, which the fourth round had left alone
because it only fixed the copy it found.

Each round's coverage was defined by where the defect was found, and each round
therefore missed the next copy. That is the error this file avoids: the rule is
"**every** `noexcept` function in the plugin tree whose body really allocates must
contain its own failure", and the corpus is the tree, not a list of places a
defect has already been seen.

    Rule 1  every noexcept function under plugins/ whose body contains an
            allocating operation contains a catch - unless it is listed in
            JUDGED below with the reason it cannot throw.
    Rule 2  every definition of LoadPluginModule contains a catch. This is
            stricter than rule 1 on purpose: that helper is the one the last
            round had to fix in four separate repositories, so it is named.
    Rule 3  a plugin that names LoadPluginModule but defines it nowhere fails.
            Otherwise renaming the helper removes that plugin from rule 2
            silently - which is how one of the four copies stayed unguarded.
    Rule 4  the sites this file exists for must actually be seen (a floor on the
            corpus). "Nothing unguarded" and "could not see anything" are the
            same output, so the count is asserted rather than reported.

    python Code/plugins/tools/check_noexcept_allocations.py          # the gate
    python Code/plugins/tools/check_noexcept_allocations.py --list   # what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]

# The helper the previous round had to fix four times.
HELPER = 'LoadPluginModule'

# Directories that are not plugin source.
SKIP_DIRS = {'snapshots', '.git', 'build', 'out', 'node_modules', 'vcpkg', 'vcpkg_installed'}

# A definition whose signature carries noexcept.
SIGNATURE = re.compile(
    r'^\s*(?:[\w:<>,*&\s]+?)\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?noexcept\b', re.M)

BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
LINE_COMMENT = re.compile(r'//[^\n]*')

# Real allocating operations, not substrings of type names. A default-constructed
# container is deliberately NOT matched - it does not allocate - which is what
# keeps this rule from reporting declarations as defects.
#
# The last two alternatives cover the two spellings that a first, narrower
# version of this pattern missed, and that rule 4 then caught:
#   `std::vector<Trait::ActorCondition> conditions = ...;`   (template argument)
#   `sceneID = std::string(sceneID)`                         (construct in a capture)
ALLOCATING = re.compile(
    r'\b(?:push_back|emplace_back|emplace|insert|resize|reserve|assign|'
    r'make_shared|make_unique|MakeUnique|MakeShared)\s*[\(\{<]'
    r'|std::to_string\s*\('
    r'|std::make_(?:shared|unique)\s*<'
    # A named container built from a template argument: `std::vector<...> name =`
    r'|\bstd::(?:wstring|string|vector|unordered_map|unordered_set|map|set)\s*<[^;{}]*>\s+\w+\s*[=(]'
    # A container constructed from an argument: `std::string(x)` / `std::wstring(x)`
    r'|\bstd::(?:wstring|string|vector|unordered_map|unordered_set)\s*\('
    # A bare named container of one of those types: `std::wstring name =`
    r'|\bstd::(?:wstring|string)\s+\w+\s*[=(]')

# Functions that match a marker above but cannot actually throw out of the frame,
# each with the reason. Empty today: the pattern was narrowed so that a
# default-constructed container - the one shape that looks like an allocation and
# is not - no longer matches at all. The table stays because the alternative to an
# exemption is a narrower regex, and a narrower regex is how a real site goes
# unlooked-at; an exemption is visible, and an unused one is removed rather than
# left in place.
JUDGED: dict[tuple[str, str], str] = {}

# The corpus floor. Currently 32 noexcept functions in the plugin tree have an
# allocating body; a collapse well below that means the scan lost its corpus
# rather than that the tree became safe.
MINIMUM_SITES = 25

# The sites this file exists for: the 4 load-path copies and the 8 OStimNG Thread
# API entry points. Rule 4 requires every one of them to be visible, so the gate
# cannot pass while being blind to what it was written for.
EXPECTED = [
    ('plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPI.cpp', 'LoadPluginModule'),
    ('plugins/AnimSyncTogether/src/STRPMApi.cpp', 'LoadPluginModule'),
    ('plugins/DAVSyncTogether/src/STRPMApi.cpp', 'LoadPluginModule'),
    ('plugins/TradeTogether/src/STRPluginMessagingAPI.cpp', 'LoadPluginModule'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'GetNavigationCount'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'GetNavigationOptions'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'NavigateToScene'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'RegisterEventCallback'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'RegisterControlCallback'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'SearchScenes'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'HasCompatibleNode'),
    ('plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp', 'MigrateThread'),
]


def strip_comments(text: str) -> str:
    return LINE_COMMENT.sub('', BLOCK_COMMENT.sub('', text))


def body_of(text: str, name: str) -> str | None:
    """The body of the first definition of `name(...)`, brace-matched."""
    for match in re.finditer(r'\b' + re.escape(name) + r'\s*\(', text):
        brace = text.find('{', match.end())
        if brace < 0:
            continue
        if ';' in text[match.end():brace]:
            continue                       # a declaration, not a definition
        depth = 0
        for i in range(brace, len(text)):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    return text[brace + 1:i]
    return None


def plugin_sources():
    """Every plugin source file, as (relative path, text)."""
    for path in sorted((ROOT / 'plugins').rglob('*')):
        if path.suffix not in ('.cpp', '.h', '.hpp'):
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        rel = path.relative_to(ROOT).as_posix()
        yield rel, path.read_text(encoding='utf-8', errors='replace')


def allocating_noexcept_sites() -> list[tuple[str, str, str, bool]]:
    """(rel, function, body, has_catch) for every noexcept frame that allocates."""
    out = []
    for rel, raw in plugin_sources():
        code = strip_comments(raw)
        for match in SIGNATURE.finditer(code):
            body = body_of(code, match.group(1))
            if body is None:
                continue
            if not ALLOCATING.search(body):
                continue
            # A named definition only: skip a match whose body we already took.
            out.append((rel, match.group(1), body, 'catch' in body))
    return out


def helper_copies() -> tuple[list[tuple[str, str]], list[str]]:
    """(definitions of the helper, files naming it with no definition found)."""
    out: list[tuple[str, str]] = []
    unaccounted: list[str] = []
    for rel, raw in plugin_sources():
        if HELPER not in raw:
            continue
        body = body_of(strip_comments(raw), HELPER)
        if body is None:
            unaccounted.append(rel)
        else:
            out.append((rel, body))
    return out, unaccounted


def check_whole_tree(failures: list[str]) -> int:
    """Rule 1: every allocating noexcept frame contains its own failure."""
    sites = allocating_noexcept_sites()
    if len(sites) < MINIMUM_SITES:
        failures.append(
            f'only {len(sites)} noexcept function(s) with an allocating body were found under '
            f'plugins/, below the floor of {MINIMUM_SITES}. The scan lost its corpus rather '
            f'than the tree becoming safe, and an empty scan reports the same OK as a clean one')
        return 0

    checked = 0
    for rel, name, body, has_catch in sites:
        if (rel, name) in JUDGED:
            continue
        checked += 1
        if not has_catch:
            failures.append(
                f'{rel}: {name}() is noexcept and its body allocates with no catch. An '
                f'allocation failure escaping a noexcept frame calls std::terminate, which '
                f'ends the game with no crash dump and nothing in any log; contain it and '
                f'return the empty answer, or add it to JUDGED with the reason it cannot throw')
    return checked


def check_helpers(failures: list[str]) -> int:
    """Rule 2 and rule 3: the helper the last round fixed four times."""
    copies, unaccounted = helper_copies()
    if not copies:
        failures.append(
            f'no definition of {HELPER} was found under plugins/; every companion plugin is '
            f'supposed to carry one, so an empty scan means the tree moved, not that the '
            f'guards hold')
        return 0

    for rel in unaccounted:
        failures.append(
            f'{rel} names {HELPER} but defines it nowhere, so this gate is not reading the code '
            f'that runs there. If the helper moved or was renamed, follow it - otherwise that '
            f'plugin is checked by nothing')

    checked = 0
    for rel, body in copies:
        if not ALLOCATING.search(body):
            failures.append(
                f'{rel}: {HELPER}() no longer allocates, so this gate is covering nothing there; '
                f'if the helper was rewritten without the wstring, drop it from this check rather '
                f'than leaving it to pass vacuously')
            continue
        checked += 1
        if 'catch' not in body:
            failures.append(
                f'{rel}: {HELPER}() is noexcept and allocates (a std::wstring is built from the '
                f'module name) with no catch. An allocation failure escaping a noexcept frame '
                f'calls std::terminate, which ends the game with no crash dump - wrap the '
                f'allocating steps the way the other copies do, and fall through to "not loaded"')
    return checked


def check_expected_visible(failures: list[str]) -> int:
    """Rule 4: the gate must be able to see the sites it exists for."""
    sites = {(rel, name) for rel, name, _b, _c in allocating_noexcept_sites()}
    copies, _unaccounted = helper_copies()
    for rel, body in copies:
        if ALLOCATING.search(body):
            sites.add((rel, HELPER))

    missing = [(rel, name) for rel, name in EXPECTED if (rel, name) not in sites]
    if missing:
        failures.append(
            f'the gate cannot see {len(missing)} of the {len(EXPECTED)} site(s) it was written '
            f'for: ' + ', '.join(f'{rel}:{name}' for rel, name in missing) +
            '. "nothing unguarded" and "could not see anything" are the same output, so the '
            f'corpus is asserted rather than reported')
    return len(EXPECTED) - len(missing)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        sites = allocating_noexcept_sites()
        copies, unaccounted = helper_copies()
        print('checked:')
        print('  every noexcept function under plugins/ with an allocating body contains a catch')
        print(f'  every definition of {HELPER} does the same (stricter, by name)')
        print(f'  every plugin naming {HELPER} defines it here')
        print(f'  all {len(EXPECTED)} site(s) this gate exists for are visible to it')
        print(f'  {len(sites)} noexcept function(s) with an allocating body, floor {MINIMUM_SITES}:')
        for rel, name, _body, has_catch in sorted(sites):
            note = ''
            if (rel, name) in JUDGED:
                note = '  [judged: cannot throw]'
            elif not has_catch:
                note = '  [UNGUARDED]'
            print(f'    {rel:<72} {name}{note}')
        if unaccounted:
            print('  files naming the helper with no definition found:')
            for rel in unaccounted:
                print(f'    {rel}')
        return 0

    failures: list[str] = []
    whole = check_whole_tree(failures)
    helpers = check_helpers(failures)
    visible = check_expected_visible(failures)

    if failures:
        print(f'NOEXCEPT ALLOCATION CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('NOEXCEPT ALLOCATION CHECK OK')
    print(f'  {whole} allocating noexcept frame(s) across the plugin tree contain their own '
          f'failure ({helpers} of them the load-path helper), and all {visible} site(s) this '
          f'gate exists for are visible to it')
    return 0


if __name__ == '__main__':
    sys.exit(main())
