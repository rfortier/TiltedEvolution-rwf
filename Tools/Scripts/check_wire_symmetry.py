#!/usr/bin/env python3
"""Keep every wire message readable by the peer that receives it.

A message is defined twice - once in `SerializeRaw` and once in `DeserializeRaw` -
and the two must agree on the sequence of wire operations, because that sequence
decides how many bytes each side consumes. Nothing about the language or the
compiler ties them together:

  * a field written but never read shifts every later field by its width;
  * a field read but never written consumes bytes that belong to the next field;
  * the same fields in a different order corrupts both.

None of those fail loudly. The reader does not run out of buffer in general (the
packet still has bytes), so the symptom is a message whose *values* are wrong -
a position, a form id, a count - and the stream stays in sync, which is what
makes it expensive to find. This is the same family as the count/width defects
recorded in docs/PITFALLS.md section 45, approached from the other end: there the
writer wrote more entries than its count promised, here the two halves can
disagree about the field sequence itself.

The comparison is on wire *operations* rather than field names, because the read
side legitimately uses different names - a loop index or a map key stands in for
what the write side called the entry:

    write:  WriteVarInt(Tick) WriteVarInt(count) WriteVarInt(kvp.first) kvp.second.Serialize
    read:   ReadVarInt(Tick)  ReadVarInt(count)  ReadVarInt(cServerId)  Updates[cServerId].Deserialize

Both are `VarInt VarInt VarInt Nested`, which is what matters. A loop body is
collapsed to one iteration for the same reason: both sides iterate the same
count, so the repeat count carries no information the sequence does not.

    python Tools/Scripts/check_wire_symmetry.py          # the gate
    python Tools/Scripts/check_wire_symmetry.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ENCODING = ROOT / 'Code' / 'encoding'
MESSAGES = ENCODING / 'Messages'
STRUCTS = ENCODING / 'Structs'

# Every directory whose .cpp files define a Serialize/Deserialize pair.
#
# `Structs` was left out of the first version of this gate, and it holds 23 more
# pairs - a third of the total. That is the scope failure this file's own sibling
# gate (check_ostimng_versions.py) had: a scan root written for the first case
# found, not for the rule. Listing both here, and failing when a listed directory
# contributes no pair at all, is what keeps the next directory from being missed.
SCANNED = [MESSAGES, STRUCTS]

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')

# Wire operations, in the order they appear in a body. Each entry maps an
# operation on one side to the operation it must be paired with on the other.
WRITE_OPS = [
    (re.compile(r'Serialization::Write(\w+)\s*\(\s*aWriter'), lambda m: m.group(1)),
    (re.compile(r'\.Serialize\s*\(\s*aWriter'), lambda _m: 'Nested'),
    (re.compile(r'aWriter\.WriteBits\s*\([^,]+,\s*(\d+)\s*\)'), lambda m: f'Bits{m.group(1)}'),
]
READ_OPS = [
    (re.compile(r'Serialization::Read(\w+)\s*\(\s*aReader'), lambda m: m.group(1)),
    (re.compile(r'\.Deserialize\s*\(\s*aReader'), lambda _m: 'Nested'),
    (re.compile(r'aReader\.ReadBits\s*\([^,]+,\s*(\d+)\s*\)'), lambda m: f'Bits{m.group(1)}'),
]


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def body(text: str, name: str) -> str | None:
    """The body of `SomeClass::name(...)`, brace-matched."""
    match = re.search(r'void\s+\w+::' + re.escape(name) + r'\s*\([^)]*\)[^{;]*\{', text)
    if not match:
        return None
    i = match.end()
    depth = 1
    start = i
    while i < len(text) and depth:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    return text[start:i - 1]


def operations(text: str, is_write: bool) -> list[str]:
    found: list[tuple[int, str]] = []
    for pattern, name in (WRITE_OPS if is_write else READ_OPS):
        for match in pattern.finditer(text):
            found.append((match.start(), name(match)))
    return [op for _pos, op in sorted(found)]


def collapse_repeats(ops: list[str]) -> list[str]:
    """A loop body repeated is one iteration's worth; keep the first two.

    Two are kept rather than one because `count` followed by a single entry is
    the normal shape, and dropping to one would make a genuine
    `count, entry` vs `count, entry, entry` difference invisible.
    """
    out: list[str] = []
    for i, op in enumerate(ops):
        if i >= 2 and op == ops[i - 1] == ops[i - 2]:
            continue
        out.append(op)
    return out


def sources() -> list[tuple[pathlib.Path, pathlib.Path, str, str]]:
    """(path, root, write function, read function) for every scanned pair.

    `Messages` names its halves SerializeRaw/DeserializeRaw; the `Structs` the
    messages are built from name them Serialize/Deserialize. Same rule, two
    spellings, so both are scanned.
    """
    found: list[tuple[pathlib.Path, pathlib.Path, str, str]] = []
    for root, write_fn, read_fn in (
        (MESSAGES, 'SerializeRaw', 'DeserializeRaw'),
        (STRUCTS, 'Serialize', 'Deserialize'),
    ):
        if not root.is_dir():
            continue
        found.extend((p, root, write_fn, read_fn) for p in sorted(root.glob('*.cpp')))
    return found


def check(failures: list[str]) -> int:
    """Every message and struct writes and reads the same wire operations."""
    checked = 0
    contributed = {root: 0 for root in SCANNED}

    for path, root, write_fn, read_fn in sources():
        text = strip_comments(path.read_text(encoding='utf-8', errors='ignore'))
        write = body(text, write_fn)
        read = body(text, read_fn)

        if write is None or read is None:
            # A file with neither is a helper; one with only one half is a bug.
            if write is None and read is None:
                continue
            missing = write_fn if write is None else read_fn
            failures.append(
                f'{path.name}: defines only one half of the pair - {missing} is missing, so the '
                f'message cannot round-trip')
            continue

        contributed[root] += 1
        checked += 1
        written = collapse_repeats(operations(write, True))
        read_back = collapse_repeats(operations(read, False))

        if written == read_back:
            continue

        detail = [
            f'    write: {" ".join(written) or "(nothing)"}',
            f'    read : {" ".join(read_back) or "(nothing)"}',
        ]
        if len(written) != len(read_back):
            longer, shorter = ('write', 'read') if len(written) > len(read_back) else ('read', 'write')
            detail.append(
                f'    the {longer} side performs {abs(len(written) - len(read_back))} more '
                f'operation(s) than the {shorter} side')
        elif sorted(written) == sorted(read_back):
            detail.append('    same operations, DIFFERENT ORDER')
        else:
            only_w = [o for o in written if o not in read_back]
            only_r = [o for o in read_back if o not in written]
            if only_w:
                detail.append(f'    only written: {sorted(set(only_w))}')
            if only_r:
                detail.append(f'    only read   : {sorted(set(only_r))}')

        failures.append(f'{path.name}: {write_fn} and {read_fn} disagree\n' + '\n'.join(detail))

    # A scanned directory that contributes nothing means the scan path is stale
    # or the directory moved, and this check would pass while covering a hole.
    # That is exactly how `Structs` - 23 pairs, a third of the total - stayed
    # outside the first version of this gate.
    for root in SCANNED:
        if not root.is_dir():
            failures.append(
                f'{root.relative_to(ROOT).as_posix()} does not exist; drop it from SCANNED or fix '
                f'the path, rather than leaving this check covering nothing there')
        elif contributed[root] == 0:
            failures.append(
                f'{root.relative_to(ROOT).as_posix()} contributed no Serialize/Deserialize pair; '
                f'either its scan path is stale or it no longer holds any, in which case drop it '
                f'from SCANNED rather than checking nothing')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  every scanned file defines both halves of its Serialize/Deserialize pair')
        print('  both halves perform the same wire operations in the same order')
        print('  (field names may differ; loop bodies collapse to one iteration)')
        for root, write_fn, read_fn in (
            (MESSAGES, 'SerializeRaw', 'DeserializeRaw'),
            (STRUCTS, 'Serialize', 'Deserialize'),
        ):
            n = len(list(root.glob('*.cpp'))) if root.is_dir() else 0
            print(f'  {root.relative_to(ROOT).as_posix()}: {n} file(s), {write_fn}/{read_fn}')
        return 0

    failures: list[str] = []
    checked = check(failures)

    if failures:
        print(f'WIRE SYMMETRY CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('WIRE SYMMETRY CHECK OK')
    print(f'  {checked} pair(s) write and read the same wire operations in the same order')
    return 0


if __name__ == '__main__':
    sys.exit(main())
