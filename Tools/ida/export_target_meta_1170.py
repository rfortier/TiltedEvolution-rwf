# Run AFTER 1.6.1170 analysis completes (skyrim_1170.i64 exists).
# Exports, for each of the 93 missing targets, the metadata needed to
# identify its 1.5.97 counterpart:
#   - 1.6.1170 decompiled pseudocode (hexrays)
#   - function bytes (hex) + size
#   - referenced string constants (version-independent matching signal)
#   - callers/callees that carry STmap labels (mapped anchors)
# Output: Tools/ida/st_target_meta_1170.json

import os
import sys
import json

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))

import idapro  # noqa: E402

IDB = '/home/sj/桌面/qwqw/ida/skyrim_1170.i64'
idapro.open_database(IDB, run_auto_analysis=False)

import ida_auto  # noqa: E402
import idaapi  # noqa: E402
import idautils  # noqa: E402
import ida_funcs  # noqa: E402
import ida_bytes  # noqa: E402
import ida_xref  # noqa: E402
import ida_name  # noqa: E402

ida_auto.auto_wait()
BASE = idaapi.get_imagebase()

# AE 标签已在阶段 1 应用 (ae<id>) — 直接按名定位
import idc  # noqa: E402

try:
    import ida_hexrays
    ida_hexrays.init_hexrays_plugin()
    HAVE_HC = True
except Exception:
    HAVE_HC = False
print('[*] hexrays:', HAVE_HC)

# AE lib: ae_id -> rva (用于 id 与 next 边界)
sys.path.insert(0, os.path.join(REPO, 'Tools', 'Scripts'))
from gen_ae_to_se_map import parse_bin  # noqa: E402
ae = parse_bin('/tmp/addrlib/SKSE/Plugins/versionlib-1-6-1170-0.bin')
ae_sorted = sorted(ae.items(), key=lambda kv: kv[1])
ae_pos = {a: i for i, (a, r) in enumerate(ae_sorted)}


def func_extent(ea):
    f = ida_funcs.get_func(ea)
    if f:
        return f.start_ea, f.end_ea
    return ea, ea + 256


def strings_in(start, end):
    """可打印字符串常量 (从函数字节范围内引用的 .rdata 字符串)"""
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
    return out[:24]


def labeled_xrefs(ea, want_from):
    """带 STmap/ae 标签的调用者/被调用者"""
    out = []
    if want_from:
        it = idautils.XrefsTo(ea)
    else:
        it = idautils.XrefsFrom(ea, 0)
    for x in it:
        nm = ida_name.get_name(x.frm if want_from else x.to)
        if nm and (nm.startswith('STmap_ae') or nm.startswith('ae')):
            out.append(nm)
    return out[:20]


manifest = os.path.join(REPO, 'Tools', 'missing_1_5_97_ids.txt')
targets = []
for line in open(manifest, encoding='utf-8'):
    if not line.startswith('# ae='):
        continue
    p = line.split('|')
    targets.append((int(p[0].split('=')[1]), p[2].strip(), p[3].strip() if len(p) > 3 else ''))

meta = {}
for ae_id, var, loc in targets:
    ea = idc.get_name_ea_simple(f'ae{ae_id}')
    if ea == idaapi.BADADDR:
        meta[ae_id] = {'var': var, 'error': 'ae label not found'}
        continue
    f_start, f_end = func_extent(ea)
    entry = {
        'var': var, 'callsite': loc,
        'ea_1170': hex(ea), 'size': f_end - f_start,
        'decompile': None, 'strings': [], 'callers': [], 'callees': [],
        'bytes16': ida_bytes.get_bytes(ea, 16).hex() if ida_bytes.get_bytes(ea, 16) else None,
    }
    if HAVE_HC:
        try:
            cf = ida_hexrays.decompile(ea)
            entry['decompile'] = str(cf)[:4000]
        except Exception as e:
            entry['decompile'] = f'<decompile failed: {e}>'
    entry['strings'] = strings_in(f_start, f_end)
    # 带标签的调用者 (谁调用它) — 通过函数内的 call 目标反查太贵, 改为:
    # 它调用的带标签函数 (callees) + 引用它的带标签函数
    entry['callers'] = [n for n in labeled_xrefs(ea, True)][:12]
    # callees: 函数内 call 的目标名 (含 ae 标签)
    cl = []
    x = f_start
    while x < f_end:
        for xr in idautils.XrefsFrom(x, 0):
            if xr.type in (17, 18, 19, 20, 21):  # call 类
                nm = ida_name.get_name(xr.to)
                if nm and (nm.startswith('ae') or nm.startswith('STmap_ae')):
                    cl.append(nm)
        x = idaapi.next_head(x, f_end)
    entry['callees'] = cl[:16]

    meta[ae_id] = entry
    print(f'[+] ae={ae_id} {var}: size={f_end-f_start} strings={len(entry["strings"])} '
          f'callers={len(entry["callers"])} decompile={"yes" if entry["decompile"] else "no"}')

out = os.path.join(REPO, 'Tools', 'ida', 'st_target_meta_1170.json')
json.dump(meta, open(out, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
print(f'[+] written {out} ({len(meta)} targets)')
idapro.close_database()
