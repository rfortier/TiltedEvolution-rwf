# IDAPython: label every AE library function on a 1.6.1170 IDB.
# Run inside IDA with the 1.6.1170 SkyrimSE.exe open (optional second
# instance, for locating the 93 targets side-by-side with the 1.5.97 IDB).

import os
import sys

import idaapi
import idc

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))

from gen_ae_to_se_map import parse_bin  # noqa: E402

BASE = idaapi.get_imagebase()

ae_bin = ida_kernwin.ask_file(False, '*.bin', 'Select the AE library (versionlib-1-6-1170-0.bin)')
if not ae_bin:
    print('[!] no address library selected')
else:
    ae = parse_bin(ae_bin)  # ae id -> 1.6.1170 rva
    print(f'[+] AE library: {len(ae)} entries')
    named = 0
    for ae_id, rva in ae.items():
        if idc.set_name(BASE + rva, f'ae{ae_id}', idc.SN_NOWARN | idc.SN_FORCE):
            named += 1
    print(f'[+] labeled {named} functions as ae<id>')
    print('[+] tip: for each of the 93 targets (Tools/missing_1_5_97_ids.txt),')
    print('    jump to base+<1.6.1170 rva from the AE lib>, study the function,')
    print('    then find its 1.5.97 counterpart (BinDiff or code matching).')
