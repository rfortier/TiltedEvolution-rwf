#!/usr/bin/env python3
"""Apply this repository's own fixes on top of the pinned plugin submodules.

The seven companion plugins are submodules pointing at repositories this project
does not own and cannot push to (docs/PLUGIN-SOURCES.md). That is fine for
following upstream and fatal for shipping a fix: a bug in plugin source cannot
be corrected by editing the working tree, because CI runs

    git submodule update --init --force --recursive --depth=1

which discards every local change before anything is built. A fix that lives
only in the working tree therefore never reaches a package.

This tool is the way such a fix does reach a package. Patches are ordinary
content of this repository under Code/plugins/patches/<plugin>/, so they are
reviewed, versioned and shipped like everything else; they are applied to the
submodule working tree immediately before the plugin is configured and built.

    apply   apply every patch (idempotent; the CI entry point)
    check   report whether every patch is currently applied
    revert  reverse every patch, restoring the pinned commit
    verify  apply --check every patch against a pristine tree, require every
            patched file to be one the plugin's build actually compiles, and
            require every patch path and every patched-source marker a document
            names to be one this repository records (the gate)

apply is written to be safe to run repeatedly: a patch that is already applied
is detected and skipped rather than failing the build, and a patch that does not
apply to the pinned commit is a hard error, because that means the pin moved and
the patch needs rewriting.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MANIFEST_REL = 'Code/plugins/plugins.json'
PATCH_DIR_REL = 'Code/plugins/patches'


def run(args: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess:
    result = subprocess.run(args, cwd=str(cwd or ROOT), capture_output=True, text=True)
    if check and result.returncode != 0:
        raise SystemExit(f"command failed: {' '.join(args)}\n{result.stderr.strip()}")
    return result


def plugins() -> list[dict]:
    """Every entry this repository patches: the staged plugins and the
    prerequisites it depends on.

    Prerequisites are not staged into the archive, but they are submodules this
    repository builds and pins, so they take patches for exactly the same reason
    the plugins do - a fix edited into the working tree is discarded by CI's
    --force checkout and never reaches a release.
    """
    manifest = json.loads((ROOT / MANIFEST_REL).read_text(encoding='utf-8'))
    return manifest['plugins'] + manifest.get('prerequisites', [])


def patch_files(plugin_id: str) -> list[Path]:
    directory = ROOT / PATCH_DIR_REL / plugin_id
    if not directory.is_dir():
        return []
    return sorted(p for p in directory.glob('*.patch') if p.is_file())


def submodule_dir(plugin: dict) -> Path:
    return ROOT / plugin['submodule']


# A patch is only a fix if the file it edits is compiled. Nothing else in this
# tool can tell: git apply checks text, and the build checks whatever CMake was
# told to compile, so a patch aimed at a source file the plugin's CMakeLists.txt
# never mentions applies cleanly, verifies, builds, and changes nothing at all.
# IEDSyncTogether/0002 was exactly that - it edited src/SyncService.cpp, which
# the pinned CMakeLists.txt stopped listing in ace777c ("Build v0.6.0 STRPM-only
# runtime"), so the bound it added was never in any binary.
#
# Reachability, not membership: MorphSyncTogether compiles src/AppearanceProbe.cpp
# and src/MorphSyncService.cpp without naming them, because
# src/AppearanceProbeBodyHair.cpp and src/MorphSyncServiceBodyHair.cpp #include
# them after renaming the entry points. Asking "is this path in CMakeLists.txt"
# would fail those two patches, so the question asked here is "does an #include
# chain from a file CMakeLists.txt does name reach this one".
IGNORED_DIRECTORIES = {'build', '.git', '.xmake', 'imgui', 'vcpkg', 'vcpkg_installed', 'out'}
SOURCE_TOKEN = re.compile(r'[\w$./\\-]+\.(?:cpp|cxx|cc|h|hpp)')
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
PATCH_TARGET = re.compile(r'^--- a/(\S+)$', re.MULTILINE)
SOURCE_SUFFIXES = {'.cpp', '.cxx', '.cc', '.h', '.hpp'}


def build_roots(plugin: dict) -> list[Path]:
    """The CMakeLists.txt files that decide what this plugin compiles.

    The plugin root is the usual place; OStimNG keeps its project one directory
    down in skse/, so the tree is searched instead of assumed.
    """
    base = submodule_dir(plugin)
    roots = []
    for path in sorted(base.rglob('CMakeLists.txt')):
        relative = path.relative_to(base)
        if any(part in IGNORED_DIRECTORIES for part in relative.parts):
            continue
        if len(relative.parts) > 3:
            continue
        roots.append(path)
    return roots


def cmake_source_seeds(roots: list[Path], base: Path) -> set[Path] | None:
    """Every source file the CMakeLists.txt files name, or None when the plugin
    collects its sources with a glob.

    A glob names no file, so there is nothing to seed a closure with and the
    plugin cannot be checked this way. OStimNG builds through
    skse/cmake/lib/autoCollectSources.cmake, which is a glob.
    """
    seeds = set()
    for root in roots:
        text = root.read_text(encoding='utf-8', errors='replace')
        if 'GLOB' in text:
            return None
        for match in SOURCE_TOKEN.finditer(text):
            token = match.group(0)
            # A path may be spelled through a CMake variable - OStimTogether
            # reads ${CMAKE_CURRENT_SOURCE_DIR}/src/OStimBridge.cpp, rewrites it
            # and compiles the copy it writes - so the token can begin in the
            # middle of a path. Every suffix is tried, which resolves both that
            # and a leading separator.
            parts = [part for part in re.split(r'[/\\]', token) if part]
            for index in range(len(parts)):
                name = '/'.join(parts[index:])
                for candidate in (root.parent / name, base / name):
                    if candidate.is_file():
                        seeds.add(candidate.resolve())
                        break
                else:
                    continue
                break
    return seeds


def include_closure(seeds: set[Path], base: Path) -> set[Path]:
    """Every file reachable from the seeds through #include "..."."""
    reached = set()
    pending = sorted(seeds)
    while pending:
        path = pending.pop()
        if path in reached:
            continue
        reached.add(path)
        text = path.read_text(encoding='utf-8', errors='replace')
        for match in INCLUDE_RE.finditer(text):
            name = match.group(1)
            candidates = (
                path.parent / name,
                base / name,
                base / 'src' / name,
                base / 'include' / name,
                base / 'skse' / 'src' / name,
            )
            for candidate in candidates:
                if candidate.is_file():
                    resolved = candidate.resolve()
                    if resolved not in reached:
                        pending.append(resolved)
                    break
    return reached


