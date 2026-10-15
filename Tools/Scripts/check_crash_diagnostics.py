#!/usr/bin/env python3
"""Keep the crash diagnostics the documentation promises actually in the build.

The defect this exists for was a doc/code disagreement that neither side could
see on its own:

  * `docs/PITFALLS.md` tells a player reporting a crash to collect
    `crash_*.dmp` from the game root;
  * `CrashHandler.cpp` wrote that dump inside `#if (IS_MASTER) false #else true`,
    a flag inherited from upstream where it means "is this the release branch".

Here the flag does not mean that. `IS_MASTER` comes from the git branch name
(xmake.lua's `before_build`) and this repository's release branch IS `main`, so
the macro is 1 in every build this project ships. The dump was therefore dead
code in exactly the builds whose crashes need reporting, and no gate noticed
because each side was internally consistent.

Two things are checked, both against the engine-independent facts:

1. **The dump is not behind a configuration macro.** A minidump is the only
   artifact that can explain a crash after the fact; gating it on a build flag
   means the builds users run cannot produce one.
2. **The cleanup matches only our own dumps.** The predicate was a substring
   test for "crash" over the whole path, which also matches a player's
   `CrashLogger.dll` or a `Crash Fixes` folder - and then deletes it.
3. **The docs and the code agree.** If the docs name an artifact, the code that
   writes it must not be conditionally compiled out.

    python Tools/Scripts/check_crash_diagnostics.py          # the gate
    python Tools/Scripts/check_crash_diagnostics.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HANDLER = ROOT / 'Code' / 'client' / 'CrashHandler.cpp'
PITFALLS = ROOT / 'docs' / 'PITFALLS.md'
XMAKE = ROOT / 'xmake.lua'

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')

# The name the handler writes, as a literal, so the check does not depend on
# reading the whole format string.
DUMP_PREFIX = 'crash_'
DUMP_SUFFIX = '.dmp'


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def branch_names() -> set[str]:
    """The git branches xmake treats as IS_MASTER, straight from xmake.lua."""
    text = XMAKE.read_text(encoding='utf-8')
    match = re.search(r'bool_to_number\[(?P<expr>[^\]]*branch\s*==\s*"[^"]*"[^\]]*)\]', text)
    if not match:
        return set()
    return set(re.findall(r'branch\s*==\s*"([^"]+)"', match.group('expr')))


def check(failures: list[str]) -> None:
    """The documented crash artifacts are produced by every shipped build."""
    if not HANDLER.is_file():
        failures.append(f'{HANDLER.relative_to(ROOT)}: missing, so nothing writes the documented dump')
        return

    code = strip_comments(HANDLER.read_text(encoding='utf-8'))

    # 1. The dump write must not be inside a configuration conditional.
    #    Look at the guard around the assignment that enables it.
    guard = re.search(
        r'(?P<before>(?:#if[^\n]*\n(?:\s*[^\n#][^\n]*\n)*)?)\s*'
        r'(?:volatile\s+)?static\s+bool\s+bMiniDump\s*=\s*(?P<value>true|false)',
        code)
    if not guard:
        failures.append(
            f'{HANDLER.relative_to(ROOT)}: no `bMiniDump` initialiser found; if the dump '
            f'was removed, drop the reference to {DUMP_PREFIX}*{DUMP_SUFFIX} from the docs')
    else:
        if '#if' in guard.group('before'):
            failures.append(
                f'{HANDLER.relative_to(ROOT)}: the minidump is enabled behind a preprocessor '
                f'conditional; it must be produced by every build, including the one users run')
        if guard.group('value') != 'true':
            failures.append(
                f'{HANDLER.relative_to(ROOT)}: bMiniDump initialises to '
                f'`{guard.group("value")}`, so this build writes no minidump - but the docs '
                f'promise a {DUMP_PREFIX}*{DUMP_SUFFIX}')

    # 2. The cleanup predicate must be specific to our own dump naming.
    cleanup = re.search(r'RemovePreviousDump\([^)]*\)\s*(?:noexcept\s*)?\{', code)
    if not cleanup:
        failures.append(
            f'{HANDLER.relative_to(ROOT)}: no RemovePreviousDump definition found; if the cleanup '
            f'was renamed or removed, this gate stops checking that it matches only '
            f'{DUMP_PREFIX}*{DUMP_SUFFIX} - point this rule at the new name instead of leaving it '
            f'to cover nothing')
    else:
        body = code[cleanup.end():]
        # Take the function body up to its closing brace at column 0.
        end = body.find('\n}')
        body = body[:end if end != -1 else len(body)]
        if re.search(r'\.find\(\s*"crash"\s*\)', body) or re.search(r'find\(\s*"crash"\s*\)', body):
            failures.append(
                f'{HANDLER.relative_to(ROOT)}: RemovePreviousDump matches the substring "crash" '
                f'over the whole path, so it also deletes unrelated files such as a '
                f'CrashLogger.dll or a "Crash Fixes" folder; match the {DUMP_PREFIX}*{DUMP_SUFFIX} '
                f'shape instead')
        if DUMP_PREFIX not in body:
            failures.append(
                f'{HANDLER.relative_to(ROOT)}: RemovePreviousDump no longer matches the '
                f'{DUMP_PREFIX!r} prefix it is supposed to clean up')

    # 3. Docs and code must agree about the artifact existing.
    if PITFALLS.is_file():
        docs = PITFALLS.read_text(encoding='utf-8')
        if re.search(re.escape(DUMP_PREFIX) + r'[^`\s]*' + re.escape(DUMP_SUFFIX), docs):
            if DUMP_PREFIX not in code:
                failures.append(
                    f'{PITFALLS.relative_to(ROOT)}: tells the reader to collect '
                    f'{DUMP_PREFIX}*{DUMP_SUFFIX}, but {HANDLER.relative_to(ROOT)} never '
                    f'writes one')

    # 4. The branch fact the reasoning rests on must still hold: if this repo
    #    stops treating `main` as IS_MASTER, the comment above bMiniDump is
    #    wrong and so is anyone reading it.
    branches = branch_names()
    if branches and 'main' not in branches:
        failures.append(
            f'{XMAKE.relative_to(ROOT)}: IS_MASTER no longer covers `main` '
            f'(it covers {sorted(branches)}); the note above bMiniDump in '
            f'{HANDLER.relative_to(ROOT)} says the shipped build has IS_MASTER=1 and needs '
            f're-checking')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print(f'  the minidump write in {HANDLER.relative_to(ROOT)} is not behind a build macro')
        print(f'  RemovePreviousDump matches only {DUMP_PREFIX}*{DUMP_SUFFIX}')
        print(f'  {PITFALLS.relative_to(ROOT)} and the handler agree the artifact exists')
        print(f'  {XMAKE.relative_to(ROOT)} still treats `main` as IS_MASTER')
        return 0

    failures: list[str] = []
    check(failures)

    if failures:
        print(f'CRASH DIAGNOSTICS CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('CRASH DIAGNOSTICS CHECK OK')
    print(f'  minidump produced unconditionally; cleanup specific to {DUMP_PREFIX}*{DUMP_SUFFIX}')
    print(f'  IS_MASTER covers {sorted(branch_names())}, matching the note in the handler')
    return 0


if __name__ == '__main__':
    sys.exit(main())
