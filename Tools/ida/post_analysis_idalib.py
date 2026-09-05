# Run AFTER the idalib auto-analysis of the 1.5.97 exe completes.
# Applies: (1) 3603 known function labels from the ST map,
#          (2) saves the labeled .i64 for the GUI session,
#          (3) writes the 93-target workbench (each target's identity +
#              the mapped functions that bracket it in 1.5.97 address space).
#
# Usage: python3 Tools/ida/post_analysis_idalib.py

import os
import sys
import json

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))

import idapro  # noqa: E402

IDB = '/home/sj/桌面/qwqw/ida/skyrim_1597.i64'
idapro.open_database(IDB, run_auto_analysis=False)

import ida_auto  # noqa: E402
import ida_funcs  # noqa: E402
import ida_name  # noqa: E402
import idaapi  # noqa: E402
import idautils  # noqa: E402

ida_auto.auto_wait()

BASE = idaapi.get_imagebase()

# ---- 1. apply the known labels ----
map_path = os.path.join(REPO, 'GameFiles', 'Skyrim', 'SKSE', 'Plugins',
                        'versionlib-ae-to-se-1-5-97-0.map')
known = {}
for line in open(map_path, encoding='utf-8'):
    if line.startswith('#'):
        continue
    parts = line.split()
    if len(parts) >= 2:
        known[int(parts[0])] = int(parts[1], 0)

named = 0
for ae_id, rva in known.items():
    if ida_name.set_name(BASE + rva, f'STmap_ae{ae_id}', ida_name.SN_NOWARN | ida_name.SN_FORCE):
        named += 1
print(f'[+] labeled {named}/{len(known)} known functions')

# ---- 2. the 93 targets: bracket each between known neighbors ----
manifest = os.path.join(REPO, 'Tools', 'missing_1_5_97_ids.txt')
ae_rvas = sorted(known.items(), key=lambda kv: kv[1])  # sort by 1.5.97 rva

targets = []
for line in open(manifest, encoding='utf-8'):
    if not line.startswith('# ae='):
        continue
    p = line.split('|')
    ae = int(p[0].split('=')[1])
    var = p[2].strip()
    loc = p[3].strip() if len(p) > 3 else ''
    targets.append((ae, var, loc))

# AE 1.6.1170 offsets for size hints
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))
from gen_ae_to_se_map import parse_bin  # noqa: E402
AE_CANDIDATES = [
    '/home/sj/桌面/qwqw/addrlib/versionlib-1-6-1170-0.bin',
    os.path.join(REPO, '..', 'addrlib', 'versionlib-1-6-1170-0.bin'),
]
ae_lib = {}
for c in AE_CANDIDATES:
    if os.path.exists(c):
        ae_lib = parse_bin(c)
        break

workbench = []
for ae, var, loc in targets:
    # bracket in 1.5.97 address space using known labels
    below = max((r for r in known.values() if r < (BASE + 0x100000)), default=None)
    lo = hi = None
    prev_r, next_r = None, None
    # find the largest known se-rva below and smallest above using AE neighbors
    if ae_lib and ae in ae_lib:
        ae_rva = ae_lib[ae]
        # find nearest known ae-id below and above with the same ae-rva ordering
        below_ae = [a for a in known if a < ae]
        above_ae = [a for a in known if a > ae]
        lo = known[max(below_ae)] if below_ae else None
        hi = known[min(above_ae)] if above_ae else None
    workbench.append({
        'ae_id': ae, 'var': var, 'callsite': loc,
        'ae_rva_1170': ae_lib.get(ae),
        'lo_1597': lo, 'hi_1597': hi,
    })

out = os.path.join(REPO, 'Tools', 'ida', 'st_targets_workbench.json')
json.dump(workbench, open(out, 'w', encoding='utf-8'), indent=1)
print(f'[+] workbench written: {out} ({len(workbench)} targets)')

# ---- 3. save the labeled database for the GUI session ----
import ida_loader  # noqa: E402
ida_loader.save_database(IDB, 0)
print('[+] labeled database saved')
idapro.close_database()
print('[+] done. open the .i64 in IDA GUI, hunt the 93 targets,')
print('    rename found ones to STtarget_ae<id>, then run 3_export_targets.py')
