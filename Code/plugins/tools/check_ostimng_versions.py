#!/usr/bin/env python3
"""Keep every address-library-using plugin loadable on every supported runtime.

These plugins resolve engine addresses through the SKSE address library. A
RELOCATION_ID whose SE or AE id is absent from the library for the runtime
actually running is not a soft failure: REL::ID() aborts the game at load
(REL/IDDB.cpp's report_id_lookup_failure). So "supports 1.5.x through 1.7.x" is
a claim about id coverage in the shipped libraries, and that is what this
checks rather than trusting it.

RELOCATION_ID(a, b) names one id per id namespace: a for the pre-AE (1.5.x)
libraries, b for the AE ones (1.6.x format 2 and 1.7.x format 5). Both are
checked against every library of their family, because a plugin that loads on
1.5.97 and aborts on 1.6.1170 is exactly the regression this exists to stop.

The file keeps its original name for its git history and its CI step, but the
scope is no longer one plugin. It scanned only OStimNG while OStimTogether
carried eight more RELOCATION_ID sites - four distinct id pairs - that nothing
checked at all, which is the shape of gap this kind of gate is supposed to
close: the check was green, and green because it was not looking. Every scanned
plugin is now listed in SCANNED, and a plugin that is checked out but
contributes no site is itself a failure, so a stale scan path cannot silently
reduce this check to nothing.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'Tools' / 'Scripts'))

from gen_ae_to_se_map import parse_bin  # noqa: E402

LIB_DIR = ROOT / 'GameFiles' / 'Skyrim' / 'SKSE' / 'Plugins'
PLUGINS_DIR = ROOT / 'plugins'

# The plugins that reach engine addresses through the address library. OStimNG is
# a prerequisite rather than a companion plugin, but it is the largest user and
# the reason this check exists; OStimTogether carries its own eight sites and
# used to be outside the scan entirely, which is the gap that let a bad id in
# one plugin pass while the other was checked.
#
# Every entry is a directory under plugins/ whose sources are scanned. A plugin
# that gains RELOCATION_ID usage later is picked up by being added here - the
# scan cannot find it on its own, because a submodule that is not checked out
# looks identical to one with no sites.
SCANNED: dict[str, str] = {
    'OStimNG/skse': 'OStimNG (prerequisite)',
    'OStimTogether/src': 'OStimTogether',
}

# RELOCATION_ID(se, ae) is the only per-namespace form these plugins use.
# REL::ID(n) and REL::Offset(n) carry a single id and are namespace-ambiguous,
# so they are reported separately rather than guessed at.
RELOCATION = re.compile(r'RELOCATION_ID\(\s*(\d+)\s*,\s*(\d+)\s*\)')
SINGLE = re.compile(r'REL::(?:ID|Offset)\(\s*(\d+)\s*\)')

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')


def roots() -> list[tuple[str, pathlib.Path]]:
    """(display name, source root) for every scanned plugin that is checked out."""
    found: list[tuple[str, pathlib.Path]] = []
    for rel, label in SCANNED.items():
        path = PLUGINS_DIR / rel
        if path.is_dir():
            found.append((label, path))
    return found


def sources() -> list[tuple[str, pathlib.Path]]:
    """(display name, file) for every scanned source file."""
    out: list[tuple[str, pathlib.Path]] = []
    for label, root in roots():
        out.extend((label, p) for p in sorted(root.rglob('*')) if p.suffix in ('.h', '.cpp', '.hpp'))
    return out


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def sites() -> tuple[list[tuple[int, int, str, int]], list[tuple[int, str, int]]]:
    pairs: list[tuple[int, int, str, int]] = []
    singles: list[tuple[int, str, int]] = []
    for label, path in sources():
        text = strip_comments(path.read_text(encoding='utf-8', errors='ignore'))
        rel = f'{label}:{path.name}'
        for match in RELOCATION.finditer(text):
            line = text[:match.start()].count('\n') + 1
            pairs.append((int(match.group(1)), int(match.group(2)), rel, line))
        for match in SINGLE.finditer(text):
            line = text[:match.start()].count('\n') + 1
            singles.append((int(match.group(1)), rel, line))
    return pairs, singles


def libraries() -> tuple[dict[str, set[int]], dict[str, set[int]]]:
    """(pre-AE libraries, AE libraries), each as name -> id set."""
    pre: dict[str, set[int]] = {}
    ae: dict[str, set[int]] = {}
    if not LIB_DIR.is_dir():
        return pre, ae
    for path in sorted(LIB_DIR.glob('version-*.bin')):
        if path.name.startswith('versionlib-'):
            continue
        pre[path.name] = set(parse_bin(path))
    for path in sorted(LIB_DIR.glob('versionlib-*.bin')):
        # The ae-to-se maps are translation tables, not id libraries.
        if path.name.endswith('.map'):
            continue
        ae[path.name] = set(parse_bin(path))
    return pre, ae


def main() -> int:
    pairs, singles = sites()
    pre, ae = libraries()
    checked = roots()

    if not checked:
        print('PLUGIN VERSION CHECK SKIPPED')
        print(f'  none of {sorted(SCANNED)} is checked out; run git submodule update --init')
        return 0

    print(f'  plugins scanned: {", ".join(label for label, _ in checked)}')
    print(f'  {len(pairs)} RELOCATION_ID site(s), {len(singles)} single-id site(s)')
    print(f'  {len(pre)} pre-AE librar(y|ies), {len(ae)} AE librar(y|ies)')

    failures: list[str] = []

    if not pre:
        failures.append(f'no pre-AE address library found under {LIB_DIR}')
    if not ae:
        failures.append(f'no AE address library found under {LIB_DIR}')

    # A 1.5.x runtime aborts on a missing SE id; an AE one on a missing AE id.
    # REL::ID() aborts the game at load (REL/IDDB.cpp's report_id_lookup_failure),
    # so an id absent from the running library is not a soft failure.
    for name, ids in sorted(pre.items()):
        missing = sorted({se for se, _ae, _f, _l in pairs if se not in ids})
        if missing:
            where = [f'{f}:{l}' for se, _a, f, l in pairs if se in missing][:3]
            failures.append(
                f'{name}: {len(missing)} SE id(s) absent, e.g. {missing[:5]} at {where}')

    for name, ids in sorted(ae.items()):
        missing = sorted({a for _se, a, _f, _l in pairs if a not in ids})
        if missing:
            where = [f'{f}:{l}' for _s, a, f, l in pairs if a in missing][:3]
            failures.append(
                f'{name}: {len(missing)} AE id(s) absent, e.g. {missing[:5]} at {where}')

    # A plugin that is checked out but contributes no site is either a new
    # plugin this list has not learned about, or a scan path that went stale
    # after a submodule reorganisation. Both are worth knowing about, because
    # the second silently reduces this check to nothing.
    for label, root in checked:
        if not any(label in f for _se, _a, f, _l in pairs):
            failures.append(
                f'{label}: checked out at {root.relative_to(ROOT).as_posix()} but contributed no '
                f'RELOCATION_ID site; either its scan path is stale or it has no address-library '
                f'use, in which case drop it from SCANNED rather than checking nothing')

    if singles:
        # Not fatal: REL::ID is namespace-ambiguous here, so it is reported so a
        # reader can decide rather than being silently counted as covered.
        print()
        print('  note: single-id sites are namespace-ambiguous and not checked:')
        for value, path, line in singles[:10]:
            print(f'    {path}:{line}  id {value}')

    if failures:
        print()
        print(f'PLUGIN VERSION CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print()
    print('PLUGIN VERSION CHECK OK')
    print(f'  every RELOCATION_ID in {len(checked)} plugin(s) has an id in every supported runtime library')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
