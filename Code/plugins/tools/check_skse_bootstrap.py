#!/usr/bin/env python3
"""Keep the SKSE bootstrap loadable by the SKSE versions this package supports.

SkyrimTogetherSKSE.dll is not the multiplayer runtime - it is the loader that
deploys SkyrimTogetherRuntime.dll into the game folder and starts it. Everything
a player sees depends on it running, and on SKSE 2.2.x it ran on nothing:

    plugin SkyrimTogetherSKSE.dll (00000000  00000000) no version data 0 (handle 0)

That line is SKSE's PluginManager::ScanPlugins saying it never added the plugin
to the load list. SKSE 2.2.x looks the entry point up by the name
"SKSEPlugin_Version" and, when the export is missing, logs "no version data" and
moves on - there is no fallback to the legacy SKSEPlugin_Query, which is all
this dll exported. So SKSEPlugin_Load never ran, the runtime was never deployed,
and the F2 menu never opened while the esp files still appeared in the Creations
menu and the logo still showed on the main menu.

SKSE 2.0.x (game 1.5.97) never looks for the export and still calls Query, which
is exactly why the same dll kept working there and the defect went unnoticed.

This checks the source rather than a built binary, because the build that ships
runs on CI and the failure is invisible locally on 1.5.x.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
SOURCE_REL = 'Code/skse_bootstrap/main.cpp'

# SKSE's own acceptance rules, from PluginManager::CheckPluginCompatibility and
# ScanPlugins. Kept here so the assertions below read as the runtime's rules.
K_VERSION_INDEPENDENT_ADDRESS_LIBRARY_POST_AE = 1 << 0
K_VERSION_INDEPENDENT_SIGNATURES = 1 << 1
K_VERSION_INDEPENDENT_STRUCTS_POST_629 = 1 << 2
K_VERSION_INDEPENDENT_EX_NO_STRUCT_USE = 1 << 0

KNOWN_INDEPENDENCE = (
    K_VERSION_INDEPENDENT_ADDRESS_LIBRARY_POST_AE
    | K_VERSION_INDEPENDENT_SIGNATURES
    | K_VERSION_INDEPENDENT_STRUCTS_POST_629
)


def strip_comments(text: str) -> str:
    """Comments describe the flags; only code may be searched for them.

    Without this the explanatory comment naming a flag would satisfy the checks
    that exist to prove the flag is actually set.
    """
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def main() -> int:
    path = ROOT / SOURCE_REL
    if not path.is_file():
        print('SKSE BOOTSTRAP CHECK FAILED (1)')
        print(f'  - {SOURCE_REL} is missing')
        return 1

    raw = path.read_text(encoding='utf-8', errors='ignore')
    code = strip_comments(raw)

    failures: list[str] = []

    # 1. The export SKSE 2.2.x requires must exist, be C-linkage and be defined
    #    once. Two definitions would not link, but one definition inside a
    #    namespace would export a mangled name SKSE cannot find.
    definitions = re.findall(
        r'__declspec\(dllexport\)\s+SksePluginVersionData\s+SKSEPlugin_Version\s*=',
        code)
    if not definitions:
        failures.append(
            f'{SOURCE_REL}: no exported SKSEPlugin_Version. SKSE 2.2.x looks this '
            f'up by name and, without it, logs "no version data" and never loads '
            f'the plugin - SKSEPlugin_Query is not a fallback')
    elif len(definitions) > 1:
        failures.append(f'{SOURCE_REL}: SKSEPlugin_Version is defined {len(definitions)} times')

    extern_c = code.find('extern "C"')
    export_at = code.find('SKSEPlugin_Version')
    if extern_c < 0 or export_at < 0 or export_at < extern_c:
        failures.append(
            f'{SOURCE_REL}: SKSEPlugin_Version is not inside an extern "C" block, '
            f'so it would be exported with a mangled name SKSE cannot look up')

    # 2. The struct must carry the field order SKSE reads. SKSE casts the export
    #    to its own SKSEPluginVersionData, so a reordered or resized struct makes
    #    every later field read from the wrong offset - silently, because the
    #    first fields would still look valid.
    struct = re.search(r'struct SksePluginVersionData\s*\{(.*?)\n\};', code, re.S)
    if not struct:
        failures.append(f'{SOURCE_REL}: no SksePluginVersionData struct')
    else:
        body = struct.group(1)
        fields = [
            (m.group(1).strip(), m.group(2), m.group(3) or '')
            for m in re.finditer(
                r'^\s+(uint32_t|char)\s+(\w+)(\[\d+\])?;', body, re.M)
        ]
        expected = [
            ('uint32_t', 'dataVersion', ''),
            ('uint32_t', 'pluginVersion', ''),
            ('char', 'name', '[256]'),
            ('char', 'author', '[256]'),
            ('char', 'supportEmail', '[252]'),
            ('uint32_t', 'versionIndependenceEx', ''),
            ('uint32_t', 'versionIndependence', ''),
            ('uint32_t', 'compatibleVersions', '[16]'),
            ('uint32_t', 'seVersionRequired', ''),
        ]
        if fields != expected:
            failures.append(
                f'{SOURCE_REL}: SksePluginVersionData fields are {fields}, but SKSE '
                f'reads {expected}; SKSE casts this export to its own struct, so a '
                f'difference reads every later field from the wrong offset')

    # 3. The declared flags must satisfy SKSE's rules. Read them from the
    #    initializer, so the check follows the code rather than a comment.
    initializer = re.search(
        r'SKSEPlugin_Version\s*=\s*\{(.*?)\n\};', code, re.S)
    if initializer and not failures:
        init = initializer.group(1)

        has_signatures = 'kVersionIndependent_Signatures' in init
        has_post_ae = 'kVersionIndependent_AddressLibraryPostAE' in init
        has_structs = 'kVersionIndependent_StructsPost629' in init
        has_no_struct = 'kVersionIndependentEx_NoStructUse' in init

        if not (has_signatures or has_post_ae):
            failures.append(
                f'{SOURCE_REL}: SKSEPlugin_Version claims no version independence, '
                f'so SKSE matches compatibleVersions against the exact running '
                f'build and rejects every runtime not listed')
        if not (has_structs or has_no_struct):
            failures.append(
                f'{SOURCE_REL}: SKSEPlugin_Version claims neither '
                f'kVersionIndependent_StructsPost629 nor '
                f'kVersionIndependentEx_NoStructUse, so SKSE 1.6.629+ answers '
                f'"disabled, only compatible with versions earlier than 1.6.629"')
        if has_post_ae and not has_signatures:
            # Not fatal on its own, but it adds the address-library requirement
            # and the 1.7.99+ V5 flag on top of a dll that resolves nothing.
            failures.append(
                f'{SOURCE_REL}: SKSEPlugin_Version claims '
                f'kVersionIndependent_AddressLibraryPostAE without '
                f'kVersionIndependent_Signatures; that makes SKSE require '
                f'versionlib-<runtime>.bin and, on 1.7.99+, the AddressLibraryV5 '
                f'flag, for a dll that resolves no engine addresses')

    # 4. Query and Load must still be exported for SKSE 2.0.x, which ignores
    #    SKSEPlugin_Version entirely and calls Query.
    for required in ('SKSEPlugin_Query', 'SKSEPlugin_Load'):
        if not re.search(r'__declspec\(dllexport\)\s+bool\s+' + required + r'\s*\(', code):
            failures.append(
                f'{SOURCE_REL}: {required} is no longer exported; SKSE 2.0.x '
                f'(game 1.5.97) never reads SKSEPlugin_Version and would stop '
                f'loading the plugin entirely')

    if failures:
        print(f'SKSE BOOTSTRAP CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('SKSE BOOTSTRAP CHECK OK')
    print(f'  SKSEPlugin_Version exported, struct layout matches SKSE, '
          f'flags satisfy 1.6.629+ and 1.7.99+')
    print(f'  SKSEPlugin_Query/Load kept for SKSE 2.0.x (1.5.97)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
