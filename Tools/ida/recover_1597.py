#!/usr/bin/env python3
"""Recover the last 1.5.97 RVAs that bracket interpolation cannot reach.

match_capstone.py resolves a target by interpolating between the nearest
*mapped* neighbours, so it needs a dense anchor grid and gives up on the ids
whose neighbourhood is empty ("no bracket", "no candidates in bracket") and on
data statics whose referencing functions are all unmapped ("no aligned xref
function"). This tool drops the bracket and identifies the counterpart of an
individual 1.6.1170 function directly, from signals that do not depend on
where the function sits in the image:

  * string literals it references - the literal bytes are identical in both
    builds, so the same literal in 1.5.97 .rdata leads back to the same
    function
  * its position in the call graph relative to functions already mapped
  * normalized instruction-token similarity, as the tie breaker

With a function located, a data static referenced from it follows: the ordered
rip-relative references of the two function bodies are aligned against each
other and the slot holding the target on the 1.6 side names its 1.5.97
counterpart.

Usage (from the repo root):

    python3 Tools/ida/recover_1597.py \
        --ae-exe "<1.6.1170 SkyrimSE.exe>" \
        --se-exe "<1.5.97 SkyrimSE.exe>" \
        --ae-bin GameFiles/Skyrim/SKSE/Plugins/versionlib-1-6-1170-0.bin \
        --se-bin GameFiles/Skyrim/SKSE/Plugins/version-1-5-97-0.bin \
        --map    GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map \
        --missing Tools/missing_1_5_97_ids.txt \
        --out-overrides Tools/ida/st_overrides_graph.txt \
        --out-report    Tools/ida/st_graph_report.json
"""

import argparse
import bisect
import difflib
import json
import os
import sys
import time
from collections import Counter, defaultdict

import numpy as np
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone import x86_const

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_capstone import (Pe, dice_sim, extract, parse_bin,  # noqa: E402
                            parse_map, parse_targets)

MIN_STR = 5
MAX_FN = 0x4000


