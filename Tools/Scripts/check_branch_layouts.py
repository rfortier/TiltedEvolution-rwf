#!/usr/bin/env python3
"""Keep every target-dependent struct layout pinned by an assertion.

The client is built twice from one set of sources - `SkyrimTogetherClient` and
`SkyrimTogetherClientLegacy` (Code/client/xmake.lua) - and the two targets do NOT
agree on where the engine's fields live. Structs that hold engine fields in
hand-written offsets therefore branch on SKYRIM_TARGET_LEGACY, and each branch
carries its own numbers.

PITFALLS.md section 2 calls this the most dangerous area in the repository, and
it has bitten three times, always the same way: a merge adopts upstream's AE
offset into the legacy branch and one branch silently reads the neighbouring
field. The stated defence is a static_assert per branch, so the next drift breaks
the build. That defence was a checklist item and nothing ran it - which is the
shape section 117 and section 127 already paid for once each: a convention that
is written down and not enforced is a comment.

Two rules, both mechanical:

  * RULE 1 - a conditional that changes a struct's members must be pinned. If
    either side of a SKYRIM_TARGET_LEGACY conditional declares members, some
    static_assert in that file must name the type that owns the conditional
    (directly, or as `Outer::Inner`). An assertion after the #endif pins the
    branch that compiled, which is enough; what is not enough is nothing naming
    the type at all, which is how ExtraDataList drifted: its size was only
    implied by TESObjectREFR's sizeof, one structure away from the edit.

  * RULE 2 - an offset constant must be tied to the struct it describes. A
    `k*Offset*` constant is a second copy of a number the struct already
    computes; it must either be initialised from offsetof() or be named by a
    static_assert. SkyrimVM is why: the offset that decides whether the client
    runs at all was written in the pads, in the asserts, in a constant, and twice
    more in the diagnostic that exists to report a wrong offset - and the
    diagnostic reporting a probe it never took is worse than no diagnostic.

    python Tools/Scripts/check_branch_layouts.py          # the gate
    python Tools/Scripts/check_branch_layouts.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'Code' / 'client'

# The macro the two targets differ by. Both variants are compiled by CI, so an
# assertion inside either branch is really checked - see check_library_scan.py,
# which is what keeps the legacy target in the build.
TARGET = 'SKYRIM_TARGET_LEGACY'

IF = re.compile(r'^\s*#\s*(ifdef|ifndef|if)\b(.*)$')
ELSE = re.compile(r'^\s*#\s*else\b')
ENDIF = re.compile(r'^\s*#\s*endif\b')
TYPE_OPEN = re.compile(r'^\s*(?:template\s*<[^>]*>\s*)?(?:struct|class)\s+(\w+)\b')
ASSERT = re.compile(r'static_assert\s*\(')
# A member declaration: a type-ish prefix, a name, an optional array extent, an
# optional initialiser. Deliberately loose - this decides "is there anything
# here to pin", and a false positive only asks the author to name the type.
MEMBER = re.compile(
    r'^\s*(?:mutable\s+|volatile\s+|static\s+|const\s+)*'
    r'(?:unsigned\s+|signed\s+)?'
    r'(?:[\w:]+(?:\s*<[^;{}]*>)?[\s\*&]+)+?'
    r'(\w+)\s*(\[[^\]]*\])?\s*(?:=[^;{}]*)?;')
CONSTANT = re.compile(r'static\s+constexpr\s+[\w:]+\s+(k\w*Offset\w*)\s*=\s*([^;]*);')
LINE_COMMENT = re.compile(r'//[^\n]*')
BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)


def strip(text: str) -> str:
    return LINE_COMMENT.sub('', BLOCK_COMMENT.sub('', text))


def owners(lines: list[str]) -> list[str | None]:
    """Innermost open struct/class name per line, by brace depth."""
    out: list[str | None] = []
    stack: list[tuple[str, int]] = []
    depth = 0
    pending: str | None = None
    for line in lines:
        if pending is None:
            m = TYPE_OPEN.match(line)
            if m:
                pending = m.group(1)
        for ch in line:
            if ch == '{':
                if pending is not None:
                    stack.append((pending, depth))
                    pending = None
                depth += 1
            elif ch == '}':
                depth -= 1
                if stack and stack[-1][1] == depth:
                    stack.pop()
        if ';' in line and '{' not in line:
            pending = None
        out.append(stack[-1][0] if stack else None)
    return out


def conditionals(lines: list[str]):
    """(start_line_index, condition, legacy_side, other_side, end_index)."""
    i = 0
    while i < len(lines):
        m = IF.match(lines[i])
        if not m or TARGET not in m.group(2):
            i += 1
            continue
        depth, a, b, seen_else = 1, [], [], False
        j = i + 1
        while j < len(lines):
            if IF.match(lines[j]):
                depth += 1
            elif ENDIF.match(lines[j]):
                depth -= 1
                if depth == 0:
                    break
            elif ELSE.match(lines[j]) and depth == 1:
                seen_else = True
            elif depth == 1:
                (b if seen_else else a).append(lines[j])
            j += 1
        yield i, f'#{m.group(1)}{m.group(2)}', a, b, j
        i = j + 1


def members(body: list[str]) -> list[str]:
    """Declarations in a branch body that would move if the layout changed.

    Statement lines are skipped by their leading keyword, because a conditional
    that only guards logic (ObjectService.cpp's player-home fallback) is not a
    layout and must not be dragged into this rule.

    A `virtual` declaration counts as layout: whether the type has a vtable is
    the difference ExtraDataList branches on, and it moves every following
    member by 8 bytes. Missing that is how the first version of this gate read
    ExtraDataList's branch as having no members at all and said nothing.
    """
    out = []
    for line in body:
        stripped = line.strip()
        if stripped.startswith(('#', 'static_assert', 'return', 'if', 'else', 'for', 'while',
                                'switch', 'case', 'break', 'continue', 'throw', 'delete', 'goto',
                                'using', 'typedef', '//', '*')):
            continue
        if stripped.startswith('virtual'):
            out.append(stripped.split('(')[0].strip().split()[-1])
            continue
        m = MEMBER.match(line)
        if m and not stripped.startswith('static constexpr'):
            out.append(m.group(1))
    return out


def checked_files() -> list[pathlib.Path]:
    out = []
    for path in sorted(CLIENT.rglob('*')):
        if path.suffix in ('.h', '.hpp', '.cpp') and TARGET in path.read_text(encoding='utf-8', errors='ignore'):
            out.append(path)
    return out


def check(failures: list[str], warnings: list[str]) -> int:
    files = checked_files()
    if not files:
        failures.append(
            f'no file under {CLIENT.relative_to(ROOT).as_posix()} mentions {TARGET}, so every rule below '
            f'would pass vacuously; the macro was renamed or the sources moved')
        return 0

    pairs = 0
    layouts = 0
    constants = 0
    outside: list[str] = []
    per_file: dict[str, int] = {}

    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        text = strip(path.read_text(encoding='utf-8', errors='ignore'))
        lines = text.splitlines()
        owner_of = owners(lines)
        asserts = [text[m.end():m.end() + 400] for m in ASSERT.finditer(text)]
        asserted_names = {n for a in asserts for n in re.findall(r'[\w:]+', a.split(')')[0])}

        for start, cond, a, b, _end in conditionals(lines):
            pairs += 1
            per_file[rel] = per_file.get(rel, 0) + 1
            ma, mb = members(a), members(b)
            if not ma and not mb:
                continue
            owner = owner_of[start]
            if owner is None:
                # Not inside a type, so it cannot be a member declaration: this
                # is a conditional on the macro that guards logic. Counted, not
                # judged - and reported, so "outside any type" stays visible.
                outside.append(f'{rel}:{start + 1} ({cond})')
                continue
            layouts += 1
            # Direct (Owner) or qualified (Outer::Owner) - the latter is how
            # TESObjectCELL's nested ReferenceData is pinned.
            named = owner in asserted_names or any(
                n.endswith('::' + owner) or n == owner for n in asserted_names)
            if not named:
                failures.append(
                    f'{rel}:{start + 1}: the {cond} branch changes {owner}\'s layout '
                    f'(members {ma or mb}) and no static_assert in this file names {owner}; a drift '
                    f'in either branch would then compile, which is the section 2 failure - one '
                    f'target reads the neighbouring field')

        for m in CONSTANT.finditer(text):
            name, value = m.group(1), m.group(2).strip()
            constants += 1
            if 'offsetof' in value:
                continue
            tied = any(re.search(rf'\b{re.escape(name)}\b', a) for a in asserts)
            if not tied:
                shown = value.splitlines()[0].strip() if value else ''
                failures.append(
                    f'{rel}: {name} ({shown}) is a second copy of an offset the struct already '
                    f'computes and nothing ties it back; initialise it from offsetof() or name it '
                    f'in a static_assert')

    if pairs == 0:
        failures.append(
            f'{len(files)} file(s) mention {TARGET} but no conditional on it was found; the scan found '
            f'the macro without the shape it is looking for, so it is covering nothing')
    if layouts == 0:
        failures.append(
            'no conditional declared a member, so the layout rule checked nothing; that is the shape '
            'of a silent narrowing, not of a clean tree')

    for rel, n in sorted(per_file.items()):
        warnings.append(f'{rel}: {n} conditional(s)')
    for site in outside:
        warnings.append(f'{site} guards logic, not a layout')

    return layouts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print(f'  every {TARGET} conditional that changes a struct\'s members is named by a static_assert')
        print('  every k*Offset* constant is computed from offsetof() or named by a static_assert')
        print(f'  files mentioning {TARGET}:')
        for path in checked_files():
            print(f'    {path.relative_to(ROOT).as_posix()}')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    layouts = check(failures, warnings)

    for warning in warnings:
        print(f'  note: {warning}')

    if failures:
        print(f'BRANCH LAYOUT CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('BRANCH LAYOUT CHECK OK')
    print(f'  {layouts} target-dependent layout(s) pinned, {len(checked_files())} file(s) scanned')
    return 0


if __name__ == '__main__':
    sys.exit(main())
