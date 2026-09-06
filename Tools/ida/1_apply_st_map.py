# IDAPython: apply the known ST map labels on the 1.5.97 IDB and print
# the 93-target checklist. Run inside IDA with the 1.5.97 SkyrimSE.exe open.
#
# Paths: edit the constants below OR just run - they fall back to asking.

import os
import sys

import idaapi
import idautils
import idc

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))

from gen_ae_to_se_map import parse_bin  # noqa: E402

BASE = idaapi.get_imagebase()


def ask_or(path, title, patterns):
    if path and os.path.exists(path):
        return path
    for pat in patterns:
        p = os.path.join(REPO, pat)
        if os.path.exists(p):
            return p
    return ida_kernwin.ask_file(False, title, title)


def parse_se_format1(path):
    # format 1/2 address library (same parser as Tools/Scripts)
    data = open(path, 'rb').read()
    fmt = int.from_bytes(data[0:4], 'little')
    o = 4
    ver = int.from_bytes(data[o:o+16], 'little'); o += 16
    if fmt == 2:
        n = int.from_bytes(data[o:o+4], 'little'); o += 4
        if 0 < n < 0x10000:
            o += n
    ps = int.from_bytes(data[o:o+4], 'little'); o += 4
    cnt = int.from_bytes(data[o:o+4], 'little'); o += 4
    out = {}
    pvid = poff = 0
    for _ in range(cnt):
        t = data[o]; o += 1
        lo, hi = t & 0xF, t >> 4

        def rd(n):
            nonlocal o
            v = int.from_bytes(data[o:o+n], 'little'); o += n
            return v

        if lo == 0: id_ = rd(8)
        elif lo == 1: id_ = pvid + 1
        elif lo == 2: id_ = pvid + rd(1)
        elif lo == 3: id_ = pvid - rd(1)
        elif lo == 4: id_ = pvid + rd(2)
        elif lo == 5: id_ = pvid - rd(2)
        elif lo == 6: id_ = rd(2)
        elif lo == 7: id_ = rd(4)
        else: raise ValueError('bad id encoding')

        tp = (poff // ps) if (fmt == 2 and (hi & 8)) else poff
        h = hi & 7
        if h == 0: off = rd(8)
        elif h == 1: off = tp + 1
        elif h == 2: off = tp + rd(1)
        elif h == 3: off = tp - rd(1)
        elif h == 4: off = tp + rd(2)
        elif h == 5: off = tp - rd(2)
        elif h == 6: off = rd(2)
        elif h == 7: off = rd(4)
        else: raise ValueError('bad offset encoding')

        if fmt == 2 and (hi & 8):
            off *= ps
        out[id_] = off
        pvid, poff = id_, off
    return out


def main():
    se_bin = ask_or(None, 'select version-1-5-97-0.bin',
                    ['../qwqw/../', 'Tools/'])
    se_bin = ida_kernwin.ask_file(False, '*.bin', 'Select the 1.5.97 address library (version-1-5-97-0.bin)')
    if not se_bin:
        print('[!] no address library selected')
        return
    map_path = os.path.join(REPO, 'GameFiles', 'Skyrim', 'SKSE', 'Plugins',
                            'versionlib-ae-to-se-1-5-97-0.map')

    se = parse_se_format1(se_bin)          # se id -> rva
    print(f'[+] SE library: {len(se)} entries')

    # the known ST mappings (ae id -> 1.5.97 rva)
    known = {}
    for line in open(map_path, encoding='utf-8'):
        if line.startswith('#'):
            continue
        parts = line.split()
        if len(parts) >= 2:
            known[int(parts[0])] = int(parts[1], 0)
    print(f'[+] known ST mappings: {len(known)}')

    named = 0
    for ae_id, rva in known.items():
        ea = BASE + rva
        if idc.set_name(ea, f'STmap_ae{ae_id}', idc.SN_NOWARN | idc.SN_FORCE):
            named += 1
    print(f'[+] labeled {named} functions as STmap_ae<id>')

    # the 93-target checklist (identities from Tools/missing_1_5_97_ids.txt)
    targets = []
    mf = os.path.join(REPO, 'Tools', 'missing_1_5_97_ids.txt')
    for line in open(mf, encoding='utf-8'):
        m = None
        if line.startswith('# ae='):
            parts = line.split('|')
            ae = parts[0].split('=')[1].strip()
            kind = parts[1].strip()
            var = parts[2].strip()
            loc = parts[3].strip() if len(parts) > 3 else ''
            targets.append((int(ae), kind, var, loc))
    print(f'\n[+] ===== {len(targets)} TARGETS TO FIND IN THIS IDB =====')
    print('[+] for each: locate the function (see Tools/missing_1_5_97_ids.txt'),
    print('[+] identities), verify, rename it to:  STtarget_ae<id>')
    print('[+] then run 3_export_targets.py\n')
    for ae, kind, var, loc in targets:
        print(f'    ae={ae:<7} {var:<32} {loc}')

    out = os.path.join(os.path.dirname(idc.get_idb_path()), 'st_targets_checklist.txt')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('ae_id | kind | var | callsite\n')
        for ae, kind, var, loc in targets:
            f.write(f'{ae} | {kind} | {var} | {loc}\n')
    print(f'[+] checklist written to {out}')


main()
