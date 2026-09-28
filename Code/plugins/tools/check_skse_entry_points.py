#!/usr/bin/env python3
"""Every hand-written SKSE plugin must load on every game line this package ships.

The package supports three game lines and they are served by two SKSE families,
which read completely different entry points. A plugin that satisfies one family
is skipped outright by the other, and the symptom is identical from the outside:
the plugin is simply not there.

    SKSE 2.0.x (game 1.5.97)  calls SKSEPlugin_Query and SKSEPlugin_Load.
                              It never looks for SKSEPlugin_Version - the string
                              does not appear in its PluginManager at all.

    SKSE 2.2.x (game 1.6.x, 1.7.x)
                              looks SKSEPlugin_Version up by name and, when the
                              export is missing, logs "no version data" and moves
                              on. SKSEPlugin_Query is NOT a fallback.

Three separate defects of this shape reached a release:

  * SkyrimTogetherSKSE.dll exported Query and Load only, so every 1.6.x/1.7.x
    player got "no version data 0 (handle 0)" and no F2 menu while the esp files
    and the logo still showed.
  * STRPluginMessagingBridge.dll exported Version and Load but no Query, so
    SKSE 2.0.x answered "does not appear to be an SKSE plugin" and skipped it.
  * STRPluginMessagingAPI.dll and its siblings declared a compatibleVersions
    list holding 1.6.1170 alone. SKSE 2.2.x matches that list against the exact
    running build, so 1.6.640, 1.6.1179 and the whole 1.7.x line - every one of
    which this package ships an address library for - were rejected.

None of the three reproduces on the machine this repository is developed on,
which runs 1.5.97. That is the whole reason this is a source-level gate: the
assertions below are SKSE's own acceptance rules replayed against every runtime
the package ships a library for, and they run in CI where all three do.

Files that hand-write SKSEPlugin_Version are discovered, not listed, so a new
plugin cannot be added without being checked. Plugins built on CommonLibSSE
declare their version data inside the library rather than in this repository,
so they do not appear here and are covered by the build itself.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
LIB_DIR = ROOT / 'GameFiles' / 'Skyrim' / 'SKSE' / 'Plugins'

# SKSE's rules, from PluginManager::CheckPluginCompatibility and ScanPlugins.
K_VERSION_INDEPENDENT_ADDRESS_LIBRARY_POST_AE = 1 << 0
K_VERSION_INDEPENDENT_SIGNATURES = 1 << 1
K_VERSION_INDEPENDENT_STRUCTS_POST_629 = 1 << 2
K_VERSION_INDEPENDENT_EX_NO_STRUCT_USE = 1 << 0

SKIP_DIRS = {'snapshots', '.git', 'build', 'out', 'node_modules'}


def strip_comments(text: str) -> str:
    """A flag named in prose must not satisfy a check that it is set in code.

    Two of the three defects above were introduced next to a comment that
    correctly described the rule being broken, so searching the raw file would
    have passed on the explanation alone.
    """
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def source_files() -> list[pathlib.Path]:
    """Every .cpp/.h in the repository, minus generated and vendored trees."""
    found = []
    for path in ROOT.rglob('*'):
        if path.suffix not in ('.cpp', '.h', '.hpp'):
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        found.append(path)
    return sorted(found)


def balanced(text: str, start: int) -> str:
    """The text inside the brace-delimited block that starts at `start`."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    return ''


def split_top_level(text: str) -> list[str]:
    """Split an initializer on its top-level commas, keeping nested braces."""
    parts, depth, current = [], 0, ''
    for char in text:
        if char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
        if char == ',' and depth == 0:
            parts.append(current.strip())
            current = ''
        else:
            current += char
    parts.append(current.strip())
    return [part for part in parts if part]


def exported(text: str, export: str) -> bool:
    """Is this entry point defined here with C linkage and exported?"""
    return bool(re.search(
        r'__declspec\(dllexport\)\s+[\w:]+\s+' + export + r'\s*\(', text))


