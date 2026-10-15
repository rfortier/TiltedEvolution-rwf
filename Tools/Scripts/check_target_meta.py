#!/usr/bin/env python3
"""Keep the 1.6.1170 target metadata a checkable description of the client.

`Tools/ida/st_target_meta_1170.json` records, for 91 engine addresses the client
resolves: which client variable binds it, where in the client that binding sits,
the absolute 1.6.1170 address, and the disassembly of the function there. It is
the evidence behind the address-library ids in the client, and until now nothing
read it - `git grep` for the filename finds the IDA scripts that write it and a
README, no gate.

That matters because the file makes three claims that can each go wrong without
any symptom:

  1. **The address is right.** `ea_1170` is an absolute VA; subtracting the image
     base gives the RVA, which must equal what `versionlib-1-6-1170-0.bin`
     records for that id. A wrong value here means the disassembly that was
     reviewed is not the function the client calls.
  2. **The client still binds that id to that variable.** `POINTER_SKYRIMSE` is
     function-local, so one name legitimately maps to different ids in different
     functions - the metadata's `callsite` is what identifies *which* binding it
     describes. If the client moves a variable to another id, the recorded
     disassembly stops describing what runs, and the metadata still looks fine.
  3. **The id resolves everywhere the client can run.** The file is named for
     1.6.1170, but the client loads the same ids on every AE build; an id that
     exists only on 1.6.1170 would resolve on one runtime and return null (or,
     on a legacy runtime, the shared stub) on another.

Claim 2 is checked as a (file, variable, id) triple rather than by line number,
because the line numbers in the file are from the revision it was captured on and
have drifted since - 46 of the 91 no longer point at their binding. The triple is
what the claim actually is; the line is a convenience that rots.

    python Tools/Scripts/check_target_meta.py          # the gate
    python Tools/Scripts/check_target_meta.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
META = ROOT / 'Tools' / 'ida' / 'st_target_meta_1170.json'
LIB_DIR = ROOT / 'GameFiles' / 'Skyrim' / 'SKSE' / 'Plugins'
AE_1170 = LIB_DIR / 'versionlib-1-6-1170-0.bin'
CLIENT = ROOT / 'Code' / 'client'

# The image base these addresses were captured at. `ea_1170` is absolute, and the
# address library records RVAs.
IMAGE_BASE = 0x140000000

# The two ways the client binds an id. A `var` of "rtti" marks the second form,
# which is how the RTTI locators are recorded.
POINTER_FORM = r'POINTER_SKYRIMSE\s*\([^,]+,\s*{var}\s*,\s*{id}\s*\)'
RTTI_FORM = r'RttiLocator<[^>]+>\s+\w+\s*\(\s*{id}\s*\)'


def read_library(path: pathlib.Path) -> dict[int, int]:
    """A format-2 (AE) address library as {id: rva}."""
    data = path.read_bytes()
    if len(data) < 32:
        return {}
    (fmt,) = struct.unpack_from('<i', data, 0)
    if fmt not in (2, 5):
        return {}

    if fmt == 5:
        # 4 ints + a 64 byte module name + ptr size + reserved, then a dense
        # uint32 offset array indexed by id.
        off = 4 + 16 + 64 + 4 + 4
        (count,) = struct.unpack_from('<I', data, off)
        off += 4
        out: dict[int, int] = {}
        for i in range(count):
            (rva,) = struct.unpack_from('<I', data, off)
            off += 4
            if rva:
                out[i] = rva
        return out

    off = 4 + 16
    (name_len,) = struct.unpack_from('<i', data, off)
    off += 4 + max(name_len, 0)
    ptr_size, count = struct.unpack_from('<ii', data, off)
    off += 8

    out = {}
    prev_id = 0
    prev_off = 0
    for _ in range(count):
        type_byte = data[off]
        off += 1
        low, high = type_byte & 0xF, type_byte >> 4

        if low == 0:
            (q1,) = struct.unpack_from('<Q', data, off)
            off += 8
        elif low == 1:
            q1 = prev_id + 1
        elif low == 2:
            q1 = prev_id + data[off]
            off += 1
        elif low == 3:
            q1 = prev_id - data[off]
            off += 1
        elif low == 4:
            (d,) = struct.unpack_from('<H', data, off)
            off += 2
            q1 = prev_id + d
        elif low == 5:
            (d,) = struct.unpack_from('<H', data, off)
            off += 2
            q1 = prev_id - d
        elif low == 6:
            (q1,) = struct.unpack_from('<H', data, off)
            off += 2
        else:
            (q1,) = struct.unpack_from('<I', data, off)
            off += 4

        base = prev_off // ptr_size if (high & 8) else prev_off
        kind = high & 7
        if kind == 0:
            (q2,) = struct.unpack_from('<Q', data, off)
            off += 8
        elif kind == 1:
            q2 = base + 1
        elif kind == 2:
            q2 = base + data[off]
            off += 1
        elif kind == 3:
            q2 = base - data[off]
            off += 1
        elif kind == 4:
            (d,) = struct.unpack_from('<H', data, off)
            off += 2
            q2 = base + d
        elif kind == 5:
            (d,) = struct.unpack_from('<H', data, off)
            off += 2
            q2 = base - d
        elif kind == 6:
            (q2,) = struct.unpack_from('<H', data, off)
            off += 2
        else:
            (q2,) = struct.unpack_from('<I', data, off)
            off += 4

        if high & 8:
            q2 *= ptr_size
        out[q1] = q2
        prev_off, prev_id = q2, q1
    return out


def metadata() -> dict[str, dict]:
    if not META.is_file():
        return {}
    return json.loads(META.read_text(encoding='utf-8'))


def check(failures: list[str], warnings: list[str]) -> int:
    meta = metadata()
    if not meta:
        failures.append(f'{META.relative_to(ROOT).as_posix()} is missing or empty')
        return 0

    if not AE_1170.is_file():
        failures.append(f'{AE_1170.relative_to(ROOT).as_posix()} is missing; the recorded addresses '
                        f'cannot be checked against the library they came from')
        return 0

    reference = read_library(AE_1170)
    if not reference:
        failures.append(f'{AE_1170.relative_to(ROOT).as_posix()} is not a readable AE library')
        return 0

    checked = 0
    for raw_id, entry in sorted(meta.items(), key=lambda kv: int(kv[0])):
        ae_id = int(raw_id)
        var = entry.get('var', '')
        callsite = entry.get('callsite', '')
        ea = entry.get('ea_1170', '')

        if not var or not callsite or not ea:
            failures.append(f'{raw_id}: the entry is missing var/callsite/ea_1170, so nothing about '
                            f'it can be checked')
            continue

        # 1. The address matches the library for that id.
        try:
            rva = int(ea, 16) - IMAGE_BASE
        except ValueError:
            failures.append(f'{raw_id}: ea_1170 {ea!r} is not a hex address')
            continue
        recorded = reference.get(ae_id)
        if recorded is None:
            failures.append(
                f'{raw_id}: {AE_1170.name} does not map this id, so the recorded address '
                f'{ea} cannot be the function it names')
        elif recorded != rva:
            failures.append(
                f'{raw_id}: ea_1170 {ea} is RVA 0x{rva:x}, but {AE_1170.name} records '
                f'0x{recorded:x} for this id; the disassembly that was reviewed is not the '
                f'function this id resolves to')

        # 2. The client still binds this id to this variable, at this file.
        rel = callsite.split(':')[0]
        path = CLIENT / rel
        if not path.is_file():
            failures.append(f'{raw_id}: {rel} does not exist, so the recorded binding cannot be '
                            f'found')
            continue
        text = path.read_text(encoding='utf-8', errors='replace')
        pointer = re.search(POINTER_FORM.format(var=re.escape(var), id=ae_id), text)
        rtti = re.search(RTTI_FORM.format(id=ae_id), text)
        if not (pointer or rtti):
            # Report what the file does bind the name to, when it binds it at all.
            others = sorted({m.group(1) for m in
                             re.finditer(r'POINTER_SKYRIMSE\s*\([^,]+,\s*' + re.escape(var) +
                                         r'\s*,\s*(\d+)\s*\)', text)})
            detail = f'that file binds {var} to {others}' if others else f'that file never binds {var}'
            failures.append(
                f'{raw_id} ({var}) is recorded against {rel}, but {detail}; the metadata describes '
                f'a binding the client no longer has')

        checked += 1

    # 3. Every recorded id resolves on every AE runtime, not only on 1.6.1170.
    runtimes = sorted(p for p in LIB_DIR.glob('versionlib-*.bin') if not p.name.endswith('.map'))
    if not runtimes:
        failures.append(
            f'no AE address library found under {LIB_DIR.relative_to(ROOT).as_posix()}, so no '
            f'recorded id was checked against any runtime. These libraries are tracked under version '
            f'control, so finding none means the scan lost its corpus rather than that the ids are '
            f'covered')
    else:
        for path in runtimes:
            library = read_library(path)
            if not library:
                failures.append(
                    f'{path.name} is present but is not a readable AE address library, so no recorded '
                    f'id was checked against it. These libraries are tracked under version control, so '
                    f'an unreadable one means the coverage claim is unfounded rather than that this '
                    f'runtime needs no check')
                continue
            missing = sorted(int(i) for i in meta if int(i) not in library)
            if missing:
                failures.append(
                    f'{path.name}: {len(missing)} recorded id(s) do not resolve there, e.g. '
                    f'{missing[:6]}; the client resolves these on every AE build, so an id that '
                    f'exists on one runtime only is a null lookup (or, on a legacy runtime, the '
                    f'shared stub) everywhere else')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        meta = metadata()
        runtimes = sorted(p for p in LIB_DIR.glob('versionlib-*.bin') if not p.name.endswith('.map'))
        print('checked:')
        print(f'  every ea_1170 in {META.relative_to(ROOT).as_posix()} equals the RVA '
              f'{AE_1170.name} records for that id')
        print('  the client still binds each id to the recorded variable in the recorded file')
        print(f'  every recorded id resolves on all {len(runtimes)} AE runtime(s)')
        print(f'  {len(meta)} recorded target(s)')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    if warnings:
        for warning in warnings:
            print(f'  note: {warning}')

    if failures:
        print(f'TARGET METADATA CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    runtimes = [p for p in LIB_DIR.glob('versionlib-*.bin') if not p.name.endswith('.map')]
    print('TARGET METADATA CHECK OK')
    print(f'  {checked} recorded target(s) agree with {AE_1170.name}, the client bindings, and '
          f'all {len(runtimes)} AE runtime(s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
