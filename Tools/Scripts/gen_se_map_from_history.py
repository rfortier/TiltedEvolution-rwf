#!/usr/bin/env python3
"""Generate the AE-to-SE (1.5.97) id map from this repository's own git history.

This project originally targeted Skyrim SE 1.5.97 with hardcoded RVA offsets.
Commit 8eaca858 ("feat: AE sigma grindset", 2021-11-13) migrated the whole
tree to AE offsets, and commit 6b2afebc / 35fb2ba3 (2022-02-12) converted the
hardcoded offsets into AE address library ids. Therefore:

    git tree @ 8eaca858^  ->  symbol -> 1.5.97 RVA offset
    current tree          ->  symbol -> AE address library id

Joining both by symbol name yields an "AE id -> 1.5.97 RVA offset" table for
exactly the addresses this code base uses. Every harvested offset is
validated against the real 1.5.97 address library (version-1-5-97-0.bin,
format 1) — a valid 1.5.97 offset must appear in it.

Optionally, (SE id, AE id) pairs from a CommonLibSSE-NG checkout are merged
in for extra coverage (ids our history lacks). History-derived values win on
disagreement.

Usage (from the repo root):

    python3 Tools/Scripts/gen_se_map_from_history.py \
        --se-bin /path/to/version-1-5-97-0.bin \
        [--commonlib /path/to/CommonLibSSE-NG] \
        [--out GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map]

The output file is loaded at runtime by VersionDb when the game version is
1.5.97 (see Code/client/VersionDb.h).
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_ae_to_se_map import parse_bin, collect_codebase_ids  # noqa: E402

# Last commit whose hardcoded offsets were still 1.5.97 (the AE offset
# migration happened in 8eaca858 itself).
SE_SNAPSHOT = "8eaca858^"

OLD_PTR = re.compile(
    r"POINTER_SKYRIMSE\(\s*[^,]+,\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)(?:\s*-\s*0x140000000)?\s*\)", re.S)
NEW_PTR = re.compile(r"POINTER_SKYRIMSE\(\s*[^,]+,\s*(\w+)\s*,\s*(\d+)\s*\)")
OLD_RTTI = re.compile(r"AutoPtr<const void>\s+RTTI_(\w+)\(0x([0-9A-Fa-f]+)\)")
NEW_RTTI = re.compile(r"RttiLocator<[^>]+>\s+registerRtti_(\w+)\((\d+)\)")
OLD_DYNCAST = re.compile(r"AutoPtr<TDynamicCast>\s+DynamicCast\(0x([0-9A-Fa-f]+)\)")
NEW_DYNCAST = re.compile(r"POINTER_SKYRIMSE\(\s*TDynamicCast\s*,\s*DynamicCast\s*,\s*(\d+)\s*\)")
# IDA-style auto names embed their pre-relocation VA (e.g. sub_14063CFB0)
SUB_NAME = re.compile(r"sub_([0-9A-Fa-f]{9,12})$")


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, errors="ignore",
                          cwd=str(REPO_ROOT)).stdout


def is_lib_offset(rva, se_offsets):
    return rva in se_offsets


def harvest_old(se_offsets):
    """symbol -> 1.5.97 RVA, from the pre-AE snapshot."""
    old_ptr, old_rtti = {}, {}

    out = git("grep", "-l", "POINTER_SKYRIMSE(", SE_SNAPSHOT, "--", "Code/client")
    for line in out.splitlines():
        path = line.split(":", 1)[1]
        content = git("show", f"{SE_SNAPSHOT}:{path}")
        for m in OLD_PTR.finditer(content):
            if "#define" in content[max(0, m.start() - 30):m.start()]:
                continue
            var, val = m.group(1), int(m.group(2), 16)
            rva = val - 0x140000000 if val >= 0x140000000 else val
            old_ptr[var] = rva

    rtti_src = git("show", f"{SE_SNAPSHOT}:Code/client/Games/Skyrim/RTTI.cpp")
    for m in OLD_RTTI.finditer(rtti_src):
        old_rtti[m.group(1)] = int(m.group(2), 16)
    m = OLD_DYNCAST.search(rtti_src)
    if m:
        old_rtti["TDynamicCast"] = int(m.group(1), 16)

    # validate: nearly every offset must exist in the 1.5.97 library
    bad = {k: v for d in (old_ptr, old_rtti) for k, v in d.items()
           if not is_lib_offset(v, se_offsets)}
    return old_ptr, old_rtti, bad


def harvest_new():
    """symbol -> AE id, from the current tree."""
    new_ptr, new_rtti = {}, {}
    paths = glob.glob(str(REPO_ROOT / "Code/client/**/*.h"), recursive=True) + \
        glob.glob(str(REPO_ROOT / "Code/client/**/*.cpp"), recursive=True)
    for path in paths:
        content = open(path, encoding="utf-8", errors="ignore").read()
        for m in NEW_PTR.finditer(content):
            if "#define" in content[max(0, m.start() - 30):m.start()]:
                continue
            new_ptr[m.group(1)] = int(m.group(2))

    rtti_src = open(REPO_ROOT / "Code/client/Games/Skyrim/RTTI.cpp",
                    encoding="utf-8", errors="ignore").read()
    for m in NEW_RTTI.finditer(rtti_src):
        new_rtti[m.group(1)] = int(m.group(2))
    m = NEW_DYNCAST.search(rtti_src)
    if m:
        new_rtti["TDynamicCast"] = int(m.group(1))
    return new_ptr, new_rtti


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--se-bin", required=True, help="path to version-1-5-97-0.bin")
    ap.add_argument("--commonlib", help="optional CommonLibSSE-NG checkout for extra pairs")
    ap.add_argument("--out", default="versionlib-ae-to-se-1-5-97-0.map")
    ap.add_argument("--overrides", action="append", default=[],
                    help="optional files with manual 'ae_id 0xRVA' lines, applied after "
                         "auto-generation (e.g. from an IDA session or vtable extraction)")
    ap.add_argument("--se-bins-dir",
                    help="optional dir containing version-1-5-*.bin files; generates a map "
                         "for every 1.5.x version found (SE ids are stable across 1.5.x, so "
                         "ae id -> se id -> per-version offset)")
    args = ap.parse_args()

    se_lib = parse_bin(args.se_bin)
    se_offsets = set(se_lib.values())

    old_ptr, old_rtti, bad = harvest_old(se_offsets)
    new_ptr, new_rtti = harvest_new()
    print(f"old: {len(old_ptr)} pointer vars, {len(old_rtti)} rtti symbols "
          f"({len(bad)} offsets not found in the 1.5.97 lib)")
    print(f"new: {len(new_ptr)} pointer vars, {len(new_rtti)} rtti symbols")

    mapping = {}

    def join(old, new, label):
        # sub_XXXXXXXX names encode their own pre-relocation VA; process them
        # first so they win over plain names sharing the same AE id
        ordered = sorted(new.items(), key=lambda kv: not SUB_NAME.match(kv[0]))
        hit = skipped = 0
        for key, ae_id in ordered:
            if key not in old:
                continue
            rva = old[key]
            sub = SUB_NAME.match(key)
            if sub:
                rva = int(sub.group(1), 16) - 0x140000000
            if ae_id in mapping and mapping[ae_id] != rva:
                # same AE id used by two sites: keep the first, report it
                print(f"  note: {label} '{key}': AE id {ae_id} already mapped to "
                      f"{mapping[ae_id]:#x}, ignoring {rva:#x}")
                skipped += 1
                continue
            mapping[ae_id] = rva
            hit += 1
        print(f"{label}: joined {hit}, conflicts skipped {skipped}")

    join(old_rtti, new_rtti, "RTTI")
    join(old_ptr, new_ptr, "POINTER")

    if args.commonlib:
        pairs = {}
        pat = re.compile(r"(?:RELOCATION_ID|RelocationID)\(\s*(\d+)\s*,\s*(\d+)\s*\)")
        for path in glob.glob(f"{args.commonlib}/include/**/*.h", recursive=True) + \
                glob.glob(f"{args.commonlib}/src/**/*.cpp", recursive=True):
            content = open(path, encoding="utf-8", errors="ignore").read()
            for m in pat.finditer(content):
                se_id, ae_id = int(m.group(1)), int(m.group(2))
                if se_id and se_id in se_lib:
                    pairs[ae_id] = se_lib[se_id]
        added = disagree = 0
        for ae_id, se_off in pairs.items():
            if ae_id in mapping:
                if mapping[ae_id] != se_off:
                    disagree += 1
            else:
                mapping[ae_id] = se_off
                added += 1
        print(f"commonlib: {len(pairs)} pairs, added {added}, disagreements (history kept) {disagree}")

    for ov in args.overrides:
        n = 0
        for line in open(ov, encoding="utf-8", errors="ignore"):
            line = line.split("#", 1)[0].strip()
            if not line: continue
            parts = line.split()
            if len(parts) < 2: continue
            try:
                ae_id, rva = int(parts[0], 0), int(parts[1], 0)
            except ValueError:
                continue
            if rva: mapping[ae_id] = rva; n += 1
        print(f"overrides {ov}: applied {n}")

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# AE address library id -> Skyrim SE 1.5.97 RVA offset\n")
        f.write("# generated by Tools/Scripts/gen_se_map_from_history.py\n")
        for ae_id in sorted(mapping):
            f.write(f"{ae_id} {mapping[ae_id]:#x}\n")

    validated = sum(1 for rva in mapping.values() if is_lib_offset(rva, se_offsets))
    ids = collect_codebase_ids()
    missing = sorted(ids - set(mapping))
    print(f"\nwrote {len(mapping)} mappings to {args.out}")
    print(f"offsets validated against the 1.5.97 lib: {validated}/{len(mapping)}")
    print(f"codebase id coverage: {len(ids & set(mapping))}/{len(ids)} "
          f"({100 * len(ids & set(mapping)) / len(ids):.1f}%), missing {len(missing)}")
    if missing:
        print("missing ids (added after the AE migration, no 1.5.97 reference):")
        print("  " + " ".join(str(i) for i in missing[:40]) + (" ..." if len(missing) > 40 else ""))

    if args.se_bins_dir:
        # SE address library ids are stable across 1.5.x versions, so the
        # 1.5.97 map can be chained through them: ae id -> (1.5.97 offset)
        # -> se id -> per-version offset.
        rva_to_se_id = {v: k for k, v in se_lib.items()}
        base = Path(args.out)
        for bin_path in sorted(glob.glob(f"{args.se_bins_dir}/version-1-5-*.bin")):
            m = re.search(r"version-1-5-(\d+)-0\.bin$", os.path.basename(bin_path))
            if not m:
                continue
            revision = m.group(1)
            if revision == "97":
                continue  # already generated directly from history offsets
            lib = parse_bin(bin_path)
            out_map = {}
            for ae_id, rva in mapping.items():
                se_id = rva_to_se_id.get(rva)
                if se_id is not None and se_id in lib:
                    out_map[ae_id] = lib[se_id]
            out_path = base.parent / f"versionlib-ae-to-se-1-5-{revision}-0.map"
            with open(out_path, "w", encoding="utf-8") as f:
                f.write("# AE address library id -> Skyrim SE 1.5.%s RVA offset\n" % revision)
                f.write("# generated by Tools/Scripts/gen_se_map_from_history.py\n")
                for ae_id in sorted(out_map):
                    f.write(f"{ae_id} {out_map[ae_id]:#x}\n")
            print(f"1.5.{revision}: wrote {len(out_map)} chained mappings to {out_path}")


if __name__ == "__main__":
    main()