def query_name(text: str) -> str | None:
    """The plugin name SKSEPlugin_Query reports, which is the dll's identity."""
    for match in re.finditer(r'SKSEPlugin_Query\s*\(', text):
        if '__declspec(dllexport)' not in text[max(0, match.start() - 200):match.start()]:
            continue
        body = balanced(text, text.find('{', match.end()))
        literal = re.search(r'->\s*name\s*=\s*"([^"]*)"', body)
        if literal:
            return literal.group(1)
    return None


def version_word(major: int, minor: int, build: int, sub: int = 0) -> int:
    return ((major & 0xFF) << 24) | ((minor & 0xFF) << 16) | ((build & 0xFFF) << 4) | (sub & 0xF)


def shipped_ae_runtimes() -> dict[int, str]:
    """The runtimes the package ships an address library for, as version words.

    SKSE builds the file name it wants as versionlib-<major>-<minor>-<build>-0.bin
    with the sub-nibble written as 0, so the trailing component is not the
    runtime type. The 1.5.x libraries use a different prefix and are not listed:
    SKSE 2.0.x ignores the version data entirely, so a runtime list cannot gate
    them.
    """
    runtimes = {}
    for path in LIB_DIR.glob('versionlib-[0-9]*.bin'):
        match = re.fullmatch(r'versionlib-(\d+)-(\d+)-(\d+)-(\d+)\.bin', path.name)
        if not match:
            continue
        major, minor, build, sub = (int(g) for g in match.groups())
        runtimes[version_word(major, minor, build, sub)] = path.name
    return runtimes


def discover() -> dict[str, dict]:
    """Group entry points into plugins, keyed by the name SKSE sees.

    A plugin's translation units are tied together by the name they declare, not
    by their file: STRPluginMessagingAPI.dll declares its version data in
    STRPluginMessagingAPISKSEMetadata.cpp and its Query/Load in
    STRPluginMessagingAPIRuntime.cpp. SKSEPlugin_Load carries no name of its own,
    so it is attributed to the name its own file declares.
    """
    plugins: dict[str, dict] = {}
    for path in source_files():
        code = strip_comments(path.read_text(encoding='utf-8', errors='ignore'))
        rel = path.relative_to(ROOT).as_posix()

        version_body = None
        version_name = None
        for match in re.finditer(r'SKSEPlugin_Version\s*=\s*\{', code):
            body = balanced(code, code.index('{', match.end() - 1))
            fields = split_top_level(body)
            if len(fields) >= 3:
                literal = re.fullmatch(r'"([^"]*)"', fields[2])
                if literal:
                    version_body, version_name = body, literal.group(1)

        name = query_name(code)
        if name is None:
            name = version_name
        if name is None:
            # A lone Load with no name is not a plugin SKSE can accept; record it
            # under its path so the missing pairs are reported rather than hidden.
            if not exported(code, 'SKSEPlugin_Load'):
                continue
            name = rel

        entry = plugins.setdefault(name, {'name': name, 'files': set()})
        entry['files'].add(rel)
        if version_body is not None:
            entry['version'] = (version_body, rel)
        for export in ('SKSEPlugin_Query', 'SKSEPlugin_Load'):
            if exported(code, export):
                entry[export] = rel
    return plugins