def patch_targets(patch: Path) -> list[str]:
    """The files a patch edits, taken from its '--- a/<path>' header lines.

    Four of the recorded patches are written in the older format that has no
    'diff --git' line, so the '---' header is the only target every patch shares.
    """
    targets = []
    for match in PATCH_TARGET.finditer(patch.read_text(encoding='utf-8', errors='replace')):
        if match.group(1) not in targets:
            targets.append(match.group(1))
    return targets


def uncompiled_patch_targets(plugin: dict) -> list[str]:
    """Patches whose edited source file the plugin's build never compiles."""
    roots = build_roots(plugin)
    if not roots:
        return []
    seeds = cmake_source_seeds(roots, submodule_dir(plugin))
    if seeds is None:
        return []
    reached = include_closure(seeds, submodule_dir(plugin))
    failures = []
    for patch in patch_files(plugin['id']):
        for target in patch_targets(patch):
            if Path(target).suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if (submodule_dir(plugin) / target).resolve() not in reached:
                failures.append(
                    f"{plugin['id']}/{patch.name} edits {target}, which this plugin's build "
                    f"never compiles - no CMakeLists.txt source and no #include chain from "
                    f"one reaches it, so the change cannot reach a release"
                )
    return failures


# A format placeholder whose braces contain a quote. The C++ literal for
# path=\"{}\" is written with escaped quotes, and writing the escape in the wrong
# place - path=\"{\"} - puts the quote *inside* the braces. fmt then reads the quote
# as the format spec, std::string has no formatter for it, and the translation unit
# fails to compile: that typo is what broke the DAVSyncTogether build with
# error C7595 at DAVConfigIndex.cpp(109,17), and the patch that carried it lives in
# this repository, so nothing upstream would ever have caught it.
#
# The rule is deliberately narrow. A real format spec is alignment, width,
# precision or a type letter; a quote in one is always a misplaced escape, so this
# cannot fire on a legitimate spec.
FORMAT_FIELD = re.compile(r'\{([^{}]*)\}')

# ...and a spec is alignment, width, precision or a type letter, so it never
# contains a comma or a space. Requiring a spec-shaped field is what keeps this
# from firing on a brace group written in another language: a patch that edits
# the relay's Lua carries `{ "sender", "senderPlayerId", ... }`, which holds
# quotes but is not a placeholder at all. The typo this rule was built for
# (`path="{"}"`, where the field is a lone quote) still matches.
FORMAT_SPEC = re.compile(r'[^,\s]*')


