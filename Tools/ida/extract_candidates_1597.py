# Run on the 1.5.97 IDB (via idalib, low-memory single pass).
# For each of the 93 target brackets, enumerate candidate functions and
# extract their matching signals:
#   - size, referenced strings, callees carrying AE labels (via STmap names)
# Output: Tools/ida/st_candidates_1597.json  (consumed by the offline matcher)
#
# Usage: python3 Tools/ida/extract_candidates_1597.py

import os
import sys
import json

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))

import idapro  # noqa: E402

IDB = '/tmp/work_1597.i64'
idapro.open_database(IDB, run_auto_analysis=False)

import ida_auto  # noqa: E402
import idaapi  # noqa: E402
import idautils  # noqa: E402
import ida_bytes  # noqa: E402
import ida_name  # noqa: E402

ida_auto.auto_wait()
BASE = idaapi.get_imagebase()

work = json.load(open(os.path.join(REPO, 'Tools', 'ida', 'st_targets_workbench.json'),
                      encoding='utf-8'))

# ae -> 1.5.97 rva (mapped anchors, for callee-label resolution)
anchor = {}
for line in open(os.path.join(REPO, 'GameFiles', 'Skyrim', 'SKSE', 'Plugins',
                             'versionlib-ae-to-se-1-5-97-0.map'), encoding='utf-8'):
    if line.startswith('#'):
        continue
    p = line.split()
    if len(p) >= 2:
        anchor[int(p[0])] = int(p[1], 0)


def strings_in(start, end):
    out = []
    ea = start
    while ea < end:
        for dr in idautils.DataRefsFrom(ea):
            s = ida_bytes.get_strlit_contents(dr, -1, 0)
            if s and len(s) >= 4 and all(32 <= b < 127 for b in s[:16]):
                try:
                    out.append(s.decode('ascii'))
                except Exception:
                    pass
        ea = idaapi.next_head(ea, end)
    return out


def labeled_callees(f_start, f_end):
    out = set()
    x = f_start
    while x < f_end:
        for xr in idautils.XrefsFrom(x, 0):
            nm = ida_name.get_name(xr.to)
            if nm and nm.startswith('STmap_ae'):
                try:
                    out.add(int(nm[8:]))
                except ValueError:
                    pass
        x = idaapi.next_head(x, f_end)
    return out


out_path = os.path.join(REPO, 'Tools', 'ida', 'st_candidates_1597.json')
all_cands = {}
for w in work:
    ae = w['ae_id']
    lo, hi = w.get('lo_1597'), w.get('hi_1597')
    if lo is None or hi is None or hi <= lo:
        all_cands[str(ae)] = []
        continue
    lo_ea, hi_ea = BASE + lo, BASE + hi
    cands = []
    ea = lo_ea
    while ea < hi_ea:
        f = idaapi.get_func(ea)
        if f:
            start, end = f.start_ea, f.end_ea
            nm = ida_name.get_name(start) or ''
            entry = {'ea': start - BASE, 'size': end - start,
                     'strings': strings_in(start, end),
                     'callees': sorted(labeled_callees(start, end))}
            if not nm.startswith('STmap_ae'):
                cands.append(entry)
            ea = end
        else:
            nxt = idaapi.next_head(ea, hi_ea)
            if nxt == idaapi.BADADDR or nxt <= ea:
                break
            ea = nxt
    all_cands[str(ae)] = cands
    print(f'[+] ae={ae}: {len(cands)} candidates in bracket', flush=True)

json.dump(all_cands, open(out_path, 'w', encoding='utf-8'), indent=1)
total = sum(len(v) for v in all_cands.values())
print(f'[+] written {out_path}: {total} candidates across {len(all_cands)} brackets')
idapro.close_database()
