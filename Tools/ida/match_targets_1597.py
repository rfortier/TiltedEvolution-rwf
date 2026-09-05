# Run AFTER export_target_meta_1170.py produced st_target_meta_1170.json.
# Matches the 93 targets against the 1.5.97 IDB (skyrim_1597.i64) using
# multi-signal scoring:
#   - string references (version-independent, strongest)
#   - callees that carry AE labels mapped via the ST map
#   - size ratio vs the 1.6.1170 extent
#   - bytes16 prologue equality
# constrained to the bracket [lo_1597, hi_1597] from the workbench.
# Output: overrides file + a JSON report with per-target scores.
#
# Usage: python3 Tools/ida/match_targets_1597.py [--threshold 2.0]

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
print('[dbg] DB loaded, base:', hex(idaapi.get_imagebase()), flush=True)
BASE = idaapi.get_imagebase()

import argparse  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--threshold', type=float, default=2.0)
ap.add_argument('--meta', default=os.path.join(REPO, 'Tools', 'ida', 'st_target_meta_1170.json'))
ap.add_argument('--workbench', default=os.path.join(REPO, 'Tools', 'ida', 'st_targets_workbench.json'))
ap.add_argument('--overrides-out', default=os.path.join(REPO, 'Tools', 'ida', 'st_overrides.txt'))
ap.add_argument('--report-out', default=os.path.join(REPO, 'Tools', 'ida', 'st_match_report.json'))
args = ap.parse_args()

meta = json.load(open(args.meta, encoding='utf-8'))
work = {w['ae_id']: w for w in json.load(open(args.workbench, encoding='utf-8'))}

# ae -> 1.5.97 rva (mapped anchors)
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


report = []
overrides = {}
cache = {}  # fn_start -> {'size','strings','callees'}
for ae_id, m in meta.items():
    ae_id = int(ae_id)
    print(f'[t] ae={ae_id} {m.get("var","")}', flush=True)
    wb = work.get(ae_id)
    if wb is None or wb.get('lo_1597') is None:
        report.append({'ae': ae_id, 'var': m.get('var', ''), 'status': 'no bracket'})
        continue
    lo, hi = BASE + wb['lo_1597'], BASE + wb['hi_1597']
    tm = m  # target metadata from the 1.6.1170 side
    t_strings = set(tm.get('strings', []))
    t_callees = set()
    for c in tm.get('callees', []):
        if c.startswith('ae'):
            try:
                aid = int(c[2:])
                if aid in anchor:
                    t_callees.add(aid)
            except ValueError:
                pass
    t_size = tm.get('size') or 0

    # candidates: functions in the bracket
    cands = []
    ea = lo
    while ea < hi:
        f = idaapi.get_func(ea)
        if f:
            start, end = f.start_ea, f.end_ea
            nm = ida_name.get_name(start) or ''
            if start in cache:
                cands.append({'ea': start, 'size': cache[start]['size'],
                              'strings': cache[start]['strings'], 'callees': cache[start]['callees']})
                continue
            if not nm.startswith('STmap_ae'):  # skip already-mapped
                size = end - start
                strs = set(strings_in(start, end))
                cl = labeled_callees(start, end)
                cache[start] = {'size': size, 'strings': strs, 'callees': cl}
                cands.append({'ea': start, 'size': size, 'strings': strs, 'callees': cl})
            ea = end
        else:
            ea = idaapi.next_head(ea, hi)
            if ea == idaapi.BADADDR or ea >= hi:
                break

    print(f'[t] ae={ae_id} cands={len(cands)}', flush=True)
    # scoring
    scored = []
    for c in cands:
        score = 0.0
        for s in c['strings'] & t_strings:
            score += 3.0
        score += 2.0 * len(c['callees'] & t_callees)
        if t_size and 0.5 <= (c['size'] / t_size) <= 2.0:
            score += 1.0
        scored.append((score, c['ea']))
    scored.sort(reverse=True)

    if not scored:
        report.append({'ae': ae_id, 'var': tm.get('var', ''), 'status': 'no candidates in bracket'})
        continue

    top_score, top_ea = scored[0]
    second = scored[1][0] if len(scored) > 1 else 0.0
    entry = {'ae': ae_id, 'var': tm.get('var', ''), 'top_ea': hex(top_ea),
             'score': top_score, 'second': second, 'cands': len(scored)}
    if top_score >= args.threshold and (len(scored) == 1 or top_score - second >= 1.0):
        overrides[str(ae_id)] = top_ea - BASE
        entry['status'] = f'MATCHED score={top_score}'
    else:
        entry['status'] = f'ambiguous score={top_score} second={second}'
    report.append(entry)

# write overrides
with open(args.overrides_out, 'w', encoding='utf-8') as f:
    f.write('# multi-signal matched targets (1.5.97)\n')
    for ae_id, rva in sorted(overrides.items(), key=lambda kv: int(kv[0])):
        f.write(f'{ae_id} {rva:#x}\n')

json.dump(report, open(args.report_out, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
matched = sum(1 for r in report if 'MATCHED' in r.get('status', ''))
print(f'[+] matched {matched} / {len(report)} targets; overrides: {args.overrides_out}')
amb = [r for r in report if 'ambiguous' in r.get('status', '')]
for r in amb[:15]:
    print(f"  ambiguous: ae={r['ae']} {r.get('var','')} {r['status']}")
