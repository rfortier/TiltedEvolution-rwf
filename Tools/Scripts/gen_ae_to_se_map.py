#!/usr/bin/env python3
"""Generate versionlib-ae-to-se.map for Skyrim SE 1.5.97 support.

The client resolves every game address through AE-era address library ids
(POINTER_SKYRIMSE / RTTI locators). On a 1.5.97 runtime the client loads a
legacy (format 1) address library, whose ids use a different namespace, so an
AE id -> 1.5.97 RVA offset translation table is required.

This tool joins two name-keyed databases by symbol name:

  * an AE-side table:   symbol name -> AE address library id
  * an SE-side table:   symbol name -> 1.5.97 RVA offset

Both tables are plain TSV files (`name<TAB>value`, `#` starts a comment),
typically exported from IDA after applying the respective address library
labels (see dump_addresses.py / get_skyrim_addresses.py for related flows).

Usage:
    python3 gen_ae_to_se_map.py \
        --ae-bin  versionlib-1-6-1170-0.bin \
        --se-bin  versionlib-1-5-97-0.bin \
        --ae-names ae_names.tsv \
        --se-names se_names.tsv \
        [--overrides manual.tsv] \
        [--out versionlib-ae-to-se.map]

It also reports which ids referenced by the code base (POINTER_SKYRIMSE and
RttiLocator entries under Code/client) are still missing from the generated
map, so gaps can be filled iteratively via --overrides.
"""

import argparse
import glob
import os
import re
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CLIENT_DIR = REPO_ROOT / "Code" / "client"


def read_int(f):
    return struct.unpack("<i", f.read(4))[0]


def read_u8(f):
    return struct.unpack("<B", f.read(1))[0]


def read_u16(f):
    return struct.unpack("<H", f.read(2))[0]


def read_u32(f):
    return struct.unpack("<I", f.read(4))[0]


def read_u64(f):
    return struct.unpack("<Q", f.read(8))[0]


