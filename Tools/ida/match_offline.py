#!/usr/bin/env python3
"""Offline matcher: score the extracted 1.5.97 candidates against the
1.6.1170 target metadata (pure Python, no IDA in memory).

Signals: string-reference matches (strongest), AE-labeled callee matches,
size ratio, prologue bytes16. Merges with the vtable-extracted overrides
(PickUpObject/DropObject) and writes the final overrides file.
"""

import json
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

cands = json.load(open(os.path.join(REPO, 'Tools', 'ida', 'st_candidates_1597.json'), encoding='utf-8'))
meta = json.load(open(os.path.join(REPO, 'Tools', 'ida', 'st_target_meta_1170.json'), encoding='utf-8'))
vt = {'37521': 0x5e6580, '40454': 0x5e6150}  # Actor::PickUpObject/DropObject (vtable-extracted)

threshold = 2.0
overrides = {}
report = []

for ae_s, m in meta.items():
    ae = int(ae_s)
    cl = cands.get(ae_s, [])
    t_strings = set(m.get('strings', []))
    t_callees = set()
    for c in m.get('callees', []):
        if c.startswith('ae'):
            try:
                aid = int(c[2:])
                t_callees.add(aid)
            except ValueError:
                pass
    t_size = m.get('size') or 0

    scored = []
    for c in cl:
        score = 0.0
        for s in c['strings']:
            if s in t_strings:
                score += 3.0
        common = c['callees'] & t_callees
        score += 2.0 * len(common)
        if t_size and 0.5 <= (c['size'] / t_size) <= 2.0:
            score += 1.0
        if c.get('bytes16') == m.get('bytes16'):
            score += 2.0
        scored.append((score, c['ea'], len(common)))
    scored.sort(reverse=True)

    entry = {'ae': ae, 'var': m.get('var', ''), 'cands': len(cl)}
    if scored and scored[0][0] >= threshold and (len(scored) == 1 or scored[0][0] - scored[1][0] >= 1.0):
        overrides[ae] = scored[0][1]
        entry['status'] = f"MATCHED score={scored[0][0]:.1f} ea={scored[0][1]:#x}"
    elif scored:
        entry['status'] = f"ambiguous top={scored[0][0]:.1f} second={scored[1][0]:.1f}"
    else:
        entry['status'] = 'no candidates'
    report.append(entry)

# merge the vtable-recovered ones (highest priority)
for ae_s, rva in vt.items():
    overrides[int(ae_s)] = rva

out = os.path.join(REPO, 'Tools', 'ida', 'st_overrides_final.txt')
with open(out, 'w', encoding='utf-8') as f:
    f.write('# 1.5.97 overrides: binary-diff + vtable + multi-signal matched\n')
    for ae in sorted(overrides):
        f.write(f'{ae} {overrides[ae]:#x}\n')
print(f'[+] final overrides: {len(overrides)} entries -> {out}')
amb = [r for r in report if 'ambiguous' in r.get('status', '')]
print(f'[+] ambiguous: {len(amb)}, no-candidates: {sum(1 for r in report if "no candidates" in r.get("status",""))}')
for r in report:
    if 'MATCHED' in r.get('status', ''):
        print(f"  ✓ ae={r['ae']} {r['var']}: {r['status']}")
