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

# Which interfaces each plugin's copy is recorded as declaring. A plugin may
# legitimately use only some of them, so a class the lookup does not find reads
# as "not used here" - but that is only true for one this plugin never declared.
# IOverlayInterface and IOverrideInterface are each declared twice, once per
# plugin, so renaming one copy leaves the other in `declaring`, no floor fires,
# and that copy's method order silently leaves the comparison.
EXPECTED: dict[str, set[str]] = {
    'MorphSyncTogether': {'IBodyMorphInterface', 'IOverlayInterface', 'IOverrideInterface'},
    'OStimTogether': {'IOverlayInterface', 'IOverrideInterface'},
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
declaring: dict[str, list[str]] = {class_name: [] for class_name in REFERENCE}

for plugin, rel in SOURCES.items():
    text = (ROOT / rel).read_text(encoding='utf-8', errors='replace')
    found_here = 0
    seen_here: set[str] = set()
    for class_name, reference in REFERENCE.items():
        body = class_body(text, class_name)
        if body is None:
            continue  # this plugin does not use that interface
        found_here += 1
        seen_here.add(class_name)
        declaring[class_name].append(plugin)
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

    # A corpus floor, not a magic number: every plugin in SOURCES vendors these
    # declarations, so a plugin that matches none of them is a scan that lost its
    # corpus rather than a plugin that uses no interface. The lookup used to read
    # 'class not found' as 'this plugin does not use that interface' and still
    # counted the pair, so renaming the declarations read as a pass.
    if not found_here:
        failures.append(
            f'{rel} declares none of {sorted(REFERENCE)}; either {plugin} stopped using '
            f'SKEE - then drop it from SOURCES - or its declarations were renamed or moved, '
            f'and this check compared nothing for it'
        )

    # The floor above only fires when a plugin's declarations vanish entirely,
    # and the per-interface floor below only when every copy loses a class. A
    # plugin that stops declaring one interface it is recorded as declaring is
    # neither: the comparison above skipped it, so its method order was never
    # checked against RaceMenu.
    expected = EXPECTED.get(plugin)
    if expected is None:
        failures.append(
            f'{plugin} is listed in SOURCES but not in EXPECTED, so nothing records which '
            f'interfaces its copy is supposed to declare and one of them can leave the '
            f'comparison unnoticed'
        )
    else:
        for class_name in sorted(expected - seen_here):
            failures.append(
                f'{rel} no longer declares {class_name}, which EXPECTED records for {plugin}: '
                f'this copy\'s method order was never compared against RaceMenu, and the other '
                f'copy still declaring it means no floor above or below fires'
            )
        for class_name in sorted(seen_here - expected):
            failures.append(
                f'{rel} declares {class_name}, which EXPECTED does not record for {plugin}; '
                f'record it there, otherwise a later rename of that declaration leaves this '
                f'copy out of the comparison unnoticed'
            )

# A recorded expectation that no longer corresponds to anything this gate looks
# at is stale, and a stale record is a failure rather than a note: it claims a
# plugin covers an interface whose declaration the gate has stopped reading.
for plugin, expected in EXPECTED.items():
    if plugin not in SOURCES:
        failures.append(
            f'EXPECTED records {plugin}, which SOURCES no longer lists; if the plugin left the '
            f'repository, drop the entry rather than leaving it claiming a comparison that '
            f'cannot happen'
        )
        continue
    for class_name in sorted(expected - set(REFERENCE)):
        failures.append(
            f'EXPECTED records {class_name} for {plugin}, which REFERENCE does not list; drop '
            f'the entry or restore the interface rather than leaving it claiming a comparison '
            f'that cannot happen'
        )

# One floor per interface REFERENCE lists, because the per-plugin floor above only
# fires when a whole plugin's declarations vanish: renaming a single class in both
# copies leaves the other two found and this check silently covering two of three.
for class_name, plugins_declaring in declaring.items():
    if not plugins_declaring:
        failures.append(
            f'none of {sorted(SOURCES)} declares {class_name}, which REFERENCE lists as a '
            f'RaceMenu interface: its declaration was renamed or moved, so its method order '
            f'was never compared. Restore it, or drop it from REFERENCE rather than leaving '
            f'this check covering nothing')

if not checked:
    failures.append(
        f'no SKEE interface declaration was found in {sorted(SOURCES.values())}; the scan '
        f'found nothing to compare, and finding nothing is a failure rather than a pass'
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
