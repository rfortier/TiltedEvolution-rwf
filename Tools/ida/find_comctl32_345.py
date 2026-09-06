#!/usr/bin/env python3
"""Find which code references the COMCTL32 ordinal-345 import slot in a PE.

Loads the DLL in IDA, locates the IAT entry for COMCTL32!345, then lists
every code xref to that slot with a short disassembly context.
"""

import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))

import idapro  # noqa: E402
idapro.open_database('/tmp/SkyrimTogetherRuntime_1_5.dll', run_auto_analysis=False)

import ida_auto  # noqa: E402
import idaapi  # noqa: E402
import idautils  # noqa: E402
import ida_name  # noqa: E402
import ida_bytes  # noqa: E402
import ida_hexrays  # noqa: E402

ida_auto.auto_wait()
BASE = idaapi.get_imagebase()
print(f'[dbg] base={BASE:#x}', flush=True)

# find the IAT entry: scan imports for COMCTL32 ordinal 345
target_ea = None
for imp in idautils.Imports():
    # imp = (ea, ordinal, name, library)
    ea, ordinal, name, lib = imp
    if lib and 'COMCTL32' in lib.upper() and ordinal == 345:
        target_ea = ea
        print(f'[+] COMCTL32!345 IAT slot at {ea - BASE:#x} (rva)')
        break

if target_ea is None:
    print('[-] COMCTL32 ordinal 345 import slot not found in IDA imports')
else:
    # list all code xrefs to the IAT slot
    print(f'\n=== xrefs to IAT slot {target_ea - BASE:#x} ===')
    xrefs = list(idautils.XrefsTo(target_ea, 0))
    print(f'total xrefs: {len(xrefs)}')
    for xr in xrefs:
        f = idaapi.get_func(xr.frm)
        fn = f'{ida_name.get_name(f.start_ea) or "sub_" + hex(f.start_ea)}' if f else '?'
        print(f'  xref @ {xr.frm - BASE:#x}  in {fn}')

    # decompile each referencing function to identify the call
    for xr in xrefs:
        f = idaapi.get_func(xr.frm)
        if not f:
            continue
        hf = ida_hexrays.decompile(f.start_ea)
        print(f'\n##### {ida_name.get_name(f.start_ea) or hex(f.start_ea - BASE)} @ {f.start_ea - BASE:#x} #####')
        if hf:
            text = str(hf)
            # print lines mentioning the IAT slot or nearby
            print(text)
        else:
            # fallback: disasm around the xref
            ea = xr.frm
            for _ in range(6):
                print(f'  {ea - BASE:#08x}: {ida_bytes.get_bytes(ea, 16).hex()}  {idaapi.generate_disasm_line(ea, 0)}')
                ea = idaapi.next_head(ea, ea + 0x40)

idapro.close_database()
