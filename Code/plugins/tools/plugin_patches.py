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
    verify  apply --check every patch against a pristine tree (the gate)

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
            if '"' in field:
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


def cmd_verify(_args) -> int:
    """Every patch must apply to the pinned commit, and none may be unapplied
    without being recorded. Run against a pristine tree (CI does)."""
    failures = lint_patches()
    total = 0
    for plugin in plugins():
        directory = submodule_dir(plugin)
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
