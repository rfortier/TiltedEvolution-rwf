#!/usr/bin/env python3
"""Binary diff matcher: recover 1.5.97 addresses for AE ids using both exes.

For each target AE id (1.6.1170 RVA known from the AE library):
  1. extract the function bytes from the 1.6.1170 exe (length = gap to the
     next AE entry, capped)
  2. normalize via capstone: (mnemonic, operand-shape) tokens with
     immediates/displacements masked (versions differ in relocated constants)
  3. disassemble candidate positions inside the 1.5.97 bracket (mapped AE
     neighbors) and score token-sequence similarity
  4. best score >= threshold -> target address found

Validation: --holdout N pretends N known mappings are missing and checks
recovery against the ground truth.
"""

import argparse
import json
import os
import re
import struct
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))
from gen_ae_to_se_map import parse_bin  # noqa: E402

try:
    import capstone
except ImportError:
    print('pip install --user --break-system-packages capstone', file=sys.stderr)
    sys.exit(1)


class Pe:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        e = struct.unpack_from('<I', self.data, 0x3C)[0]
        nsec = struct.unpack_from('<H', self.data, e + 6)[0]
        optsize = struct.unpack_from('<H', self.data, e + 20)[0]
        self.image_base = struct.unpack_from('<Q', self.data, e + 24 + 24)[0]
        sec_off = e + 24 + optsize
        self.sections = []
        for i in range(nsec):
            name, vsize, vaddr, rsize, roff = struct.unpack_from('<8sIIII', self.data, sec_off + i * 40)
            self.sections.append((name.rstrip(b'\0').decode(), vsize, vaddr, rsize, roff))

    def off2rva(self, fo):
        for name, vsize, vaddr, rsize, roff in self.sections:
            if roff <= fo < roff + rsize:
                return vaddr + (fo - roff)
        return None

    def rva2off(self, rva):
        for name, vsize, vaddr, rsize, roff in self.sections:
            if vaddr <= rva < vaddr + rsize:
                o = roff + (rva - vaddr)
                if o < len(self.data):
                    return o
        return None

    def read_rva(self, rva, n):
        o = self.rva2off(rva)
        return self.data[o:o + n] if o is not None else b''


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = False


def normalize(code, max_insns):
    """(mnemonic, shape) tokens; operands reduced to shapes."""
    toks = []
    for insn in MD.disasm(code, 0):
        ops = insn.op_str
        # mask immediates and anything that looks like an address
        ops = re.sub(r'0x[0-9a-fA-F]+', '?', ops)
        ops = re.sub(r'\b\d{3,}\b', '?', ops)
        toks.append((insn.mnemonic, ops))
        if len(toks) >= max_insns:
            break
    return toks


def similarity(a, b):
    n = min(len(a), len(b))
    if n == 0:
        return 0.0
    same = sum(1 for i in range(n) if a[i] == b[i])
    return same / n


def fn_len_16(ae_sorted, idx, ae_rva, cap=1024):
    for j in range(idx + 1, len(ae_sorted)):
        nxt = ae_sorted[j][1]
        if nxt > ae_rva:
            return min(nxt - ae_rva, cap)
    return cap


PLAUSIBLE_STARTS = set(range(0x40, 0x50)) | {0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57, 0xE9, 0xEB, 0xCC}


