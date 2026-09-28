#!/usr/bin/env python3
"""Recover missing 1.5.97 RVAs for AE address-library ids -- no IDA required.

Pure-python pipeline built on capstone + numpy. Memory stays bounded (both
exes are kept as raw bytes, the only big allocation is a chunked displacement
array for the data-xref scan), every target is isolated in its own
try/except, and the report is written incrementally so an interrupted run can
be restarted. Targets that cannot be proven are left unmapped (stability
first -- a wrong RVA on a 1.5.97 runtime is worse than a stub).

Signals
  function targets:  shared string refs with the 1.6.1170 counterpart (w=2),
                     call targets whose 1.6 callee is already mapped (w=3),
                     function size ratio (w=1). Candidates are the 1.5.97
                     address-library function starts inside the bracket
                     spanned by the nearest already-mapped AE-id neighbours.
  data targets:      rip-relative xrefs to the target are located in the
                     1.6.1170 .text (numpy displacement scan + capstone
                     verification); each xref's containing function must be
                     already mapped, and its 1.5.97 counterpart must expose
                     the same ordered rip-ref list, yielding a candidate
                     1.5.97 RVA. All xrefs must agree (>=2 to accept).
  RTTI targets:      the ".?AV..." type name is read next to the 1.6.1170
                     TypeDescriptor and searched verbatim in the 1.5.97
                     binary (deterministic; accept on a unique hit).

Usage (repo root):
    python3 Tools/ida/match_capstone.py \
        --ae-exe "/home/sj/下载/SkyrimSE (1)/SkyrimSE.exe" \
        --se-exe "/home/sj/桌面/qwqw/extracted/Cracks/CODEX/SkyrimSE.exe" \
        --ae-bin  /tmp/addrlib/SKSE/Plugins/versionlib-1-6-1170-0.bin \
        --se-bin  /tmp/addrlib/SKSE/Plugins/version-1-5-97-0.bin \
        --map     GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map \
        --missing Tools/missing_1_5_97_ids.txt \
        --out-overrides Tools/ida/st_overrides.txt \
        --out-report    Tools/ida/st_capstone_report.json
"""

import argparse
import bisect
import json
import struct
import sys
import time
from collections import Counter
from pathlib import Path

import numpy as np
from capstone import CS_ARCH_X86, CS_MODE_64, Cs, x86_const

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "Tools" / "Scripts"))
from gen_ae_to_se_map import parse_bin  # noqa: E402

MIN_STR = 4          # minimum printable chars for a shared string signal
MAX_FN = 0x8000      # disassembly byte cap per function
MAX_INSNS = 300_000  # defensive instruction cap per function
MAX_CANDS = 3000     # give up on runaway brackets
BRACKET_WIDE = 0x20000  # bracketed span wider than this is considered unsafe
W_STR, W_CALL, W_SIZE = 2, 3, 1
ACCEPT_SCORE = 4
ACCEPT_MARGIN = 2
TOKEN_ACCEPT = 0.72     # min Dice similarity of the normalized token streams
TOKEN_MARGIN = 0.10     # required gap to the runner-up candidate
RIP_XREF_CHUNK = 4 << 20  # bytes of .text processed per numpy chunk


