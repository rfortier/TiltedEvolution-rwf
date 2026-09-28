#!/usr/bin/env python3
"""Keep the companion plugins' SKEE interface declarations honest.

MorphSyncTogether and OStimTogether both talk to RaceMenu through SKEE's
published interface exchange: they dispatch kMessageExchangeInterface, then ask
the returned IInterfaceMap for "Overlay", "Override" or "BodyMorph". Each
plugin carries its own copy of the interface declarations, because RaceMenu's
headers cannot be a build dependency of a plugin that has to compile without
them.

That copy is a promise about memory layout, and the exchange API cannot check
it: QueryInterface answers by name, so a RaceMenu whose interfaces are a
different version hands back a pointer whose vtable has a different order, and
every call after it dispatches to the wrong method. That is not hypothetical.
RaceMenu 3.x ships version 1 of IOverlayInterface, whose slot 11 is
RevertHeadOverlays; these plugins declare the version 2 layout, whose slot 11 is
GetOverlayCount. The call passed OverlayType::Spell (1) where RevertHeadOverlays
expects a TESObjectREFR*, which faulted reading [1 + 0x14] - the crash in
skee64.dll+0x81be8, "target address 0x15", reproduced three times.

Two things have to stay true, and both are cheap to break silently:

  1. The declared method order is the real one. A method inserted or dropped
     shifts every later slot, and the mistake is invisible until a user's game
     dies inside a third-party DLL.
  2. The declared method order is covered by a version check. A declaration that
     matches today's RaceMenu is still wrong on an older one, and nothing in the
     code says so.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]

# RaceMenu's IPluginInterface.h, in declaration order, from the upstream header.
# Trailing methods a plugin does not call may be omitted; the order of the ones
# it does declare may not change.
REFERENCE = {
    'IOverlayInterface': [
        'HasOverlays', 'AddOverlays', 'RemoveOverlays', 'RevertOverlays',
        'RevertOverlay', 'EraseOverlays', 'RevertHeadOverlays',
        'RevertHeadOverlay', 'GetOverlayCount', 'GetOverlayFormat',
        'RegisterInstallCallback', 'UnregisterInstallCallback',
    ],
    'IOverrideInterface': [
        'HasArmorAddonNode', 'HasArmorOverride', 'AddArmorOverride',
        'GetArmorOverride', 'RemoveArmorOverride', 'SetArmorProperties',
        'SetArmorProperty', 'GetArmorProperty', 'ApplyArmorOverrides',
        'RemoveAllArmorOverrides', 'RemoveAllArmorOverridesByReference',
        'RemoveAllArmorOverridesByArmor', 'RemoveAllArmorOverridesByAddon',
        'RemoveAllArmorOverridesByNode', 'HasNodeOverride', 'AddNodeOverride',
        'GetNodeOverride', 'RemoveNodeOverride', 'SetNodeProperties',
        'SetNodeProperty', 'GetNodeProperty', 'ApplyNodeOverrides',
        'RemoveAllNodeOverrides', 'RemoveAllNodeOverridesByReference',
        'RemoveAllNodeOverridesByNode', 'HasSkinOverride', 'AddSkinOverride',
        'GetSkinOverride', 'RemoveSkinOverride', 'SetSkinProperties',
        'SetSkinProperty', 'GetSkinProperty', 'ApplySkinOverrides',
        'RemoveAllSkinOverrides', 'RemoveAllSkinOverridesByReference',
        'RemoveAllSkinOverridesBySlot',
    ],
    'IBodyMorphInterface': [
        'SetMorph', 'GetMorph', 'ClearMorph', 'GetBodyMorphs',
        'ClearBodyMorphNames', 'VisitMorphs', 'VisitKeys', 'VisitMorphValues',
        'ClearMorphs', 'ApplyVertexDiff', 'ApplyBodyMorphs', 'UpdateModelWeight',
        'SetCacheLimit', 'HasMorphs', 'EvaluateBodyMorphs', 'HasBodyMorph',
        'HasBodyMorphName', 'HasBodyMorphKey', 'ClearBodyMorphKeys',
        'VisitStrings', 'VisitActors', 'ClearMorphCache',
        'AddMorphShapeCallback',
    ],
}

# The oldest interface version each declared layout is still correct for.
# IBodyMorphInterface only ever appends, so a version 4 declaration remains a
# valid prefix of version 5. IOverlayInterface and IOverrideInterface were
# reordered in version 2, so a version 2 declaration is wrong for version 1 from
# slot 2 onwards.
MINIMUM_VERSION = {
    'IOverlayInterface': 2,
    'IOverrideInterface': 2,
    'IBodyMorphInterface': 4,
}

# Each plugin keeps its own copy of the declarations it uses.
SOURCES = {
    'MorphSyncTogether': 'plugins/MorphSyncTogether/src/SkeeInterfaces.h',
    'OStimTogether': 'plugins/OStimTogether/src/RaceMenuOverlayBridge.cpp',
}


def class_body(text: str, class_name: str) -> str | None:
    """The text of one class declaration, brace-matched.

    Brace-matched rather than regex-terminated on purpose: these classes nest
    visitor and variant helper classes, so scanning to the first closing brace
    would stop inside the first nested one and undercount the methods.
    """
    match = re.search(r'\bclass\s+' + class_name + r'\b', text)
    if not match:
        return None
    start = text.find('{', match.end())
    if start < 0:
        return None
    depth = 0
    index = start
    while index < len(text):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
        index += 1
    return None


def own_virtuals(body: str) -> list[str]:
    """The virtual methods declared directly by the class, in order."""
    declared: list[str] = []
    depth = 0
    index = 0
    while index < len(body):
        char = body[index]
        if char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
        elif depth == 1 and body.startswith('virtual', index):
            found = re.match(r'virtual\s+(.+?)\b(\w+)\s*\(', body[index:], re.S)
            if found:
                declared.append(found.group(2))
                index += found.end()
                continue
        index += 1
    return declared


failures: list[str] = []
checked = 0

for plugin, rel in SOURCES.items():
    text = (ROOT / rel).read_text(encoding='utf-8', errors='replace')
    for class_name, reference in REFERENCE.items():
        body = class_body(text, class_name)
        if body is None:
            continue  # this plugin does not use that interface
        declared = own_virtuals(body)
        checked += 1

        if declared != reference[:len(declared)]:
            divergence = next(
                (i for i, (a, b) in enumerate(zip(declared, reference)) if a != b),
                min(len(declared), len(reference)),
            )
            got = declared[divergence] if divergence < len(declared) else '(nothing)'
            want = reference[divergence] if divergence < len(reference) else '(nothing)'
            failures.append(
                f'{plugin}/{class_name} diverges from RaceMenu at slot {divergence}: '
                f'declares {got}, RaceMenu has {want}. Every later call would dispatch '
                f'to the wrong method.'
            )
            continue

        minimum = MINIMUM_VERSION.get(class_name)
        if minimum is None:
            continue
        guard = re.search(r'kSupportedVersion\s*=\s*(\d+)', body)
        if not guard:
            failures.append(
                f'{plugin}/{class_name} declares the RaceMenu layout but never states '
                f'the version it is valid for, so an older RaceMenu is accepted and '
                f'called through the wrong vtable.'
            )
        elif int(guard.group(1)) < minimum:
            failures.append(
                f'{plugin}/{class_name} declares version {guard.group(1)} but the '
                f'declared layout only matches version {minimum} or newer'
            )

print(f'skee interfaces: {checked} declaration(s) checked against RaceMenu')

if failures:
    print()
    print(f'SKEE ABI CHECK FAILED ({len(failures)})')
    for failure in failures:
        print('  -', failure)
    sys.exit(1)

print()
print('SKEE ABI CHECK OK')
print('  declared method orders match RaceMenu and carry a version guard')