class Image:
    """A binary plus the indexes the matching needs, all built from .text."""

    def __init__(self, exe, lib_path, label):
        self.label = label
        self.pe = Pe(exe)
        self.lib = parse_bin(lib_path)
        self.rev = {r: i for i, r in self.lib.items()
                    if self.pe.sec_of(r) is self.pe.text}
        # every id, not just the code ones: the data anchors are what let a
        # literal-free function be identified by the globals it touches
        self.rev_all = {r: i for i, r in self.lib.items()}
        self.starts = np.array(sorted(self.rev), dtype=np.int64)
        self.starts_list = self.starts.tolist()

        t = self.pe.text
        self.text_rva = t["va"]
        raw = np.frombuffer(self.pe.data, dtype=np.uint8,
                            count=t["raw_size"], offset=t["raw"])
        u = raw.astype(np.uint32)
        self.raw = raw
        # little-endian int32 at every byte position of .text
        self.dword = (u[:-3] | (u[1:-2] << np.uint32(8))
                      | (u[2:-1] << np.uint32(16)) | (u[3:] << np.uint32(24)))
        self._build_calls()
        self._fn_cache = {}
        self._xref_cache = {}
        self._str_cache = {}
        self._lit_cache = {}

    # ---------------------------------------------------------------- calls
    def _build_calls(self):
        """Direct `call rel32` edges whose target is a known function start."""
        d = self.dword
        e8 = np.flatnonzero(self.raw[:len(d) - 1] == 0xE8)
        rel = d[e8 + 1].astype(np.int64)
        rel = np.where(rel >= 0x80000000, rel - 0x100000000, rel)
        tgt = self.text_rva + e8.astype(np.int64) + 5 + rel
        idx = np.clip(np.searchsorted(self.starts, tgt), 0, len(self.starts) - 1)
        hit = self.starts[idx] == tgt
        site = self.text_rva + e8[hit].astype(np.int64)
        callee = tgt[hit]
        fi = np.clip(np.searchsorted(self.starts, site, side="right") - 1,
                     0, len(self.starts) - 1)
        caller = self.starts[fi]
        self.callees_of = defaultdict(set)
        self.callers_of = defaultdict(set)
        for a, b in zip(caller.tolist(), callee.tolist()):
            if a != b:
                self.callees_of[a].add(b)
                self.callers_of[b].add(a)

    # ------------------------------------------------------------ functions
    def fn_of(self, rva):
        i = bisect.bisect_right(self.starts_list, rva) - 1
        return self.starts_list[i] if i >= 0 else None

    def features(self, fn):
        f = self._fn_cache.get(fn)
        if f is None:
            f = extract(self.pe, self.starts_list, fn)
            self._fn_cache[fn] = f
        return f

    def body(self, fn):
        """Ordered rip-relative references: [(site, target, mnemonic, shape)]."""
        i = bisect.bisect_right(self.starts_list, fn) - 1
        if i < 0 or self.starts_list[i] != fn:
            return None
        nxt = (self.starts_list[i + 1] if i + 1 < len(self.starts_list)
               else fn + MAX_FN)
        size = min(max(nxt - fn, 1), MAX_FN)
        off = self.pe.off(fn)
        if off is None:
            return None
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        md.detail = True
        base = self.pe.image_base
        out = []
        for insn in md.disasm(self.pe.data[off:off + size], base + fn):
            for op in insn.operands:
                if (op.type == x86_const.X86_OP_MEM
                        and op.mem.base == x86_const.X86_REG_RIP):
                    t = insn.address + insn.size + op.mem.disp - base
                    shape = f"{insn.mnemonic}/{len(insn.operands)}"
                    out.append((insn.address - base, t, insn.mnemonic, shape))
        return out

    # ----------------------------------------------------------- rip xrefs
    def _verify_rip(self, pos, want_lo, want_hi):
        """Disassemble around a candidate disp32 position, return (site, target)."""
        for back in (3, 2, 4, 5, 6, 7):
            sc = pos - back
            if sc < 0:
                continue
            o = self.pe.text["raw"] + sc
            if o + 16 > len(self.pe.data):
                continue
            md = Cs(CS_ARCH_X86, CS_MODE_64)
            md.detail = True
            for insn in md.disasm(self.pe.data[o:o + 16],
                                  self.pe.image_base + self.text_rva + sc):
                for op in insn.operands:
                    if (op.type == x86_const.X86_OP_MEM
                            and op.mem.base == x86_const.X86_REG_RIP):
                        t = (insn.address + insn.size + op.mem.disp
                             - self.pe.image_base)
                        if want_lo <= t < want_hi:
                            return self.text_rva + sc, t
                break
        return None

    def rip_xrefs_in(self, lo, hi):
        """Every rip-relative reference into [lo, hi) -> [(site, target)].

        Walks .text in chunks: the int64 temporaries a whole-image pass needs
        add up to roughly a gigabyte per call, and both images stay resident.
        """
        cached = self._xref_cache.get((lo, hi))
        if cached is not None:
            return cached
        out = []
        step = 1 << 22
        for start in range(0, len(self.dword), step):
            d = self.dword[start:start + step].astype(np.int64)
            np.subtract(d, 0x100000000, out=d, where=d >= 0x80000000)
            d += self.text_rva + 4 + start
            d += np.arange(len(d), dtype=np.int64)
            for pos in (np.flatnonzero((d >= lo) & (d < hi)) + start).tolist():
                got = self._verify_rip(pos, lo, hi)
                if got:
                    out.append(got)
        out = sorted(set(out))
        self._xref_cache[(lo, hi)] = out
        return out

    def string_users(self, text):
        """Functions referencing the given literal -> set of function starts."""
        if text in self._str_cache:
            return self._str_cache[text]
        users = set()
        for rva in self.find_literal(text):
            for site, _ in self.rip_xrefs_in(rva, rva + 1):
                fn = self.fn_of(site)
                if fn is not None:
                    users.add(fn)
        self._str_cache[text] = users
        return users

    def find_literal(self, text):
        """RVAs in the read-only data where this exact C string lives."""
        if text in self._lit_cache:
            return self._lit_cache[text]
        needle = text.encode("latin1") + b"\0"
        hits = []
        for sec in self.pe.sections:
            if sec["name"] not in (".rdata", ".data"):
                continue
            blob = self.pe.data[sec["raw"]:sec["raw"] + sec["raw_size"]]
            pos = blob.find(needle)
            while pos != -1 and len(hits) < 64:
                # a literal starts on a boundary, never mid-string
                if pos == 0 or blob[pos - 1] == 0:
                    hits.append(sec["va"] + pos)
                pos = blob.find(needle, pos + 1)
        self._lit_cache[text] = hits
        return hits