def parse_bin(path):
    """Parse an SKSE address library file (format 1, 2 or 5) -> {id: offset}.

    Formats 1 and 2 share the same layout: format, version[4], module name
    length + name, pointer size, offset count, then delta-encoded entries
    (the offset scaling flag in the high nibble applies to both).

    Format 5 (Skyrim 1.7.99+): 96-byte header (format, version[4], 64-byte
    zero-padded module name, pointer size, reserved, entry count) followed
    by a dense uint32 offset array indexed directly by the AE id
    (0 = unassigned id).
    """
    table = {}
    with open(path, "rb") as f:
        fmt = read_int(f)
        if fmt == 5:
            for _ in range(4):
                read_int(f)  # version
            f.read(64)  # module name
            read_int(f)  # pointer size
            read_int(f)  # reserved
            count = read_u32(f)
            if count == 0 or count > 0x10000000:
                raise ValueError(f"{path}: bad entry count {count}")
            for entry_id in range(count):
                offset = read_u32(f)
                if offset != 0:
                    table[entry_id] = offset
            return table

        if fmt not in (1, 2):
            raise ValueError(f"{path}: unsupported format {fmt}")

        for _ in range(4):
            read_int(f)  # version

        tn_len = read_int(f)
        if 0 < tn_len < 0x10000:
            f.read(tn_len)  # module name

        ptr_size = read_int(f)
        addr_count = read_int(f)

        pvid = 0
        poffset = 0
        for _ in range(addr_count):
            t = read_u8(f)
            low = t & 0xF
            high = t >> 4

            if low == 0:
                q1 = read_u64(f)
            elif low == 1:
                q1 = pvid + 1
            elif low == 2:
                q1 = pvid + read_u8(f)
            elif low == 3:
                q1 = pvid - read_u8(f)
            elif low == 4:
                q1 = pvid + read_u16(f)
            elif low == 5:
                q1 = pvid - read_u16(f)
            elif low == 6:
                q1 = read_u16(f)
            elif low == 7:
                q1 = read_u32(f)
            else:
                raise ValueError(f"{path}: bad id encoding {low}")

            tpoffset = (poffset // ptr_size) if (high & 8) else poffset

            if (high & 7) == 0:
                q2 = read_u64(f)
            elif (high & 7) == 1:
                q2 = tpoffset + 1
            elif (high & 7) == 2:
                q2 = tpoffset + read_u8(f)
            elif (high & 7) == 3:
                q2 = tpoffset - read_u8(f)
            elif (high & 7) == 4:
                q2 = tpoffset + read_u16(f)
            elif (high & 7) == 5:
                q2 = tpoffset - read_u16(f)
            elif (high & 7) == 6:
                q2 = read_u16(f)
            elif (high & 7) == 7:
                q2 = read_u32(f)
            else:
                raise ValueError(f"{path}: bad offset encoding {high & 7}")

            if high & 8:
                q2 *= ptr_size

            table[q1] = q2
            pvid = q1
            poffset = q2

    return table


def parse_names(path):
    """Parse a TSV name table -> {name: value} (decimal or 0x-hex)."""
    table = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            name = parts[0]
            try:
                value = int(parts[1], 0)
            except ValueError:
                print(f"warning: skipping bad line: {line}", file=sys.stderr)
                continue
            table[name] = value
    return table


def parse_overrides(path):
    """Parse an override table -> {ae id (int): SE RVA offset (int)}."""
    table = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                ae_id = int(parts[0], 0)
                se_offset = int(parts[1], 0)
            except ValueError:
                print(f"warning: skipping bad override line: {line}", file=sys.stderr)
                continue
            table[ae_id] = se_offset
    return table


def collect_codebase_ids():
    """Collect all AE ids referenced by the client code base."""
    ids = set()
    pattern = re.compile(r"POINTER_SKYRIMSE\s*\([^,]+,\s*[^,]+,\s*(\d+)\s*\)")
    # e.g. "internal::RttiLocator<IFormFactory> registerRtti_IFormFactory(392214);"
    rtti_pattern = re.compile(r"RttiLocator(?:<[^>]*>)?\s*\w+\s*\(\s*(\d+)\s*\)")
    # ids reaching the address library outside a VersionDbPtr: byte-patch
    # anchors go through GamePatch::Anchor, and a few sites still build a
    # VersionDbPtr directly. They count towards coverage just the same.
    anchor_pattern = re.compile(r"GamePatch::Anchor\s*\(\s*(\d+)")
    raw_pattern = re.compile(r"VersionDbPtr\s*<[^>]*>\s*\w+\s*\(\s*(\d+)")
    for path in glob.glob(str(CLIENT_DIR / "**" / "*.[ch]pp"), recursive=True):
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
            for p in (pattern, rtti_pattern, anchor_pattern, raw_pattern):
                ids.update(int(m) for m in p.findall(content))
    return ids


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ae-bin", required=True, help="AE address library, e.g. versionlib-1-6-1170-0.bin")
    ap.add_argument("--se-bin", required=True, help="SE 1.5.97 address library, e.g. versionlib-1-5-97-0.bin")
    ap.add_argument("--ae-names", required=True, help="TSV: symbol name -> AE address library id")
    ap.add_argument("--se-names", required=True, help="TSV: symbol name -> 1.5.97 RVA offset")
    ap.add_argument("--overrides", help="TSV: AE id -> SE RVA offset, applied last (hex ok)")
    ap.add_argument("--out", default="versionlib-ae-to-se.map", help="output file")
    args = ap.parse_args()

    ae_names = parse_names(args.ae_names)
    se_names = parse_names(args.se_names)

    # sanity: verify the name tables against the binaries they describe
    ae_bin = parse_bin(args.ae_bin)
    se_bin = parse_bin(args.se_bin)
    for name, ae_id in ae_names.items():
        if ae_id not in ae_bin:
            print(f"warning: AE id {ae_id} for '{name}' not present in {args.ae_bin}", file=sys.stderr)
    for name, se_off in se_names.items():
        if se_off not in set(se_bin.values()):
            print(f"warning: SE offset {se_off:#x} for '{name}' not present in {args.se_bin}", file=sys.stderr)

    mapping = {}
    matched = 0
    for name, ae_id in ae_names.items():
        if name in se_names:
            mapping[ae_id] = se_names[name]
            matched += 1

    if args.overrides:
        mapping.update(parse_overrides(args.overrides))

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# AE address library id -> Skyrim SE 1.5.97 RVA offset\n")
        for ae_id in sorted(mapping):
            f.write(f"{ae_id} {mapping[ae_id]:#x}\n")

    print(f"wrote {len(mapping)} mappings ({matched} by name) to {args.out}")

    codebase_ids = collect_codebase_ids()
    missing = sorted(codebase_ids - set(mapping))
    if missing:
        print(f"\n{len(missing)} of {len(codebase_ids)} codebase ids are missing from the map:")
        for ae_id in missing[:50]:
            print(f"  {ae_id}")
        if len(missing) > 50:
            print(f"  ... and {len(missing) - 50} more")
        print("Fill them via --overrides and re-run.")


if __name__ == "__main__":
    main()
