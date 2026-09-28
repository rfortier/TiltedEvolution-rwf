#!/usr/bin/env python3
"""Translate the 1.6.x intra-function patch offsets to 1.5.97.

Mapping an address library id only names a function start. The client also
patches *inside* those functions - nop this call, flip that jump, replace this
window style - at offsets measured on 1.6.1170, and those offsets do not
survive a recompile. This tool aligns the two disassemblies instruction by
instruction and reports where each patch site moved to.

The alignment key is the normalized instruction (mnemonic plus operand
classes, with addresses replaced by placeholders), so it tolerates the
register allocation and address differences between the builds while still
pinning the shape of each instruction.

A result is only useful when the instruction at the 1.5.97 offset has the same
mnemonic and length as the one on the 1.6 side; anything else is reported as
`mismatch` and must not be patched blindly.

Usage (from the repo root):

    python3 Tools/ida/patch_offsets_1597.py \
        --ae-exe "<1.6.1170 SkyrimSE.exe>" \
        --se-exe "<1.5.97 SkyrimSE.exe>" \
        --ae-bin GameFiles/Skyrim/SKSE/Plugins/versionlib-1-6-1170-0.bin \
        --se-bin GameFiles/Skyrim/SKSE/Plugins/version-1-5-97-0.bin \
        --map    GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map \
        --out    Tools/ida/st_patch_offsets_1597.tsv
"""

import argparse
import bisect
import difflib
import json
import os
import sys

from capstone import CS_ARCH_X86, CS_MODE_64, Cs

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_capstone import Pe, _insn_token, parse_bin, parse_map  # noqa: E402

# (ae id, offset, bytes patched, what the client does there)
PATCH_SITES = [
    (77226, 0x174 + 1, 4, "window style (BSGraphics::InitWindows dwStyle)"),
    (68781, 0x55 + 2, 4, "dinput cooperative level"),
    (36548, 0xFE, 1, "skip startup movie"),
    (51538, 0x15, 2, "favorites menu numbering"),
    (52510, 0x84E, 6, "stats menu appear"),
    (52510, 0xA10, 4, "stats menu background"),
    (52510, 0x1040, 2, "stats menu update"),
    (52518, 0x46, 4, "stats menu controls a"),
    (52518, 0x4A, 2, "stats menu controls b"),
    (82082, 0x682, 5, "menu unfreeze (call site)"),
    (69554, 0x63, 5, "tasklet thread names (call site)"),
    (34452, 0x374, 5, "projectile null check (jump out)"),
    (34452, 0x379, 0, "projectile null check (jump back)"),
    (77246, 9, 5, "frame end (call site, 1.6 only)"),
]


def disasm(pe, starts, fn, cap=0x4000):
    """[(offset, mnemonic, size, token)] for the function at `fn`."""
    i = bisect.bisect_right(starts, fn) - 1
    if i < 0 or starts[i] != fn:
        return None
    nxt = starts[i + 1] if i + 1 < len(starts) else fn + cap
    n = min(max(nxt - fn, 1), cap)
    off = pe.off(fn)
    if off is None:
        return None
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    out = []
    for insn in md.disasm(pe.data[off:off + n], pe.image_base + fn):
        out.append((insn.address - pe.image_base - fn, insn.mnemonic,
                    insn.size, _insn_token(pe, insn, pe.image_base)))
    return out


def translate(ae_body, se_body, ae_off):
    """Where `ae_off` lands in the 1.5.97 body, plus how sure we are."""
    ae_idx = {o: k for k, (o, _, _, _) in enumerate(ae_body)}
    # a patch offset may point into the middle of an instruction (the client
    # skips the opcode to reach an immediate), so find the instruction that
    # contains it and keep the distance from its start
    k, inner = ae_idx.get(ae_off), 0
    if k is None:
        for j, (o, _, size, _) in enumerate(ae_body):
            if o <= ae_off < o + size:
                k, inner = j, ae_off - o
                break
    if k is None:
        return None, "offset outside the 1.6 function"

    sm = difflib.SequenceMatcher(a=[t for _, _, _, t in ae_body],
                                 b=[t for _, _, _, t in se_body])
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal" and i1 <= k < i2:
            j = j1 + (k - i1)
            o_se, mn_se, sz_se, _ = se_body[j]
            _, mn_ae, sz_ae, _ = ae_body[k]
            if mn_se != mn_ae or sz_se != sz_ae:
                return None, f"mismatch: 1.6 {mn_ae}/{sz_ae} vs 1.5.97 {mn_se}/{sz_se}"
            return o_se + inner, f"aligned on {mn_ae} (+{inner} into it)"
    return None, "instruction not in an aligned run"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ae-exe", required=True)
    ap.add_argument("--se-exe", required=True)
    ap.add_argument("--ae-bin", required=True)
    ap.add_argument("--se-bin", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    pe_ae, pe_se = Pe(args.ae_exe), Pe(args.se_exe)
    ae_lib, se_lib = parse_bin(args.ae_bin), parse_bin(args.se_bin)
    anchor = parse_map(args.map)
    ae_starts = sorted(r for r in ae_lib.values() if pe_ae.sec_of(r) is pe_ae.text)
    se_starts = sorted(r for r in se_lib.values() if pe_se.sec_of(r) is pe_se.text)

    rows, ok = [], 0
    for ae_id, ae_off, width, what in PATCH_SITES:
        ae_rva, se_rva = ae_lib.get(ae_id), anchor.get(ae_id)
        if ae_rva is None or se_rva is None:
            rows.append((ae_id, ae_off, None, "id not mapped on 1.5.97", what))
            continue
        ae_body = disasm(pe_ae, ae_starts, ae_rva)
        se_body = disasm(pe_se, se_starts, se_rva)
        if not ae_body or not se_body:
            rows.append((ae_id, ae_off, None, "function body unreadable", what))
            continue
        se_off, why = translate(ae_body, se_body, ae_off)
        rows.append((ae_id, ae_off, se_off, why, what))
        ok += se_off is not None

    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# 1.6.1170 patch site -> 1.5.97 offset, by instruction alignment\n")
        f.write("# generated by Tools/ida/patch_offsets_1597.py\n")
        f.write("# ae_id\tae_offset\tse_offset\tnote\tsite\n")
        for ae_id, ae_off, se_off, why, what in rows:
            f.write(f"{ae_id}\t{ae_off:#x}\t"
                    f"{se_off:#x}" if se_off is not None else
                    f"{ae_id}\t{ae_off:#x}\t-")
            f.write(f"\t{why}\t{what}\n")

    w = max(len(w) for _, _, _, _, w in rows)
    print(f"{'ae id':>7} {'1.6 off':>9} {'1.5.97 off':>11}  {'site':<{w}}  note")
    for ae_id, ae_off, se_off, why, what in rows:
        print(f"{ae_id:>7} {ae_off:>#9x} {(hex(se_off) if se_off is not None else '-'):>11}"
              f"  {what:<{w}}  {why}")
    print(f"\ntranslated {ok}/{len(rows)} -> {args.out}")


if __name__ == "__main__":
    main()