# ---------------------------------------------------------------- PE
class Pe:
    """Minimal PE32+ reader: sections, image base, rva<->offset, cstrings."""

    def __init__(self, path):
        self.path = str(path)
        with open(path, "rb") as f:
            self.data = f.read()
        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
            raise ValueError(f"{self.path}: not a PE")
        coff = e_lfanew + 4
        nsec = struct.unpack_from("<H", self.data, coff + 2)[0]
        opt_size = struct.unpack_from("<H", self.data, coff + 16)[0]
        opt = coff + 20
        magic = struct.unpack_from("<H", self.data, opt)[0]
        if magic != 0x20B:  # PE32+
            raise ValueError(f"{self.path}: not a 64-bit PE (magic {magic:#x})")
        self.image_base = struct.unpack_from("<Q", self.data, opt + 24)[0]
        self.sections = []
        sec = opt + opt_size
        for _ in range(nsec):
            name = self.data[sec:sec + 8].rstrip(b"\x00").decode("latin1")
            vsz, va, raw_size, raw = struct.unpack_from("<IIII", self.data, sec + 8)
            chars = struct.unpack_from("<I", self.data, sec + 36)[0]
            self.sections.append(
                {"name": name, "va": va, "vsz": vsz, "raw_size": raw_size,
                 "raw": raw, "chars": chars})
            sec += 40
        exe = [s for s in self.sections if s["chars"] & 0x20000000]
        if not exe:
            raise ValueError(f"{self.path}: no executable section")
        self.text = max(exe, key=lambda s: s["raw_size"])

    def sec_of(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsz"], s["raw_size"]):
                return s
        return None

    def off(self, rva):
        s = self.sec_of(rva)
        if s is None:
            return None
        o = rva - s["va"] + s["raw"]
        if o < 0 or o >= len(self.data):
            return None
        return o

    def read(self, rva, n):
        o = self.off(rva)
        if o is None:
            return b""
        return self.data[o:o + n]

    def read_cstr(self, rva, limit=256):
        o = self.off(rva)
        if o is None:
            return None
        end = self.data.find(b"\x00", o, o + limit)
        if end < 0:
            return None
        return self.data[o:end]

    def rva_of_file(self, off):
        for s in self.sections:
            if s["raw_size"] and s["raw"] <= off < s["raw"] + s["raw_size"]:
                return s["va"] + off - s["raw"]
        return None


# ---------------------------------------------------------------- feature extraction
class Features:
    __slots__ = ("strings", "callees", "refs", "size", "tokens")

    def __init__(self, strings, callees, refs, size, tokens):
        self.strings = strings      # frozenset[str]
        self.callees = callees      # frozenset[int] rvas of call/jmp targets
        self.refs = refs            # list[int] ordered rip-relative data refs
        self.size = size            # int bytes
        self.tokens = tokens        # list[str] normalized instruction tokens


def _imm_class(pe, imm, base):
    """Operand class for an immediate: 'A' address, 'i' small const, 'I' other."""
    v = imm - base
    if 0 <= v < 0x20000000 and pe.sec_of(v) is not None:
        return "A"
    if abs(imm) <= 0x1000:
        return f"i{imm}"
    return "I"


def _op_class(pe, op, base, rip_ok):
    """One-character-ish operand class; rip-relative data refs -> 'A'."""
    if op.type == x86_const.X86_OP_REG:
        return "R"
    if op.type == x86_const.X86_OP_IMM:
        return _imm_class(pe, op.imm, base)
    if op.type == x86_const.X86_OP_MEM:
        if op.mem.base == x86_const.X86_REG_RIP:
            return "A" if rip_ok else "M"
        cls = "M"
        if op.mem.index:
            cls += f"x{op.mem.scale}"
        if op.mem.disp != 0 and abs(op.mem.disp) <= 0x1000:
            cls += f"d{op.mem.disp}"
        elif abs(op.mem.disp) > 0x1000:
            cls += "D"
        return cls
    return "?"


def _insn_token(pe, insn, base):
    """Normalized instruction token: address operands become placeholders."""
    parts = [insn.mnemonic]
    ops = []
    for op in insn.operands:
        if op.type == x86_const.X86_OP_MEM and op.mem.base == x86_const.X86_REG_RIP:
            ops.append("A")
        else:
            ops.append(_op_class(pe, op, base, rip_ok=False))
    parts.append("".join(ops))
    return "|".join(parts)


def extract(pe, starts, rva):
    """Disassemble the function at `rva` (must be a function start in `starts`).

    Returns a Features object, or None when `rva` is not a known function
    start or the region is unreadable.
    """
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0 or starts[i] != rva:
        return None
    nxt = starts[i + 1] if i + 1 < len(starts) else rva + MAX_FN
    size = min(max(nxt - rva, 1), MAX_FN)
    off = pe.off(rva)
    if off is None:
        return None
    code = pe.data[off:off + size]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    base = pe.image_base
    strings, callees, refs, tokens = set(), set(), [], []
    n_insns = 0
    for insn in md.disasm(code, base + rva):
        n_insns += 1
        tokens.append(_insn_token(pe, insn, base))
        for op in insn.operands:
            if op.type == x86_const.X86_OP_MEM and op.mem.base == x86_const.X86_REG_RIP:
                trva = insn.address + insn.size + op.mem.disp - base
                if 0 <= trva < 0x20000000:
                    refs.append(trva)
                    s = pe.sec_of(trva)
                    if s is not None and s["name"] in (".rdata", ".data", "rdata", "data"):
                        cstr = pe.read_cstr(trva)
                        if (cstr and len(cstr) >= MIN_STR
                                and all(32 <= b < 127 for b in cstr)):
                            strings.add(cstr[:96].decode("latin1"))
            elif op.type == x86_const.X86_OP_IMM and insn.mnemonic in ("call", "jmp"):
                trva = op.imm - base
                s = pe.sec_of(trva)
                if s is not None and s is pe.text:
                    callees.add(trva)
        if n_insns >= MAX_INSNS or insn.address - (base + rva) >= MAX_FN:
            break
    return Features(frozenset(strings), frozenset(callees), refs, size, tokens)


# ---------------------------------------------------------------- matching
def bracket_for(anchor_sorted, anchor, ae_id):
    """(lo_rva, hi_rva) 1.5.97 span between the mapped neighbours of ae_id."""
    lo_i = bisect.bisect_left(anchor_sorted, ae_id) - 1
    hi_i = bisect.bisect_right(anchor_sorted, ae_id)
    if lo_i < 0 or hi_i >= len(anchor_sorted):
        return None
    lo_r, hi_r = anchor[anchor_sorted[lo_i]], anchor[anchor_sorted[hi_i]]
    if lo_r >= hi_r or hi_r - lo_r > BRACKET_WIDE:
        return None
    return lo_r, hi_r


def bracket_for_rva(ae_rva, ae_rva_sorted, anchor_by_ae_rva):
    """Span in 1.5.97 space between the mapped anchors whose *1.6* RVAs
    bracket the target. The AE-migration preserved layout order, so 1.6-rva
    neighbours are the tightest reliable bracket (id order can skip whole
    address ranges when the anchor set is sparse)."""
    i = bisect.bisect_left(ae_rva_sorted, ae_rva)
    if i == 0 or i >= len(ae_rva_sorted):
        return None
    lo_r = anchor_by_ae_rva[ae_rva_sorted[i - 1]]
    hi_r = anchor_by_ae_rva[ae_rva_sorted[i]]
    if lo_r >= hi_r or hi_r - lo_r > BRACKET_WIDE:
        return None
    return lo_r, hi_r


def dice_sim(a_tokens, b_tokens):
    """Multiset Dice coefficient over normalized instruction tokens."""
    if not a_tokens or not b_tokens:
        return 0.0
    ca, cb = Counter(a_tokens), Counter(b_tokens)
    inter = sum((ca & cb).values())
    return 2.0 * inter / (len(a_tokens) + len(b_tokens))


def match_function(pe_ae, ae_text_starts, ae_rev, anchor,
                   anchor_code_sorted, anchor_code, ae_rva_sorted,
                   anchor_code_by_rva, full_ae_rva_sorted,
                   full_anchor_by_rva, pe_se, se_starts, T, ae_rva):
    """Best 1.5.97 candidate for an in-.text AE id, or (None, reason).

    Primary signal is the Dice similarity of the *normalized* instruction
    token streams (address operands become placeholders, so the two builds of
    the same source function match while unrelated functions do not). The
    mapped-callee (cc) and shared-string (sc) signals from the first pass are
    kept as small tie-breakers.

    The candidate pool is the union of four interpolations (full/code x
    id-order/rva-order). The AE->SE address order is only locally monotonic
    (25/701 inversions), so no single pair is reliable; similarity decides
    within the union. Oversized unions fall back to the tightest bracket.
    """
    ref = extract(pe_ae, ae_text_starts, ae_rva)
    if ref is None:
        return None, "unreadable 1.6 target"
    if not ref.tokens:
        return None, "empty token stream"
    brackets = []
    for b in (bracket_for_rva(ae_rva, ae_rva_sorted, anchor_code_by_rva),
              bracket_for(anchor_code_sorted, anchor_code, T),
              bracket_for_rva(ae_rva, full_ae_rva_sorted, full_anchor_by_rva),
              bracket_for(sorted(anchor), anchor, T)):
        if b is not None:
            brackets.append(b)
    if not brackets:
        return None, "no bracket"
    lo_r = min(b[0] for b in brackets)
    hi_r = max(b[1] for b in brackets)
    cand_rvas = [r for r in se_starts if lo_r < r < hi_r]
    if len(cand_rvas) > MAX_CANDS:
        tight = min(brackets, key=lambda b: b[1] - b[0])
        cand_rvas = [r for r in se_starts if tight[0] < r < tight[1]]
    if not cand_rvas:
        return None, "no candidates in bracket"
    if len(cand_rvas) > MAX_CANDS:
        return None, f"bracket too wide ({len(cand_rvas)} candidates)"
    exp_callees = set()
    for c in ref.callees:
        a = ae_rev.get(c)
        if a is not None and a in anchor:
            exp_callees.add(anchor[a])
    ranked = []
    for r in cand_rvas:
        cand = extract(pe_se, se_starts, r)
        if cand is None or not cand.tokens:
            continue
        sim = dice_sim(ref.tokens, cand.tokens)
        if sim <= 0.0:
            continue
        cc = len(cand.callees & exp_callees)
        sc = len(cand.strings & ref.strings)
        ranked.append((sim + 0.03 * cc + 0.02 * sc, sim, r, cc, sc))
    ranked.sort(key=lambda x: -x[0])
    if not ranked:
        return None, "no token overlap"
    best, runner = ranked[0], (ranked[1] if len(ranked) > 1 else None)
    if best[1] < TOKEN_ACCEPT:
        return None, f"low similarity {best[1]:.2f}"
    decisive = best[1] >= 0.95  # near-perfect match wins over any twin
    if runner is not None and not decisive and best[1] - runner[1] < TOKEN_MARGIN:
        return None, (f"ambiguous {best[1]:.2f} vs {runner[1]:.2f} "
                      f"@ {runner[2]:#x}")
    if best[2] not in se_starts:
        return None, "accepted rva not a function start"
    return best[2], f"ok sim={best[1]:.2f} cc={best[3]} sc={best[4]}"


def build_text_disp(pe):
    """Chunked displacement array over .text for the rip-xref scan.

    Returns [(raw_text_offset, np.uint32 array)]; element p is the little-
    endian int32 at raw-text offset p (kept as uint32, wrapped comparison).
    """
    t = pe.text
    raw8 = np.frombuffer(pe.data, dtype=np.uint8, count=t["raw_size"], offset=t["raw"])
    chunks = []
    for start in range(0, max(len(raw8) - 3, 0), RIP_XREF_CHUNK):
        a = raw8[start:start + RIP_XREF_CHUNK + 3]
        if len(a) < 4:
            break
        u32 = a.astype(np.uint32)
        D = (u32[:-3] | (u32[1:-2] << np.uint32(8))
             | (u32[2:-1] << np.uint32(16)) | (u32[3:] << np.uint32(24)))
        chunks.append((start, D))
    return chunks


def find_rip_xrefs(pe, T_rva, chunks):
    """Raw .text rip-relative refs to T_rva, verified with capstone."""
    base_rva = pe.text["va"]
    sites = []
    for cbase, D in chunks:
        n = len(D)
        p = cbase + np.arange(n, dtype=np.int64)
        rhs = (np.int64(T_rva) - base_rva - 4 - p) & 0xFFFFFFFF
        mask = D.astype(np.int64) == rhs
        for rel in np.flatnonzero(mask):
            pp = int(rel)
            for sc in (pp - 3, pp - 2, pp - 4, pp - 5):  # plausible insn starts
                if sc < 0:
                    continue
                site_rva = base_rva + sc
                o = pe.text["raw"] + sc
                if o < 0 or o + 16 > len(pe.data):
                    continue
                md = Cs(CS_ARCH_X86, CS_MODE_64)
                md.detail = True
                for insn in md.disasm(pe.data[o:o + 16], pe.image_base + site_rva):
                    for op in insn.operands:
                        if (op.type == x86_const.X86_OP_MEM
                                and op.mem.base == x86_const.X86_REG_RIP
                                and insn.address + insn.size + op.mem.disp
                                - pe.image_base == T_rva):
                            sites.append(site_rva)
                            break
                    break
    return sorted(set(sites))


def match_data(pe_ae, pe_se, ae_text_starts, ae_rev, anchor, se_starts,
               T, ae_rva, chunks):
    """Ordered rip-ref matching via already-mapped xref functions."""
    xrefs = find_rip_xrefs(pe_ae, ae_rva, chunks)
    if not xrefs:
        return None, "no 1.6 xrefs"
    votes, aligned = Counter(), 0
    for site in xrefs:
        fi = bisect.bisect_right(ae_text_starts, site) - 1
        if fi < 0:
            continue
        fn_rva = ae_text_starts[fi]
        fn_ae = ae_rev.get(fn_rva)
        if fn_ae is None or fn_ae not in anchor:
            continue
        ref16 = extract(pe_ae, ae_text_starts, fn_rva)
        ref97 = extract(pe_se, se_starts, anchor[fn_ae])
        if ref16 is None or ref97 is None:
            continue
        if len(ref16.refs) != len(ref97.refs):
            continue  # code drifted; ordered refs no longer align -> skip
        for k, r in enumerate(ref16.refs):
            if r == ae_rva:
                votes[ref97.refs[k]] += 1
                aligned += 1
    if not votes:
        return None, "no aligned xref function"
    (val, cnt), = votes.most_common(1)
    if len(votes) != 1:
        return None, f"xrefs disagree ({len(votes)} distinct)"
    if cnt < 2:
        return None, f"single xref only ({cnt})"
    return val, f"ok ({cnt} aligned xrefs/{aligned})"


def match_rtti(pe_ae, pe_se, ae_rva):
    """Search the .?AV name near the 1.6 TypeDescriptor in the 1.5.97 binary."""
    data16 = pe_ae.read(ae_rva, 0x40)
    for o in range(0, max(len(data16) - 3, 1)):
        if data16[o:o + 3] == b".?A":
            name = pe_ae.read_cstr(ae_rva + o, 256)
            if name and len(name) >= MIN_STR and all(32 <= b < 127 for b in name):
                hits = []
                start = 0
                while True:
                    i = pe_se.data.find(name, start)
                    if i < 0:
                        break
                    rva = pe_se.rva_of_file(i)
                    if rva is not None:
                        hits.append(rva - o)
                    start = i + 1
                if len(hits) == 1:
                    label = name[:32].decode("latin1", "ignore")
                    return hits[0], f"rtti name ({label})"
                return None, f"rtti name {len(hits)} hits"
    return None, "no .?A name"


def parse_targets(path):
    """Parse the missing-manifest comment lines -> {ae_id: (kind, var)}."""
    out = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line.startswith("# ae="):
            continue
        parts = [p.strip() for p in line[2:].split("|")]
        if len(parts) < 2:
            continue
        try:
            ae_id = int(parts[0].replace("ae=", "").strip())
        except ValueError:
            continue
        out[ae_id] = (parts[1], parts[2] if len(parts) > 2 else "")
    return out


def parse_map(path):
    """Parse 'ae_id 0xRVA' lines (comments allowed) -> {ae_id: rva}."""
    out = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        try:
            out[int(parts[0], 0)] = int(parts[1], 0)
        except ValueError:
            continue
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ae-exe", required=True)
    ap.add_argument("--se-exe", required=True)
    ap.add_argument("--ae-bin", required=True)
    ap.add_argument("--se-bin", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--missing", required=True)
    ap.add_argument("--out-overrides", required=True)
    ap.add_argument("--out-report", required=True)
    args = ap.parse_args()

    t0 = time.time()
    pe_ae, pe_se = Pe(args.ae_exe), Pe(args.se_exe)
    ae_lib, se_lib = parse_bin(args.ae_bin), parse_bin(args.se_bin)
    anchor = parse_map(args.map)
    targets = parse_targets(args.missing)
    print(f"loaded ({time.time() - t0:.1f}s): ae.text@{pe_ae.text['va']:#x} "
          f"se.text@{pe_se.text['va']:#x}, targets {len(targets)}")

    ae_text_starts = sorted(r for r in ae_lib.values() if pe_ae.sec_of(r) is pe_ae.text)
    se_text_starts = sorted(r for r in se_lib.values() if pe_se.sec_of(r) is pe_se.text)
    ae_rev = {r: i for i, r in ae_lib.items() if pe_ae.sec_of(r) is pe_ae.text}
    # code-side anchors: mapped ids whose 1.6 and 1.5.97 addresses both sit
    # in .text (RTTI/data ids live in .data and would poison interpolation)
    anchor_code = {}
    for a, s in anchor.items():
        r = ae_lib.get(a)
        if r is not None and pe_ae.sec_of(r) is pe_ae.text \
                and pe_se.sec_of(s) is pe_se.text:
            anchor_code[a] = s
    anchor_code_sorted = sorted(anchor_code)
    anchor_code_by_rva = {ae_lib[a]: s for a, s in anchor_code.items()}
    ae_rva_sorted = sorted(anchor_code_by_rva)
    full_anchor_by_rva = {}
    for a in anchor:
        r = ae_lib.get(a)
        if r is not None:
            full_anchor_by_rva[r] = anchor[a]
    full_ae_rva_sorted = sorted(full_anchor_by_rva)
    print(f"anchors {len(anchor)}, code anchors {len(anchor_code)}")
    print(f"ae fn starts {len(ae_text_starts)}, se fn starts {len(se_text_starts)}, "
          f"anchors {len(anchor)}")

    chunks = build_text_disp(pe_ae)
    print(f"text-disp chunks built ({time.time() - t0:.1f}s)")

    report, accepted = {}, {}
    for idx, (T, (kind, var)) in enumerate(sorted(targets.items()), 1):
        rec = {"ae": T, "var": var, "kind": kind, "status": "?", "se_rva": None}
        try:
            ae_rva = ae_lib.get(T)
            if ae_rva is None:
                rec["status"] = "no ae lib entry"
            elif pe_ae.sec_of(ae_rva) is pe_ae.text:
                se_rva, status = match_function(
                    pe_ae, ae_text_starts, ae_rev, anchor,
                    anchor_code_sorted, anchor_code, ae_rva_sorted,
                    anchor_code_by_rva, full_ae_rva_sorted,
                    full_anchor_by_rva, pe_se, se_text_starts, T, ae_rva)
                rec["status"], rec["se_rva"] = status, se_rva
            else:
                se_rva, status = match_rtti(pe_ae, pe_se, ae_rva)
                if se_rva is None:
                    se_rva, status = match_data(
                        pe_ae, pe_se, ae_text_starts, ae_rev, anchor,
                        se_text_starts, T, ae_rva, chunks)
                rec["status"], rec["se_rva"] = status, se_rva
        except Exception as e:  # noqa: BLE001 -- one bad target must not kill the run
            rec["status"] = f"error: {e!r}"
        if rec["se_rva"] is not None:
            accepted[T] = rec["se_rva"]
        report[T] = rec
        if idx % 10 == 0 or idx == len(targets):
            Path(args.out_report).write_text(json.dumps(report, indent=2))
            print(f"[{idx}/{len(targets)}] accepted {len(accepted)} "
                  f"({time.time() - t0:.0f}s)")

    se_vals = set(se_lib.values())
    ok = [(T, r) for T, r in sorted(accepted.items()) if r in se_vals]
    bad = [(T, r) for T, r in sorted(accepted.items()) if r not in se_vals]
    if bad:
        print(f"WARNING: {len(bad)} accepted rvas missing from the se lib: {bad}")
    lines = ["# capstone-recovered 1.5.97 offsets (Tools/ida/match_capstone.py)",
             "# ae_id 0xRVA"]
    lines += [f"{T} {r:#x}" for T, r in ok]
    Path(args.out_overrides).write_text("\n".join(lines) + "\n")
    Path(args.out_report).write_text(json.dumps(report, indent=2))
    n_amb = sum(1 for r in report.values()
                if r["status"].startswith(("ambiguous", "single", "xrefs disagree",
                                           "no candidates", "bracket")))
    print(f"accepted {len(ok)}/{len(targets)}, uncertain {n_amb} "
          f"({time.time() - t0:.0f}s total)")
    print(f"overrides -> {args.out_overrides}")


if __name__ == "__main__":
    main()


def extract_tokens(pe, starts, rva):
    """Fast token-only variant of extract() (no strings/callees/refs)."""
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0 or starts[i] != rva:
        return None
    nxt = starts[i + 1] if i + 1 < len(starts) else rva + MAX_FN
    size = min(max(nxt - rva, 1), MAX_FN)
    off = pe.off(rva)
    if off is None:
        return None
    code = pe.data[off:off + size]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    base = pe.image_base
    tokens = []
    n = 0
    for insn in md.disasm(code, base + rva):
        n += 1
        tokens.append(_insn_token(pe, insn, base))
        if n >= MAX_INSNS or insn.address - (base + rva) >= MAX_FN:
            break
    return tokens, size