# --------------------------------------------------------------- locating
_LOCATE_CACHE = {}


def locate(ae, se, anchor, ae_fn, want=None):
    """Find the 1.5.97 counterpart of a 1.6.1170 function.

    Returns (se_fn, reason) or (None, reason). `want` restricts the answer to
    a candidate set when the caller already has one.
    """
    aeid = ae.rev.get(ae_fn)
    if aeid is not None and aeid in anchor:
        return anchor[aeid], "already mapped"
    if want is None and ae_fn in _LOCATE_CACHE:
        return _LOCATE_CACHE[ae_fn]

    f_ae = ae.features(ae_fn)
    if f_ae is None:
        return None, "unreadable 1.6 function"

    cands = None

    def narrow(new, label):
        nonlocal cands, notes
        if not new:
            return
        notes.append(f"{label}={len(new)}")
        cands = set(new) if cands is None else (cands & set(new)) or cands

    notes = []
    # literal references: identical bytes in both builds, so the strongest
    # signal available and completely position independent
    for s in sorted(f_ae.strings):
        if len(s) >= MIN_STR:
            narrow(se.string_users(s), f"str:{s[:18]}")

    # call graph: a mapped callee constrains the counterpart to its callers
    mapped_callees = {anchor[ae.rev[c]] for c in f_ae.callees
                      if c in ae.rev and ae.rev[c] in anchor}
    for c in mapped_callees:
        narrow(se.callers_of.get(c, set()), "callers-of-callee")
    mapped_callers = {anchor[ae.rev[c]] for c in ae.callers_of.get(ae_fn, ())
                      if c in ae.rev and ae.rev[c] in anchor}
    for c in mapped_callers:
        narrow(se.callees_of.get(c, set()), "callees-of-caller")

    # globals it touches: singletons, RTTI descriptors and other statics that
    # are already mapped. Most of the anchor grid is data rather than code, so
    # for a function with no literals and no mapped call-graph neighbours this
    # is usually the only signal left.
    body = ae.body(ae_fn) or []
    seen = set()
    for _, target, _, _ in body:
        aeid = ae.rev_all.get(target)
        if aeid is None or aeid not in anchor or target in seen:
            continue
        seen.add(target)
        se_target = anchor[aeid]
        users = set()
        for site, _ in se.rip_xrefs_in(se_target, se_target + 1):
            fn = se.fn_of(site)
            if fn is not None:
                users.add(fn)
        narrow(users, f"uses:{target:#x}")

    if want:
        cands = set(want) if cands is None else (cands & set(want))
    if not cands:
        return None, "no candidate; " + ",".join(notes)

    scored = []
    for c in cands:
        f_se = se.features(c)
        if f_se is None:
            continue
        sim = dice_sim(f_ae.tokens, f_se.tokens)
        shared = len(f_ae.strings & f_se.strings)
        scored.append((sim + 0.05 * shared, sim, c))
    if not scored:
        return None, "candidates unreadable"
    scored.sort(reverse=True)
    best, sim, fn = scored[0]
    if len(scored) > 1 and scored[1][0] > best - 0.05:
        out = None, (f"ambiguous {sim:.2f} vs {scored[1][1]:.2f} "
                     f"({len(scored)} candidates)")
    elif sim < 0.55:
        out = None, f"low similarity {sim:.2f} ({len(scored)} candidates)"
    else:
        out = fn, f"sim={sim:.2f} cands={len(scored)} [{','.join(notes[:3])}]"
    if want is None:
        _LOCATE_CACHE[ae_fn] = out
    return out


def align_refs(ae_body, se_body):
    """Map 1.6 rip-ref indices to 1.5.97 ones by aligning the ref sequences.

    Aligning on the referencing mnemonic (not on the addresses, which differ
    by construction) tolerates the codegen drift that makes a plain
    "same number of refs" test fail on almost every function.
    """
    a = [m for _, _, m, _ in ae_body]
    b = [m for _, _, m, _ in se_body]
    out = {}
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(a=a, b=b).get_opcodes():
        if op == "equal":
            for k in range(i2 - i1):
                out[i1 + k] = j1 + k
    return out