def unescape_cpp(literal: str) -> str:
    """The bytes the compiler sees, for the escapes that matter here."""
    return literal.replace('\\\\', '\x00').replace('\\"', '"').replace('\x00', '\\')


def malformed_format_fields(text: str) -> list[tuple[int, str]]:
    """(line, field) for every placeholder that cannot be a valid format spec.

    One entry per line, not per quote: a line with two broken placeholders is one
    mistake to fix, and reporting it twice only makes the output noisier.
    """
    found = []
    for number, line in enumerate(text.splitlines(), 1):
        # Only added lines: a '-' line is the pinned source, not ours to fix.
        if not line.startswith('+') or line.startswith('+++'):
            continue
        # fmt treats {{ and }} as literal braces, so drop them before parsing.
        candidate = unescape_cpp(line[1:]).replace('{{', '').replace('}}', '')
        for field in FORMAT_FIELD.findall(candidate):
            if '"' in field and FORMAT_SPEC.fullmatch(field):
                found.append((number, field))
                break
    return found


def is_applied(directory: Path, patch: Path) -> bool:
    """True when the patch is already in the tree (forward apply would fail)."""
    forward = run(['git', 'apply', '--check', '--reverse', str(patch)], cwd=directory, check=False)
    return forward.returncode == 0


def applies_cleanly(directory: Path, patch: Path) -> bool:
    return run(['git', 'apply', '--check', str(patch)], cwd=directory, check=False).returncode == 0


def cmd_apply(_args) -> int:
    applied = skipped = 0
    for plugin in plugins():
        directory = submodule_dir(plugin)
        for patch in patch_files(plugin['id']):
            if not directory.is_dir():
                print(f"  {plugin['id']}: submodule not checked out, skipping {patch.name}")
                continue
            if is_applied(directory, patch):
                skipped += 1
                continue
            if not applies_cleanly(directory, patch):
                print(f"PATCH FAILED {plugin['id']}/{patch.name}")
                print(f"  it does not apply to the pinned commit; the pin moved and the")
                print(f"  patch needs rewriting against the new one.")
                detail = run(['git', 'apply', '--check', '--verbose', str(patch)], cwd=directory, check=False)
                for line in (detail.stderr or detail.stdout).splitlines()[:10]:
                    print('   ', line)
                return 1
            result = run(['git', 'apply', str(patch)], cwd=directory, check=False)
            if result.returncode != 0:
                print(f"PATCH FAILED {plugin['id']}/{patch.name}")
                print('   ', (result.stderr or '').strip()[:500])
                return 1
            applied += 1
            print(f"  applied {plugin['id']}/{patch.name}")
    print(f"patches: {applied} applied, {skipped} already present")
    return 0


def cmd_check(_args) -> int:
    failures = lint_patches()
    for plugin in plugins():
        directory = submodule_dir(plugin)
        for patch in patch_files(plugin['id']):
            if not directory.is_dir():
                failures.append(f"{plugin['id']}: submodule not checked out")
                continue
            if not is_applied(directory, patch):
                failures.append(f"{plugin['id']}/{patch.name} is not applied")
    for failure in failures:
        print('  -', failure)
    if failures:
        print(f"PATCH CHECK FAILED ({len(failures)})")
        return 1
    print('PATCH CHECK OK')
    return 0


def cmd_revert(_args) -> int:
    reverted = 0
    for plugin in plugins():
        directory = submodule_dir(plugin)
        for patch in reversed(patch_files(plugin['id'])):
            if not directory.is_dir():
                continue
            if is_applied(directory, patch):
                run(['git', 'apply', '--reverse', str(patch)], cwd=directory, check=False)
                reverted += 1
                print(f"  reverted {plugin['id']}/{patch.name}")
    print(f"patches: {reverted} reverted")
    return 0


def lint_patches() -> list[str]:
    """Reject a patch whose own added lines cannot compile as format strings.

    git apply cannot see this: the patch is syntactically perfect and applies
    cleanly, and the failure only appears in a Windows build ten minutes later.
    That is exactly how it reached CI once, so it is checked here instead.
    """
    failures = []
    for plugin in plugins():
        for patch in patch_files(plugin['id']):
            for number, field in malformed_format_fields(patch.read_text(encoding='utf-8')):
                failures.append(
                    f"{plugin['id']}/{patch.name}:{number}: a format placeholder contains a "
                    f"quote ({{{field}}}); the escaped quote belongs outside the braces "
                    f'(\"{{}}\" not \"{{\"}}) or fmt fails to compile it'
                )
    return failures


