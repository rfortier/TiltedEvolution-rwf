#!/usr/bin/env python3
"""Keep a durable copy of the Papyrus compiler's source inside this repository.

Code/plugins/papyrus/ compiles this project's scripts with a *released binary*
of russo-2025/papyrus-compiler, pinned by tag and SHA-256. That pin is a
dependency on one GitHub release asset in a repository this project does not
control. If the repository or the release disappears, the ability to build a
Papyrus compiler disappears with it - and that matters here for a reason that is
not hypothetical.

The pinned release cannot compile one of this project's own shipping scripts.
SkyrimTogetherVerifyLaunchScript.psc contains "\n" escapes, and the scanner in
release 2026.03.15 has no escape handling at all: it emits the two characters
backslash and n where the shipped .pex has a real newline. The fix exists only
in commits newer than that tag, which have no release. So the compiler this
project depends on is known-broken for its own input, the fix is unreleased, and
nothing but upstream's repository holds it.

This tool copies the tracked source of the upstream checkout at HEAD into
snapshots/tools/papyrus-compiler/, which is ordinary content of this repository
and therefore survives in its history.

    snapshot   refresh the archive from a local upstream checkout
    verify     check the archive matches its record (the CI gate)

What is excluded, and why:

  bin/         Bethesda's PapyrusCompiler.exe, PapyrusAssembler.exe and
               PCompiler.dll, committed by upstream. Redistributing them from a
               GPL-3 repository is not this project's call to make.
  docs/        a 4.7 MB README screenshot and compiler benchmark tables.
  test-files/  compiled test output; upstream's own AGENTS.md calls it "not
               source code".

Excluding bin/ has a consequence worth stating plainly: this archive cannot
produce papyrus.exe. Building it needs the V toolchain, which is a far larger
dependency than the download it replaces. The archive preserves the *source*, so
a fix remains possible; it is not a build input and must never be treated as one.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE_DIR = 'snapshots/tools/papyrus-compiler'
UPSTREAM_FILE = 'UPSTREAM.json'
UPSTREAM_URL = 'https://github.com/russo-2025/papyrus-compiler.git'

# Directories dropped from the archive. See the module docstring.
EXCLUDED_PREFIXES = ('bin/', 'docs/', 'test-files/')


def run(args: list[str], cwd: Path | None = None, check: bool = True) -> str:
    result = subprocess.run(args, cwd=str(cwd or ROOT), capture_output=True, text=True)
    if check and result.returncode != 0:
        raise SystemExit(f"command failed: {' '.join(args)}\n{result.stderr.strip()}")
    return result.stdout


def normalized(path: Path) -> bytes:
    """Content with line endings forced to LF.

    The same commit checks out as CRLF or LF depending on the machine's git
    settings, so a byte comparison would report drift that is not drift.
    """
    return path.read_bytes().replace(b'\r\n', b'\n').replace(b'\r', b'\n')


def tracked_files(source: Path) -> list[str]:
    """Tracked files of the upstream checkout, minus the excluded trees."""
    out = run(['git', '-C', str(source), 'ls-files'])
    return [
        line for line in out.splitlines()
        if line and not line.startswith(EXCLUDED_PREFIXES)
    ]


def digest_of(source: Path, rels: list[str]) -> tuple[str, int]:
    digest = hashlib.sha256()
    count = 0
    for rel in sorted(rels):
        path = source / rel
        if not path.is_file():
            continue
        digest.update(rel.encode('utf-8'))
        digest.update(b'\0')
        digest.update(normalized(path))
        digest.update(b'\0')
        count += 1
    return digest.hexdigest(), count


def ignored_in_archive(rels: list[str]) -> list[str]:
    """Archived files that git would refuse to commit.

    The archive carries upstream's own .gitignore, and a nested .gitignore wins
    over anything written at the repository root - so a rule like `*.bat` in
    there silently keeps a file out of `git add snapshots/` while it sits
    happily on disk. `snapshot` copies from the checkout and never consults git's
    ignore rules, and `verify` reads the filesystem, so both agree the archive is
    complete while the committed one is missing a file.

    This repository has already lost 22 plugin snapshot files to an ignore rule,
    which is why it is checked rather than trusted.
    """
    if not rels:
        return []
    prefix = ARCHIVE_DIR + '/'

    # Paths are passed as arguments, not on stdin. `git check-ignore --stdin`
    # returned nothing at all here, even for a file a plain argv call reports as
    # ignored, so the stdin form would have reported a clean archive every time.
    # Batched because a command line has a length limit and this list can grow.
    found: list[str] = []
    batch_size = 200
    for start in range(0, len(rels), batch_size):
        batch = [prefix + rel for rel in rels[start:start + batch_size]]
        result = subprocess.run(
            ['git', '-C', str(ROOT), 'check-ignore'] + batch,
            capture_output=True, text=True,
        )
        # Exit 1 means "nothing in this batch is ignored", which is the good case.
        if result.returncode not in (0, 1):
            raise SystemExit(f"git check-ignore failed: {result.stderr.strip()}")
        found.extend(
            line[len(prefix):] for line in result.stdout.splitlines()
            if line.startswith(prefix)
        )
    return found


def cmd_snapshot(args) -> int:
    source = Path(args.source).resolve()
    if not (source / '.git').exists():
        raise SystemExit(f"{source} is not a git checkout of the compiler")

    target = ROOT / ARCHIVE_DIR
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)

    rels = tracked_files(source)
    copied = 0
    for rel in sorted(rels):
        path = source / rel
        if not path.is_file():
            continue
        destination = target / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, destination)
        copied += 1

    content_sha, _ = digest_of(source, rels)
    commit = run(['git', '-C', str(source), 'rev-parse', 'HEAD']).strip()
    commit_date = run(['git', '-C', str(source), 'log', '-1', '--format=%ci']).strip()
    describe = run(['git', '-C', str(source), 'describe', '--tags'], check=False).strip()

    record = {
        'id': 'papyrus-compiler',
        'upstream': UPSTREAM_URL,
        'commit': commit,
        'commitDate': commit_date,
        'describe': describe,
        'files': copied,
        'contentSha256': content_sha,
        'excluded': list(EXCLUDED_PREFIXES),
        'upstreamDocDrift': (
            'AGENTS.md is archived verbatim, drift included. Line 35 says the test header '
            'stubs live in modules/tests/psc_deps/ with "83 Skyrim base class header stubs"; '
            'no such path exists. Line 152 of the same file says modules/tests/sources/psc_deps/, '
            'which is where the tests actually look (projects_test.v:28, '
            'selective_headers_loading_test.v:18). Whether it holds 83 files cannot be checked '
            'here: that path is a submodule and is not in this archive. Recorded rather than '
            'fixed, because editing the archive would stop it being a copy of the commit above. '
            'See docs/PAPYRUS-SOURCE-ARCHIVE.md.'
        ),
        'why': (
            'Source durability copy of the Papyrus compiler this project builds its scripts with. '
            'Not a build input and cannot produce papyrus.exe: bin/ is excluded because upstream '
            'commits Bethesda binaries there. Kept because the pinned release 2026.03.15 cannot '
            'compile SkyrimTogetherVerifyLaunchScript.psc, and the fix for that is unreleased.'
        ),
    }
    (target / UPSTREAM_FILE).write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')

    print(f"  archived {copied:>4} files  {commit[:8]}  {describe}")
    print(f"  contentSha256 {content_sha[:16]}")

    ignored = ignored_in_archive(sorted(rels))
    if ignored:
        print()
        print(f"  WARNING: {len(ignored)} archived file(s) are ignored by git and")
        print("  would not be committed, so the archive on disk would not be the")
        print("  archive in the repository:")
        for rel in ignored[:5]:
            print(f"    {rel}")
        print("  'verify' fails on this, so the gate will catch it - but the file")
        print("  needs a negation in the archive's own .gitignore to be committed.")

    print()
    print('archive refreshed; commit snapshots/ with the record')
    return 0


def cmd_verify(_args) -> int:
    target = ROOT / ARCHIVE_DIR
    record_path = target / UPSTREAM_FILE

    if not record_path.is_file():
        print(f"{ARCHIVE_DIR}/{UPSTREAM_FILE} is missing; run 'snapshot'")
        return 1

    record = json.loads(record_path.read_text(encoding='utf-8'))

    # Recompute from the archive itself. The record and the tree it describes
    # are committed together, so a hand-edit of either one is what this catches.
    rels = [
        str(p.relative_to(target)).replace('\\', '/')
        for p in sorted(target.rglob('*'))
        if p.is_file() and p.name != UPSTREAM_FILE
    ]

    digest = hashlib.sha256()
    for rel in sorted(rels):
        digest.update(rel.encode('utf-8'))
        digest.update(b'\0')
        digest.update(normalized(target / rel))
        digest.update(b'\0')
    actual = digest.hexdigest()

    if actual != record.get('contentSha256'):
        print(f"archive content does not match its record:")
        print(f"  recorded {record.get('contentSha256')}")
        print(f"  actual   {actual}")
        return 1

    if len(rels) != record.get('files'):
        print(f"file count differs: recorded {record.get('files')}, found {len(rels)}")
        return 1

    leaked = [r for r in rels if r.startswith(EXCLUDED_PREFIXES)]
    if leaked:
        print(f"excluded path present in the archive: {leaked[0]}")
        return 1

    ignored = ignored_in_archive(rels)
    if ignored:
        print(f"{len(ignored)} archived file(s) are ignored by git, so the archive in the")
        print("repository is not the archive on disk:")
        for rel in ignored[:5]:
            print(f"  {rel}")
        print("Add a negation to the archive's own .gitignore - a rule at the repository")
        print("root loses to a nested .gitignore, so it will not work.")
        return 1

    print(f"papyrus-compiler archive OK: {len(rels)} files, {record.get('commit', '?')[:8]}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)

    snap = sub.add_parser('snapshot', help='refresh the archive from a local checkout')
    snap.add_argument('--source', default='../papyrus-compiler',
                      help='path to the upstream checkout (default: ../papyrus-compiler)')
    snap.set_defaults(func=cmd_snapshot)

    ver = sub.add_parser('verify', help='check the archive against its record')
    ver.set_defaults(func=cmd_verify)

    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