def match_data(ae, se, anchor, ae_rva, log):
    """Resolve a data static through the functions that reference it."""
    sites = ae.rip_xrefs_in(ae_rva, ae_rva + 1)
    if not sites:
        return None, "no 1.6 xref reaches this address"

    votes, tried, located = Counter(), 0, 0
    for site, _ in sites:
        ae_fn = ae.fn_of(site)
        if ae_fn is None:
            continue
        tried += 1
        se_fn, why = locate(ae, se, anchor, ae_fn)
        if se_fn is None:
            log.append(f"    xref fn {ae_fn:#x}: {why}")
            continue
        located += 1
        ae_body, se_body = ae.body(ae_fn), se.body(se_fn)
        if not ae_body or not se_body:
            continue
        amap = align_refs(ae_body, se_body)
        for i, (s, t, _, _) in enumerate(ae_body):
            if t == ae_rva and i in amap:
                target = se_body[amap[i]][1]
                if se.pe.sec_of(target) is not None:
                    votes[target] += 1
        log.append(f"    xref fn {ae_fn:#x} -> {se_fn:#x} ({why})")
    if not votes:
        return None, f"no aligned reference ({tried} xref fns, {located} located)"
    (val, cnt), = votes.most_common(1)
    if len(votes) > 1 and votes.most_common(2)[1][1] == cnt:
        return None, f"references disagree ({dict(votes)})"
    return val, f"{cnt}/{sum(votes.values())} aligned refs, {located} fns located"


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
    ap.add_argument("--only", help="comma separated AE ids to process")
    args = ap.parse_args()

    t0 = time.time()
    ae = Image(args.ae_exe, args.ae_bin, "1.6.1170")
    se = Image(args.se_exe, args.se_bin, "1.5.97")
    anchor = parse_map(args.map)
    targets = parse_targets(args.missing)
    if args.only:
        keep = {int(x) for x in args.only.split(",")}
        targets = {k: v for k, v in targets.items() if k in keep}
    print(f"loaded in {time.time() - t0:.1f}s: "
          f"1.6 fns {len(ae.starts)}, 1.5.97 fns {len(se.starts)}, "
          f"anchors {len(anchor)}, targets {len(targets)}")

    report, accepted = {}, {}
    for n, (tid, (kind, var)) in enumerate(sorted(targets.items()), 1):
        log = []
        rec = {"ae": tid, "var": var, "kind": kind, "status": "?",
               "se_rva": None, "log": log}
        ae_rva = ae.lib.get(tid)
        try:
            if ae_rva is None:
                rec["status"] = "no 1.6 address library entry"
            elif ae.pe.sec_of(ae_rva) is ae.pe.text:
                fn, why = locate(ae, se, anchor, ae_rva)
                rec["se_rva"], rec["status"] = fn, why
            else:
                val, why = match_data(ae, se, anchor, ae_rva, log)
                rec["se_rva"], rec["status"] = val, why
        except Exception as e:  # noqa: BLE001 - one target must not kill the run
            rec["status"] = f"error: {e!r}"
        if rec["se_rva"] is not None:
            accepted[tid] = rec["se_rva"]
        report[tid] = rec
        mark = f"{rec['se_rva']:#x}" if rec["se_rva"] else "-"
        print(f"[{n}/{len(targets)}] {tid:>7} {var[:26]:<26} {mark:>10}  "
              f"{rec['status']} ({time.time() - t0:.0f}s)", flush=True)
        for line in log:
            print(line, flush=True)

    se_vals = set(se.lib.values())
    lines = ["# graph-recovered 1.5.97 offsets (Tools/ida/recover_1597.py)",
             "# ae_id 0xRVA"]
    for tid, rva in sorted(accepted.items()):
        flag = "" if rva in se_vals else "   # not in the 1.5.97 address library"
        lines.append(f"{tid} {rva:#x}{flag}")
    open(args.out_overrides, "w").write("\n".join(lines) + "\n")
    json.dump(report, open(args.out_report, "w"), indent=2)
    print(f"\naccepted {len(accepted)}/{len(targets)}, "
          f"{sum(1 for r in accepted.values() if r in se_vals)} of them present "
          f"in the 1.5.97 address library")
    print(f"overrides -> {args.out_overrides}")


if __name__ == "__main__":
    main()
