#!/usr/bin/env python3
"""Verify the framework exports the exact plugin entry points the contract names.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
HEADER_REL = 'Code/plugins/STRPM/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h'
IMPL_REL = 'Code/client/Services/PluginMessagingExport.cpp'

header = (ROOT / HEADER_REL).read_text(encoding='utf-8', errors='replace')
impl = (ROOT / IMPL_REL).read_text(encoding='utf-8', errors='replace')

named = dict(re.findall(r'(kQuery\w*ExportName)\[\]\s*=\s*"([^"]+)"', header))
# Accepts either spelling: STRPM_EXPORT on its own (it already expands to
# extern "C" __declspec(dllexport)) or an explicit extern "C" prefix. What
# matters is that a definition exists whose name is unmangled.
exported = set(re.findall(r'(?:extern\s+"C"\s+)?STRPM_EXPORT[^;{}]*?\b(STR_\w+|STRPM_\w+)\s*\(', impl))

print('contract names  :', ', '.join(sorted(named.values())) or '(none found)')
print('exported symbols:', ', '.join(sorted(exported)) or '(none found)')

failures = []
for constant, symbol in sorted(named.items()):
    if symbol not in exported:
        failures.append(f'{constant} -> {symbol} is named by the contract but not exported by {IMPL_REL}')

if failures:
    print()
    print(f'EXPORT CHECK FAILED ({len(failures)})')
    for failure in failures:
        print('  -', failure)
    sys.exit(1)

print()
print('EXPORT CHECK OK')
print(f'  {len(named)} contract entry point(s) exported by the framework runtime')