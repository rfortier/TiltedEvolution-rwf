#!/usr/bin/env python3
"""Keep the layout assertions self-consistent within each build branch.

The game structs in Code/client/Games mirror the engine's memory, and their layouts
differ between the pre-AE runtime and the AE one. That is why the assertions come in
pairs:

    #ifdef SKYRIM_TARGET_LEGACY
    static_assert(offsetof(ExtraDataList, data) == 0x0);
    static_assert(offsetof(ExtraDataList, bitfield) == 0x8);
    #else
    static_assert(offsetof(ExtraDataList, data) == 0x8);
    static_assert(offsetof(ExtraDataList, bitfield) == 0x10);
    #endif

Within one branch an offset identifies exactly one field, and a field has exactly
one offset. Across branches both may differ - that is the design, not a conflict.

Two ways to get this wrong, both silent until a runtime hits them:

  * the same field asserted twice at two offsets inside one branch - one of the two
    is stale, and which one depends on which branch it sits in;
  * two different fields asserted at the same offset inside one branch - the struct
    cannot have both there, so at least one assert is wrong and the layout it
    describes was never true.

A scan that ignores the branch structure reports the intended legacy/AE pairs as
conflicts (measured: 14 in Actor.h alone), which is why this gate tracks #if depth
rather than flattening the file.

What this gate deliberately does NOT do: check that every field of a struct has an
assertion. That is not decidable by text here - these headers hold 153 nested
struct/class/enum declarations and 22 preprocessor branches, so a member extractor
counts nested type names and #ifdef lines as members (Actor came out with 114
"members" against a real count far lower). See docs/PITFALLS.md section 147.

    python Tools/Scripts/check_layout_asserts.py          # the gate
    python Tools/Scripts/check_layout_asserts.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GAMES = ROOT / 'Code' / 'client' / 'Games'

ASSERT = re.compile(
    r'static_assert\s*\(\s*offsetof\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*==\s*(0x[0-9A-Fa-f]+|\d+)'
)

# A scan that finds nothing is not a passing scan.
MIN_ASSERTS = 100


def branch_id(stack: list[str]) -> str:
    return '|'.join(stack) if stack else 'unconditional'


def check(failures: list[str], notes: list[str]) -> int:
    if not GAMES.is_dir():
        failures.append(f'{GAMES.relative_to(ROOT).as_posix()} is missing; the game structs cannot be '
                        f'checked')
        return 0

    total = 0
    files = 0
    branches = 0
    for path in sorted(GAMES.rglob('*')):
        if path.suffix not in ('.h', '.hpp'):
            continue
        rel = path.relative_to(ROOT).as_posix()
        stack: list[str] = []
        per_branch: dict[str, list[tuple[str, str, int, int]]] = {}
        for i, line in enumerate(path.read_text(encoding='utf-8', errors='ignore').splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith(('#ifdef', '#ifndef', '#if')):
                parts = stripped.split()
                stack.append(f'{parts[0]}:{parts[1] if len(parts) > 1 else "?"}')
            elif stripped.startswith('#else'):
                if stack:
                    stack[-1] = stack[-1].split('|')[0] + '|else'
            elif stripped.startswith('#endif'):
                if stack:
                    stack.pop()
            for struct, field, value in ASSERT.findall(line):
                per_branch.setdefault(branch_id(stack), []).append((struct, field, int(value, 0), i))

        if per_branch:
            files += 1
            branches += len(per_branch)
        for branch, entries in sorted(per_branch.items()):
            total += len(entries)
            seen: dict[tuple[str, str], int] = {}
            by_offset: dict[tuple[str, int], str] = {}
            for struct, field, value, line in entries:
                key = (struct, field)
                if key in seen and seen[key] != value:
                    failures.append(
                        f'{rel}:{line}: {struct}.{field} is asserted at both {seen[key]:#x} and '
                        f'{value:#x} in the same branch ({branch}); one of the two is stale')
                seen[key] = value
                okey = (struct, value)
                if okey in by_offset and by_offset[okey] != field:
                    failures.append(
                        f'{rel}:{line}: {struct}: {by_offset[okey]} and {field} are both asserted at '
                        f'{value:#x} in the same branch ({branch}); the struct cannot have both there')
                by_offset[okey] = field

    if total < MIN_ASSERTS:
        failures.append(
            f'only {total} offsetof assertion(s) were found, below the floor of {MIN_ASSERTS}; the '
            f'assertion shape changed and this gate is no longer looking at the layouts it claims to')

    notes.append(f'{total} assertion(s) in {files} file(s) across {branches} branch(es)')
    return total


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  within one preprocessor branch, an offset identifies exactly one field')
        print('  and a field has exactly one offset')
        print('  (across branches they legitimately differ: SKYRIM_TARGET_LEGACY)')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'LAYOUT ASSERT CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('LAYOUT ASSERT CHECK OK')
    print('  every asserted offset is unique per struct per build branch')
    return 0


if __name__ == '__main__':
    sys.exit(main())