def find_in_1597(pe15, pattern_tokens, lo, hi, step=16, max_insns=96):
    """scan [lo,hi) for the best normalized match"""
    best = (0.0, None)
    o_lo, o_hi = pe15.rva2off(lo), pe15.rva2off(hi)
    if o_lo is None or o_hi is None:
        return best
    pos = ((lo + 15) // 16) * 16
    while pos < hi:
        b0 = pe15.data[pe15.rva2off(pos)]
        if b0 in PLAUSIBLE_STARTS:
            code = pe15.read_rva(pos, max_insns * 8)
            if code:
                toks = normalize(code, max_insns)
                sc = similarity(pattern_tokens, toks)
                if sc > best[0]:
                    best = (sc, pos)
        pos += step
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe-1597', required=True)
    ap.add_argument('--exe-1170', required=True)
    ap.add_argument('--ae-bin', default='/tmp/addrlib/SKSE/Plugins/versionlib-1-6-1170-0.bin')
    ap.add_argument('--map', default=os.path.join(REPO, 'GameFiles', 'Skyrim', 'SKSE', 'Plugins',
                                                  'versionlib-ae-to-se-1-5-97-0.map'))
    ap.add_argument('--manifest', default=os.path.join(REPO, 'Tools', 'missing_1_5_97_ids.txt'))
    ap.add_argument('--threshold', type=float, default=0.9)
    ap.add_argument('--holdout', type=int, default=0)
    ap.add_argument('--overrides-out', default='Tools/ida/st_overrides.txt')
    args = ap.parse_args()

    pe15 = Pe(args.exe_1597)
    pe16 = Pe(args.exe_1170)
    text16_end = max(v for n, v, r, rr, ro in pe16.sections if n == '.text') + 0x1000
    ae_lib = parse_bin(args.ae_bin)
    ae_sorted = sorted(ae_lib.items(), key=lambda kv: kv[1])
    ae_pos = {a: i for i, (a, r) in enumerate(ae_sorted)}

    known = {}
    for line in open(args.map, encoding='utf-8'):
        if line.startswith('#'):
            continue
        p = line.split()
        if len(p) >= 2:
            known[int(p[0])] = int(p[1], 0)

    targets = []
    for line in open(args.manifest, encoding='utf-8'):
        if not line.startswith('# ae='):
            continue
        p = line.split('|')
        targets.append((int(p[0].split('=')[1]), p[2].strip()))

    # optional holdout: remove N known mappings to test the matcher
    holdout_truth = {}
    if args.holdout:
        import random
        random.seed(1597)
        fn_keys = [k for k in sorted(known) if ae_lib.get(k) is not None
                   and ae_lib[k] < text16_end]
        sample = random.sample(fn_keys, min(args.holdout, len(fn_keys)))
        for k in sample:
            holdout_truth[k] = known[k]
            del known[k]
        print(f'holdout: removed {len(holdout_truth)} known mappings for validation')

    md = MD
    recovered, failed, holdout_ok = {}, [], 0

    def match_one(ae_id):
        ae_rva_16 = ae_lib.get(ae_id)
        if ae_rva_16 is None:
            return None

        # RTTI type descriptors: the mangled class-name string is identical
        # across versions; find it in the 1.5.97 image directly
        if ae_rva_16 >= text16_end:
            o = pe16.rva2off(ae_rva_16 + 16)
            if o is None:
                return None
            name16 = pe16.data[o:o + 96].split(b'\0')[0]
            if not name16.startswith(b'.?A'):
                return None
            i15 = pe15.data.find(name16)
            if i15 < 0:
                return None
            return pe15.off2rva(i15) - 16

        idx = ae_pos[ae_id]
        ln = fn_len_16(ae_sorted, idx, ae_rva_16)
        code16 = pe16.read_rva(ae_rva_16, ln)
        if not code16:
            return None
        pat = normalize(code16, 96)
        if len(pat) < 4:
            return None
        # bracket from mapped AE neighbors
        i = idx
        lo = hi = None; lo_ae_id = hi_ae_id = None
        for j in range(i - 1, -1, -1):
            if ae_sorted[j][0] in known:
                lo = known[ae_sorted[j][0]]; lo_ae_id = ae_sorted[j][0]; break
        for j in range(i + 1, len(ae_sorted)):
            if ae_sorted[j][0] in known:
                hi = known[ae_sorted[j][0]]; hi_ae_id = ae_sorted[j][0]; break
        if lo is None or hi is None or hi <= lo:
            return None
        # interpolation estimate between the mapped AE neighbors
        lo_ae_rva = ae_lib.get(lo_ae_id) if lo_ae_id else None
        hi_ae_rva = ae_lib.get(hi_ae_id) if hi_ae_id else None
        if lo_ae_rva is None or hi_ae_rva is None or hi_ae_rva <= lo_ae_rva:
            est = (lo + hi) // 2
        else:
            est = lo + round((hi - lo) * (ae_rva_16 - lo_ae_rva) / (hi_ae_rva - lo_ae_rva))

        best = [0.0, None]

        def scan_range(pos_start, pos_end, prefix_len):
            pre = code16[:prefix_len]
            if len(pre) < prefix_len:
                return
            o1, o2 = pe15.rva2off(pos_start), pe15.rva2off(pos_end)
            if o1 is None or o2 is None:
                return
            seg = pe15.data[o1:o2]
            i = seg.find(pre)
            while i >= 0:
                ea = pos_start + i
                code = pe15.read_rva(ea, 96 * 8)
                if code:
                    toks = normalize(code, 96)
                    sc = similarity(pat, toks)
                    if sc > best[0]:
                        best[0] = sc; best[1] = ea
                i = seg.find(pre, i + 1)

        # expanding radii around the interpolation estimate: small radii get a
        # full 16-aligned scan, large ones use a fast prefix-byte prefilter
        for radius in (0x100, 0x400, 0x1000, 0x4000, 0x10000):
            start = max(est - radius, lo)
            end = min(est + radius, hi)
            if radius <= 0x400:
                pos = ((start + 15) // 16) * 16
                while pos < end:
                    o = pe15.rva2off(pos)
                    if o is not None and (data_byte := pe15.data[o]):
                        code = pe15.read_rva(pos, 96 * 8)
                        if code:
                            toks = normalize(code, 96)
                            sc = similarity(pat, toks)
                            if sc > best[0]:
                                best[0] = sc; best[1] = pos
                    pos += 16
            else:
                scan_range(start, end, 6)
            if best[0] >= args.threshold:
                return best[1]
        return None

    for ae_id, rva in sorted(holdout_truth.items()):
        got = match_one(ae_id)
        if got == rva:
            holdout_ok += 1
        else:
            g = f'{got:#x}' if got is not None else 'no match'
            print(f'  holdout MISS ae={ae_id}: truth {rva:#x} got {g}')
    print(f'holdout: {holdout_ok}/{len(holdout_truth)} correct')

    n = 0
    for ae_id, var in sorted(targets):
        if ae_id in known:
            continue
        got = match_one(ae_id)
        if got:
            recovered[ae_id] = got
            n += 1
    print(f'recovered {n} of {len(targets)} targets')

    with open(args.overrides_out, 'w', encoding='utf-8') as f:
        f.write('# matched by binary diff (1.6.1170 -> 1.5.97), normalized capstone tokens\n')
        for ae_id in sorted(recovered):
            f.write(f'{ae_id} {recovered[ae_id]:#x}\n')
    print(f'overrides written: {args.overrides_out}')


if __name__ == '__main__':
    main()
