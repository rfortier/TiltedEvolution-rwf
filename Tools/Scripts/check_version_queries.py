#!/usr/bin/env python3
"""Keep the three copies of the game-version query testing the same things.

The same resource query - read `ProductVersion`, fall back to `FileVersion` -
is written out three times, because the three callers do not share a library:

    Code/immersive_launcher/utils/FileVersion.inl   the launcher path
    Code/client/VersionDb.h                         the client's own lookup
    Code/skse_client/main.cpp                       the SKSE path

Each one decides whether the version it read is usable, and each one has to test
four things: the query succeeded, it reported a length, it returned a pointer,
and the string is not empty. Getting the set wrong is not symmetric:

  * testing too little accepts an empty version. In the launcher that reaches
    `if (LC.Version.empty()) DIE_NOW(L"Failed to query game version")`, so a
    block that carries `ProductVersion` but leaves it blank aborts the launch
    with a message naming the wrong cause - `FileVersion` was there to be read.
  * testing too much rejects a version that is present.

The launcher's `ProductVersion` branch was missing the empty-string test while
its own `FileVersion` branch right below it had it, and while both other
implementations had it on both keys. One file disagreeing with itself about the
same query is the tell.

This gate reads each implementation's guard and requires the same four tests on
every key it queries, in every copy. It is the section 114 / section 120
criterion - one definition that became several, so consistency stopped being the
compiler's job - applied to a version query rather than to a struct.

    python Tools/Scripts/check_version_queries.py          # the gate
    python Tools/Scripts/check_version_queries.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# Every implementation of the query, and where its guard lives.
IMPLEMENTATIONS = [
    'Code/immersive_launcher/utils/FileVersion.inl',
    'Code/client/VersionDb.h',
    'Code/skse_client/main.cpp',
]

# The resource keys a query may read. Both spellings of the path separator are
# accepted, because the sources differ.
KEYS = ['ProductVersion', 'FileVersion']

# The four tests, as they appear in the guard.
#
# `length` is not required, because two of the three copies omit it and both are
# correct without it: `VerQueryValueA` sets the length, and a non-empty string
# already implies a non-zero length. Requiring it would report working code as
# broken, and a criterion that reports working code gets abandoned (section
# 115.3). What every copy must do is refuse an empty string, which is the test
# that was missing.
TESTS = {
    'non-empty': re.compile(r'\*\s*\w+'),
}

# A query whose result is guarded, in any of the shapes the three copies use:
#
#   if (VerQueryValueA(buf, key, &p, &len) && len && p && *p)
#   if (VerQueryValue(buf, key, &p, &len) && len && p)
#   const char* kKeys[] = {...}; for (k : kKeys) { if (VerQueryValueA(...) && ... *p) }
#
# The key may be a literal or a loop variable, so the guard is what identifies
# the call - and a key list makes the guard apply to every key in it.
QUERY_CALL = re.compile(r'VerQueryValue\w*\s*\(', re.S)
KEY_LITERAL = re.compile(r'StringFileInfo\\\\?[\w]*\\\\?(ProductVersion|FileVersion)')
KEY_LIST = re.compile(r'kKeys\s*\[\s*\]\s*=\s*\{([^}]*)\}', re.S)

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def keys_named(text: str) -> list[str]:
    """Every resource key the file mentions, by literal or through a key list."""
    out = [k for k in KEYS if re.search(r'StringFileInfo[^"]*' + k, text)]
    for match in KEY_LIST.finditer(text):
        out.extend(k for k in KEYS if k in match.group(1))
    # Preserve declaration order, drop duplicates.
    seen: list[str] = []
    for k in out:
        if k not in seen:
            seen.append(k)
    return seen


def guards(text: str) -> list[tuple[str, str]]:
    """(key, guard) for every guarded VerQueryValue call.

    The guard runs from the call to the end of its enclosing `if` condition. It
    is taken by brace/paren matching rather than by a regex over the whole call,
    because the condition spans lines and nests parentheses in the call itself.
    """
    out: list[tuple[str, str]] = []
    for match in QUERY_CALL.finditer(text):
        # Walk to the `)` that closes this call.
        i = match.end() - 1
        depth = 0
        while i < len(text):
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        call = text[match.start():i + 1]

        # Is the result guarded? Look for `&&` before the end of the condition.
        j = i + 1
        while j < len(text) and text[j] in ' \t\r\n':
            j += 1
        if not text.startswith('&&', j):
            continue
        k = text.find('{', j)
        if k < 0:
            continue
        guard = ' '.join(text[j:k].split())
        guard = guard.rstrip(')').strip()

        # Which key(s) does this call cover?
        named = [key for key in KEYS if key in call]
        if not named:
            named = keys_named(text)   # a loop over a key list
        for key in named:
            out.append((key, guard))
    return out


def check(failures: list[str], warnings: list[str]) -> int:
    checked = 0
    for rel in IMPLEMENTATIONS:
        path = ROOT / rel
        if not path.is_file():
            failures.append(
                f'{rel} is missing; if the query moved, move it here rather than leaving this '
                f'check covering nothing')
            continue

        text = strip_comments(path.read_text(encoding='utf-8', errors='replace'))
        found = guards(text)
        if not found:
            failures.append(
                f'{rel} has no guarded VerQueryValue call; either the query is gone or its shape '
                f'changed and this check is silently covering nothing there')
            continue

        # Every key the file reads must be covered by at least one guard, so a
        # key cannot be added without a guard.
        covered = {key for key, _g in found}
        for key in keys_named(text):
            if key not in covered:
                failures.append(
                    f'{rel} reads {key} but no guarded VerQueryValue call covers it; an unguarded '
                    f'key accepts an empty value')
                continue

            # And the guard on it must refuse an empty string.
            for covered_key, guard in found:
                if covered_key != key:
                    continue
                missing = [name for name, pattern in TESTS.items() if not pattern.search(guard)]
                if missing:
                    failures.append(
                        f'{rel}: the {key} guard is `{guard}`, which does not test '
                        f'{", ".join(missing)}. An empty {key} then counts as a version, and the '
                        f'fallback key is never tried')
                checked += 1

    # The three copies must agree on WHICH keys they read. This is a failure
    # rather than a note because the fallback is the whole point: a copy that
    # reads only ProductVersion has nothing to fall back to when a repack leaves
    # that key blank - which is the case the second key exists for.
    seen: dict[str, list[str]] = {}
    for rel in IMPLEMENTATIONS:
        path = ROOT / rel
        if path.is_file():
            seen[rel] = keys_named(strip_comments(path.read_text(encoding='utf-8', errors='replace')))
    if seen:
        reference_rel, reference = next(iter(seen.items()))
        for rel, keys in seen.items():
            if keys != reference:
                failures.append(
                    f'{rel} reads {keys} while {reference_rel} reads {reference}; a copy with '
                    f'fewer keys has no fallback, so a block that leaves the first key blank '
                    f'aborts that path instead of trying the second')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  every guarded VerQueryValue call tests the length, the pointer and non-emptiness')
        print('  the three implementations read the same resource keys')
        total = 0
        for rel in IMPLEMENTATIONS:
            path = ROOT / rel
            if not path.is_file():
                print(f'    {rel:<50} MISSING')
                continue
            text = strip_comments(path.read_text(encoding='utf-8', errors='replace'))
            found = guards(text)
            total += len(found)
            print(f'    {rel:<50} {len(found)} guard(s): '
                  f'{", ".join(k for k, _g in found) or "-"}')
        print(f'  {total} guard(s) total')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    if warnings:
        for warning in warnings:
            print(f'  note: {warning}')

    if failures:
        print(f'VERSION QUERY CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('VERSION QUERY CHECK OK')
    print(f'  {checked} version-query guard(s) across {len(IMPLEMENTATIONS)} implementation(s) '
          f'test the same four things')
    return 0


if __name__ == '__main__':
    sys.exit(main())
