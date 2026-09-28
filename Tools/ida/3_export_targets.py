# IDAPython: export the identified 1.5.97 targets.
# Run inside IDA on the 1.5.97 IDB AFTER you renamed found functions to
# STtarget_ae<id> (e.g. STtarget_ae37521 for Actor::PickUpObject).
# Writes an overrides file ready for gen_se_map_from_history.py.

import os
import re

import idaapi
import idautils

BASE = idaapi.get_imagebase()

out_path = ida_kernwin.ask_file(True, 'st_overrides.txt',
                                'Save overrides file to...')
if not out_path:
    out_path = os.path.join(os.path.dirname(idc.get_idb_path()), 'st_overrides.txt')

found = 0
with open(out_path, 'w', encoding='utf-8') as f:
    f.write('# identified 1.5.97 targets (from the IDA session)\n')
    for ea, name in ((ea, idaapi.get_name(ea)) for ea in idautils.Functions()):
        m = re.match(r'^STtarget_ae(\d+)$', name or '')
        if m:
            rva = ea - BASE
            f.write(f'{m.group(1)} {rva:#x}\n')
            print(f'[+] STtarget_ae{m.group(1)} -> {rva:#x}')
            found += 1

print(f'[+] wrote {found} overrides to {out_path}')
print('[+] now run:')
print('    python3 Tools/Scripts/gen_se_map_from_history.py \\')
print('      --se-bin version-1-5-97-0.bin --commonlib <CommonLibSSE-NG> \\')
print('      --se-bins-dir <SKSE/Plugins dir> \\')
print(f'      --overrides {out_path} \\')
print('      --out GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map')