def main() -> int:
    runtimes = shipped_ae_runtimes()
    if not runtimes:
        print('SKSE PLUGIN ENTRY POINT CHECK FAILED (1)')
        print(f'  - no address library found under {LIB_DIR.relative_to(ROOT)}; the '
              f'supported-runtime list is what the version-list check compares '
              f'against, so an empty one would pass vacuously')
        return 1

    plugins = discover()
    if not plugins:
        print('SKSE PLUGIN ENTRY POINT CHECK FAILED (1)')
        print('  - no SKSE plugin entry point found anywhere. Every plugin that '
              'ships would be skipped by SKSE 2.2.x; if the declarations moved '
              'into a build system, this check needs to follow them')
        return 1

    failures: list[str] = []
    for name in sorted(plugins):
        entry = plugins[name]
        where = entry['version'][1] if 'version' in entry else sorted(entry['files'])[0]
        label = f'{name} ({where})'

        # SKSE 2.2.x: the version data is the only way in.
        if 'version' not in entry:
            failures.append(
                f'{label}: no exported SKSEPlugin_Version. SKSE 2.2.x looks it up '
                f'by name and never falls back to SKSEPlugin_Query, so the plugin '
                f'is skipped on every 1.6.x and 1.7.x runtime with "no version '
                f'data"')

        # SKSE 2.0.x: Query and Load together, or the file is not a plugin.
        for export, lines in (('SKSEPlugin_Query', '1.5.97'),
                              ('SKSEPlugin_Load', 'all of them')):
            if export not in entry:
                failures.append(
                    f'{label}: no exported {export}. SKSE 2.0.x requires Query and '
                    f'Load together and logs "does not appear to be an SKSE '
                    f'plugin" when either is missing, so the plugin is skipped on '
                    f'game {lines}')

        if 'version' not in entry:
            continue

        body = entry['version'][0]
        fields = split_top_level(body)
        if len(fields) < 9:
            failures.append(
                f'{label}: SKSEPlugin_Version has {len(fields)} initializer '
                f'fields, expected the 9 of SKSEPluginVersionData')
            continue

        ex_flags, flags, compat = fields[5], fields[6], fields[7]

        # SKSE: versionIndependent = versionIndependence & (PostAE | Signatures)
        has_signatures = 'kVersionIndependent_Signatures' in flags
        has_post_ae = 'kVersionIndependent_AddressLibraryPostAE' in flags
        independent = has_signatures or has_post_ae

        if not independent:
            # SKSE now matches compatibleVersions against the exact running build.
            listed = {int(tok, 0) for tok in
                      re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', compat)}
            missing = [runtimes[word] for word in sorted(runtimes)
                       if word not in listed]
            if missing:
                failures.append(
                    f'{label}: versionIndependence declares no independence flag '
                    f'({"none" if flags.strip() in ("", "0") else flags.strip()}), '
                    f'so SKSE 2.2.x matches compatibleVersions against the exact '
                    f'running build and rejects {", ".join(missing)} with '
                    f'"disabled, incompatible with current version of the game". '
                    f'Declare kVersionIndependent_Signatures instead of a list, or '
                    f'add them')
            continue

        # Version-independent, so SKSE no longer reads compatibleVersions, but it
        # still enforces the structure layout on 1.6.629+.
        if not ('kVersionIndependent_StructsPost629' in flags
                or 'kVersionIndependentEx_NoStructUse' in ex_flags):
            failures.append(
                f'{label}: versionIndependenceEx declares neither '
                f'kVersionIndependent_StructsPost629 nor '
                f'kVersionIndependentEx_NoStructUse, so SKSE 1.6.629+ answers '
                f'"disabled, only compatible with versions earlier than 1.6.629"')

        if has_post_ae and not has_signatures:
            failures.append(
                f'{label}: claims kVersionIndependent_AddressLibraryPostAE without '
                f'kVersionIndependent_Signatures. That makes SKSE run '
                f'CheckAddressLibrary, which requires versionlib-<runtime>.bin to '
                f'exist, and on 1.7.99+ it also demands the AddressLibraryV5 flag')

    if failures:
        print(f'SKSE PLUGIN ENTRY POINT CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('SKSE PLUGIN ENTRY POINT CHECK OK')
    print(f'  {len(plugins)} hand-written plugin(s) checked against both SKSE families')
    for name in sorted(plugins):
        entry = plugins[name]
        exports = [e for e in ('SKSEPlugin_Version', 'SKSEPlugin_Query',
                               'SKSEPlugin_Load') if
                   (e == 'SKSEPlugin_Version' and 'version' in entry) or
                   (e != 'SKSEPlugin_Version' and e in entry)]
        version = 'version-independent' if 'version' in entry else 'no version data'
        print(f'    {name:<30} {", ".join(e.replace("SKSEPlugin_", "") for e in exports):<14} {version}')
    print(f'  {len(runtimes)} shipped AE runtime(s) covered')
    return 0


if __name__ == '__main__':
    sys.exit(main())