# Two more ways a fix can be written down and still not exist. Both were live in
# this repository at once: the changelog and both READMEs named a patch that had
# just been deleted, and the changelog and the pitfalls notes quoted the "after"
# source of that same deleted patch. Nothing caught either, because a document is
# not compiled and this tool only ever looked at patches.
#
# A name containing '...' is how the documents abbreviate a path on purpose, so
# those are skipped rather than reported.
DOCUMENTS = ('README.md', 'README_EN.md', 'CHANGELOG.md', 'docs/PITFALLS.md')
PATCH_PATH = re.compile(r'Code/plugins/patches/([A-Za-z0-9_]+)/([A-Za-z0-9_.\-]+\.patch)')
SOURCE_MARKER = re.compile(r'//\s*plugins/([A-Za-z0-9_]+)/(\S+?)\(修复后\)')
LINE_SUFFIX = re.compile(r':\d+(?:-\d+)?$')


def document_lines() -> list[tuple[str, int, str]]:
    """Every line of every document that quotes a repository path, with its number."""
    lines = []
    for name in DOCUMENTS:
        path = ROOT / name
        if not path.is_file():
            continue
        text = path.read_text(encoding='utf-8', errors='replace')
        for number, line in enumerate(text.splitlines(), 1):
            lines.append((name, number, line))
    return lines


def patched_targets() -> dict[str, set[str]]:
    """Every file each plugin has a patch for, by plugin id."""
    targets = {}
    for plugin in plugins():
        recorded = targets.setdefault(plugin['id'], set())
        for patch in patch_files(plugin['id']):
            recorded.update(patch_targets(patch))
    return targets


def unrecorded_patch_paths() -> list[str]:
    """Patch paths a document names that this repository does not have."""
    failures = []
    for name, number, line in document_lines():
        for plugin_id, file_name in PATCH_PATH.findall(line):
            if '...' in file_name:
                continue
            if not (ROOT / PATCH_DIR_REL / plugin_id / file_name).is_file():
                failures.append(
                    f"{name}:{number} names {plugin_id}/{file_name}, which is not a patch "
                    f"this repository has - the document and the patch directory disagree"
                )
    return failures


def unrecorded_source_markers() -> list[str]:
    """'after the fix' markers quoting a file that plugin has no patch for."""
    failures = []
    recorded = patched_targets()
    for name, number, line in document_lines():
        for plugin_id, target in SOURCE_MARKER.findall(line):
            target = LINE_SUFFIX.sub('', target)
            if target not in recorded.get(plugin_id, set()):
                failures.append(
                    f"{name}:{number} quotes // plugins/{plugin_id}/{target} as the state "
                    f"after the fix, but no {plugin_id} patch edits that file - the marker "
                    f"outlived the patch it came from"
                )
    return failures


def cmd_verify(_args) -> int:
    """Every patch must apply to the pinned commit, edit a file the plugin
    actually compiles, and be the patch the documents say it is. Run against a
    pristine tree (CI does)."""
    failures = lint_patches()
    failures.extend(unrecorded_patch_paths())
    failures.extend(unrecorded_source_markers())
    total = 0
    for plugin in plugins():
        directory = submodule_dir(plugin)
        if directory.is_dir():
            failures.extend(uncompiled_patch_targets(plugin))
        for patch in patch_files(plugin['id']):
            total += 1
            if not directory.is_dir():
                failures.append(f"{plugin['id']}: submodule not checked out, cannot verify {patch.name}")
                continue
            # Checked against the index, not the working tree. The index holds the
            # pinned content even when the patch is already applied to the
            # working tree, so "already applied" cannot hide a patch that has
            # stopped matching the pin.
            result = run(['git', 'apply', '--check', '--cached', str(patch)],
                         cwd=directory, check=False)
            if result.returncode != 0:
                failures.append(
                    f"{plugin['id']}/{patch.name} does not apply to the pinned commit "
                    f"- the submodule was re-pinned without rewriting the patch"
                )
    if total == 0:
        print('no patches recorded')
        return 0
    for failure in failures:
        print('  -', failure)
    if failures:
        print(f"PATCH VERIFY FAILED ({len(failures)})")
        return 1
    print(f"PATCH VERIFY OK\n  {total} patch(es) apply to their pinned commits")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command')
    for name, handler in (
        ('apply', cmd_apply),
        ('check', cmd_check),
        ('revert', cmd_revert),
        ('verify', cmd_verify),
    ):
        sub.add_parser(name).set_defaults(func=handler)
    args = parser.parse_args()
    if not getattr(args, 'func', None):
        parser.print_help()
        return 2
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
