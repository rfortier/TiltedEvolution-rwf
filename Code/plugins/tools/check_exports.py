#!/usr/bin/env python3
"""Verify the framework exports the exact plugin entry points the contract names.
"""

from __future__ import annotations

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
HEADER_REL = 'Code/plugins/STRPM/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h'
IMPL_REL = 'Code/client/Services/PluginMessagingExport.cpp'
# The contract's own manifest. Its exportNames key is the source of truth for
# which entry points the contract publishes, so it is what says whether the
# header still declares all of them.
CONTRACT_REL = 'Code/plugins/STRPM/contract.json'

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
# The two scans above are the corpus, and an empty one is a moved file or a
# renamed declaration rather than a satisfied contract: the loop below iterates
# `named`, so an empty `named` compares nothing and reaches EXPORT CHECK OK.
if not named:
    failures.append(
        f'{HEADER_REL}: no kQuery*ExportName declaration matched; the contract names the '
        f'entry points the runtime has to export, so finding none means the scan lost its '
        f'corpus rather than that the contract is met')
if not exported:
    failures.append(
        f'{IMPL_REL}: no STRPM_EXPORT definition matched; the scan compared no exported '
        f'entry point at all, which must not read as a pass')

# `named` is a dict, so the floors above only fire when the scan loses every
# constant: renaming one of the four leaves the other three in place, the loop
# below still runs, and the renamed entry point quietly stops being verified.
# The manifest names the entry points the contract publishes, so the header's
# constant values are compared against it.
contract: dict = {}
contract_path = ROOT / CONTRACT_REL
if not contract_path.is_file():
    failures.append(
        f'{CONTRACT_REL} is missing; it names the entry points the contract publishes, so '
        f'nothing says which of them the header still declares')
else:
    try:
        contract = json.loads(contract_path.read_text(encoding='utf-8'))
    except json.JSONDecodeError as exc:
        failures.append(f'{CONTRACT_REL} is not valid JSON: {exc}')

required = contract.get('exportNames')
if not isinstance(required, list) or not required:
    failures.append(
        f'{CONTRACT_REL} has no exportNames list; it is what says which entry points the '
        f'contract publishes, so without it the header is only compared against itself')
else:
    required = {str(name) for name in required}
    declared = set(named.values())
    for name in sorted(required - declared):
        failures.append(
            f'{HEADER_REL} no longer declares {name}, which {CONTRACT_REL} exportNames requires: '
            f'the constant naming that entry point was renamed or removed, so the comparison '
            f'below covers fewer entry points than the contract publishes')
    for name in sorted(declared - required):
        failures.append(
            f'{HEADER_REL} declares {name}, which {CONTRACT_REL} exportNames does not list: the '
            f'manifest no longer describes the entry points the header publishes')

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