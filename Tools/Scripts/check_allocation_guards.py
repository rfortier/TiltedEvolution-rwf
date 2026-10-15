#!/usr/bin/env python3
"""Keep the hand-rolled engine helpers from dereferencing what they allocated.

`ExtraDataList` is this client's own reimplementation of an engine structure:
`New()` allocates the object and its bitfield, `Add()` links a node in, and
`Contains()`/`SetType()` read and write one bit of that bitfield. The engine's
version of the same structure is the contract, and the contract that matters
here is internal consistency - the read side and the write side must agree about
what an absent member means.

The defect this exists for was exactly that disagreement:

  * `Contains()` tested `if (bitfield)` and returned false, i.e. "no bits set";
  * `SetType()` - the write side of the same member, reached from `Add()` -
    dereferenced `bitfield` outright.

So a list whose bitfield allocation failed faulted on write while the read path
would have answered "absent". `New()` had the same shape one level up: it wrote
through both allocations without checking either, and its only caller deref'd
the result.

The check is structural: for each of these allocation/access functions it
requires that a pointer the body dereferences is either checked in that body or
guaranteed non-null by a constructor. It deliberately reads the function bodies
rather than a list of blessed lines.

    python Tools/Scripts/check_allocation_guards.py          # the gate
    python Tools/Scripts/check_allocation_guards.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'Code' / 'client'

# Functions whose body must not dereference an unchecked allocation, with the
# member each one touches and the reason it can be null.
#
# file -> { function name: (member dereferenced, why it can be null) }
GUARDED: dict[str, dict[str, tuple[str, str]]] = {
    'Games/Skyrim/ExtraData/ExtraDataList.cpp': {
        'New': ('pExtraDataList', 'Memory::Allocate returns null on failure'),
        'Add': ('apNewData', 'the caller may pass a null node'),
        'SetType': ('bitfield', 'the bitfield allocation may have failed'),
        'Contains': ('bitfield', 'the bitfield allocation may have failed'),
        'GetByType': ('data', 'the list may be empty'),
        'GetCount': ('data', 'the list may be empty'),
    },
}

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def body_of(text: str, func: str) -> str | None:
    """The body of `Cls::func(...)`, or None when it is not defined here."""
    match = re.search(r'\b\w[\w:<>,\s*&]*::' + re.escape(func) + r'\s*\([^)]*\)[^{;]*\{', text)
    if not match:
        return None
    # Walk braces so a nested block does not end the body early.
    depth = 1
    i = match.end()
    while i < len(text) and depth:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    return text[match.end():i - 1]


def deref_pattern(member: str) -> str:
    """Every spelling of a dereference of `member` a body can use.

    `p->field` and `p[i]` are the common ones, but `(*p).field` and a bare `*p`
    write through the same pointer: a body that only used those spellings used
    to be skipped as if it dereferenced nothing at all.
    """
    escaped = re.escape(member)
    return (
        r'\b' + escaped + r'\s*(?:->|\[)'          # p->field, p[i]
        r'|\(\s*\*\s*' + escaped + r'\s*\)'        # (*p).field
        r'|(?<![\w>])\*\s*' + escaped + r'\b'      # *p = ...
    )


def check(failures: list[str]) -> tuple[int, int]:
    """Every allocation-touching helper checks the pointer before using it.

    Returns (bodies read, dereferences actually compared), so the summary
    reports what this run compared rather than how long the table above is.
    """
    examined = 0
    compared = 0
    for rel, functions in GUARDED.items():
        path = CLIENT / rel
        if not path.is_file():
            failures.append(f'{rel}: missing, so the guards it documents cannot be verified')
            continue

        text = strip_comments(path.read_text(encoding='utf-8', errors='ignore'))
        for func, (member, reason) in functions.items():
            body = body_of(text, func)
            if body is None:
                failures.append(
                    f'{rel}: {func}() is no longer defined here; if it moved, move its entry '
                    f'in this gate rather than leaving the check covering nothing')
                continue

            examined += 1

            # Does the body dereference the member at all? A body that reaches
            # the member only through a local alias (`auto p = data;`) really
            # does not dereference it, so this is not a failure - but such a
            # body is not a compared one either, and `compared` is what the
            # corpus floor below counts.
            derefs = re.search(deref_pattern(member), body)
            if not derefs:
                continue

            compared += 1

            # Is it guarded? A test of the same member, or of the pointer it is
            # reached through, on the path before the dereference.
            guarded = re.search(
                r'if\s*\(\s*!?\s*' + re.escape(member) + r'\b|'
                r'if\s*\(\s*' + re.escape(member) + r'\s*(?:==|!=)\s*(?:nullptr|NULL|0)|'
                r'for\s*\([^;]*;\s*' + re.escape(member) + r'\s*;|'
                r'while\s*\(\s*' + re.escape(member) + r'\s*\)',
                body)
            if not guarded:
                failures.append(
                    f'{rel}: {func}() dereferences `{member}` without checking it, but '
                    f'{reason} - guard it the way the sibling function does')

    # The reverse direction: an entry whose function no longer exists, or whose
    # file is gone, would make this check pass while covering nothing - the loop
    # above already reports both. What is left to assert here is the other half
    # of that same failure mode: a run that read the table, found every function
    # and still compared nothing, because no body matched the dereference
    # pattern. The summary used to cover for exactly that case by counting the
    # entries in the table above instead of the dereferences actually compared.
    if not failures and compared == 0:
        failures.append(
            f'{examined} function body(ies) were read but none dereferenced its documented '
            f'member, so this run compared nothing: the table above is not evidence that the '
            f'guards hold, only that the source no longer matches it')

    return examined, compared


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        # --list is documentation, not a verdict: it names the table, and says
        # so, instead of leaving the reader to read a listing as a green build.
        print('hand-rolled helpers whose dereferences are checked:')
        for rel, functions in sorted(GUARDED.items()):
            for func, (member, reason) in sorted(functions.items()):
                print(f'  {rel}:{func}  member={member}  ({reason})')
        print('(this lists the table above; run without --list to compare the source)')
        return 0

    failures: list[str] = []
    examined, compared = check(failures)

    if failures:
        print(f'ALLOCATION GUARD CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('ALLOCATION GUARD CHECK OK')
    print(f'  {examined} helper(s) across {len(GUARDED)} file(s) were read; {compared} of them '
          f'dereference the member this gate documents and were checked against it')
    return 0


if __name__ == '__main__':
    sys.exit(main())
