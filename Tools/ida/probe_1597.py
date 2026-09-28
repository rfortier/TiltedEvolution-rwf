#!/usr/bin/env python3
"""Interactive probe on the 1.5.97 IDB (idapro/idalib).

Helpers used for the final 4-function recovery session:
  --xrefs <rva>          list functions referencing the given 1.5.97 rva
  --xrefs-in-fn <rva>    for each function referencing rva, dump decompile
  --bytes <hex>          search .text for masked byte pattern (?? = wildcard)
  --decompile <rva>      decompile the function containing rva
  --asm <rva> [count]    disassemble count instructions from rva

Example:
  python3 Tools/ida/probe_1597.py --xrefs 0x2ec59b8
  python3 Tools/ida/probe_1597.py --bytes 40534883ec2033d2498bd9498911488b
  python3 Tools/ida/probe_1597.py --decompile 0x2ec59b8
"""

import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))

IDB = '/tmp/work_1597.i64'

import idapro  # noqa: E402
idapro.open_database(IDB, run_auto_analysis=False)

import ida_auto  # noqa: E402
import idaapi  # noqa: E402
import idautils  # noqa: E402
import ida_name  # noqa: E402
import ida_funcs  # noqa: E402
import ida_bytes  # noqa: E402
import ida_hexrays  # noqa: E402
import ida_segment  # noqa: E402

ida_auto.auto_wait()
BASE = idaapi.get_imagebase()
print(f'[dbg] base={BASE:#x}', flush=True)


def rva(ea):
    return ea - BASE


def decompile(rva_addr):
    ea = BASE + rva_addr
    f = idaapi.get_func(ea)
    if not f:
        return '(no function)'
    hf = ida_hexrays.decompile(f.start_ea)
    if hf:
        return str(hf)
    return '(decompile failed)'


def disasm(rva_addr, count=40):
    ea = BASE + rva_addr
    out = []
    for _ in range(count):
        if ea >= idaapi.BADADDR:
            break
        out.append(f'{rva(ea):#08x}: {ida_bytes.get_bytes(ea, 16).hex()[:32]:32s} '
                   f'{idaapi.generate_disasm_line(ea, 0)}')
        ea = idaapi.next_head(ea, ea + 0x40)
    return '\n'.join(out)


def xrefs(rva_addr):
    ea = BASE + rva_addr
    out = []
    for xr in idautils.XrefsTo(ea, 0):
        f = idaapi.get_func(xr.frm)
        if f:
            nm = ida_name.get_name(f.start_ea) or ''
            out.append((f.start_ea - BASE, f.end_ea - f.start_ea, xr.frm - BASE, nm))
    return out


def search_bytes(pat_hex):
    """Search .text for a hex pattern with ?? wildcards."""
    pat = bytearray()
    for tok in pat_hex.strip().split():
        if tok == '??':
            pat.append(0)
        else:
            pat.append(int(tok, 16))
    mask = bytearray(0xFF if tok != '??' else 0x00 for tok in pat_hex.strip().split())
    text = ida_segment.get_segm_by_name('.text') or ida_segment.get_first_seg()
    start, end = text.start_ea, text.end_ea
    data = ida_bytes.get_bytes(start, end - start)
    hits = []
    if data:
        i = 0
        while True:
            i = data.find(pat, i)
            if i < 0:
                break
            # mask check
            ok = all((data[i + j] & mask[j]) == pat[j] for j in range(len(pat)))
            if ok:
                hits.append(start + i - BASE)
            i += 1
    return hits


def fns_in_range(lo, hi):
    out = []
    ea = BASE + lo
    while ea < BASE + hi:
        f = idaapi.get_func(ea)
        if f:
            nm = ida_name.get_name(f.start_ea) or ''
            out.append((f.start_ea - BASE, f.end_ea - f.start_ea, nm))
            ea = f.end_ea
        else:
            ea = idaapi.next_head(ea, BASE + hi)
            if ea == idaapi.BADADDR or ea >= BASE + hi:
                break
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--xrefs', type=lambda s: int(s, 0))
    ap.add_argument('--xrefs-in-fn', type=lambda s: int(s, 0))
    ap.add_argument('--bytes')
    ap.add_argument('--decompile', type=lambda s: int(s, 0))
    ap.add_argument('--asm', type=lambda s: int(s, 0))
    ap.add_argument('--count', type=int, default=40)
    ap.add_argument('--scan-range', nargs=2, type=lambda s: int(s, 0))
    ap.add_argument('--out', default=None)
    args = ap.parse_args()

    results = []
    if args.scan_range:
        lo, hi = args.scan_range
        fns = fns_in_range(lo, hi)
        results.append(f'== functions in {lo:#x}..{hi:#x}: {len(fns)} ==')
        for st, sz, nm in fns:
            results.append(f'  fn {st:#08x} (size {sz:#x})  {nm}')
    if args.xrefs is not None:
        rows = xrefs(args.xrefs)
        results.append(f'== xrefs to {args.xrefs:#x}: {len(rows)} ==')
        for st, sz, fr, nm in sorted(rows):
            results.append(f'  fn {st:#08x} (size {sz:#x})  xref@{fr:#08x}  {nm}')
    if args.xrefs_in_fn is not None:
        rows = xrefs(args.xrefs_in_fn)
        results.append(f'== decompile of functions referencing {args.xrefs_in_fn:#x} ==')
        for st, sz, fr, nm in sorted(rows):
            results.append(f'\n##### fn {st:#08x} (size {sz:#x}) {nm} #####')
            results.append(decompile(st))
    if args.bytes:
        hits = search_bytes(args.bytes)
        results.append(f'== bytes {args.bytes}: {len(hits)} hits ==')
        for h in hits[:50]:
            results.append(f'  {h:#08x}')
    if args.decompile is not None:
        results.append(f'== decompile {args.decompile:#x} ==')
        results.append(decompile(args.decompile))
    if args.asm is not None:
        results.append(f'== asm {args.asm:#x} x{args.count} ==')
        results.append(disasm(args.asm, args.count))

    out = '\n'.join(results)
    if args.out:
        with open(args.out, 'w', encoding='utf-8') as f:
            f.write(out + '\n')
        print(f'[+] wrote {args.out} ({len(results)} lines)')
    else:
        print(out)


main()
idapro.close_database()