#!/usr/bin/env python3
"""Keep OStimNG loadable on every Skyrim runtime this project supports.

OStimNG is a prerequisite for OStim Together, and it resolves engine addresses
through the SKSE address library. A RELOCATION_ID whose SE or AE id is absent
from the library for the runtime actually running is not a soft failure:
REL::ID() aborts the game at load (REL/IDDB.cpp's report_id_lookup_failure).
So "OStimNG supports 1.5.x through 1.7.x" is a claim about id coverage in the
shipped libraries, and that is what this checks rather than trusting it.

RELOCATION_ID(a, b) names one id per id namespace: a for the pre-AE (1.5.x)
libraries, b for the AE ones (1.6.x format 2 and 1.7.x format 5). Both are
checked against every library of their family, because a plugin that loads on
1.5.97 and aborts on 1.6.1170 is exactly the regression this exists to stop.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'Tools' / 'Scripts'))

from gen_ae_to_se_map import parse_bin  # noqa: E402

LIB_DIR = ROOT / 'GameFiles' / 'Skyrim' / 'SKSE' / 'Plugins'
NG_SRC = ROOT / 'plugins' / 'OStimNG' / 'skse'

# RELOCATION_ID(se, ae) is the only per-namespace form OStimNG uses. REL::ID(n)
# and REL::Offset(n) carry a single id and are namespace-ambiguous, so they are
# reported separately rather than guessed at.
RELOCATION = re.compile(r'RELOCATION_ID\(\s*(\d+)\s*,\s*(\d+)\s*\)')
SINGLE = re.compile(r'REL::(?:ID|Offset)\(\s*(\d+)\s*\)')

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')


def sources() -> list[pathlib.Path]:
    if not NG_SRC.is_dir():
        return []
    return sorted(p for p in NG_SRC.rglob('*') if p.suffix in ('.h', '.cpp'))


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def sites() -> tuple[list[tuple[int, int, str, int]], list[tuple[int, str, int]]]:
    pairs: list[tuple[int, int, str, int]] = []
    singles: list[tuple[int, str, int]] = []
    for path in sources():
        text = strip_comments(path.read_text(encoding='utf-8', errors='ignore'))
        rel = path.relative_to(NG_SRC.parent).as_posix()
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

    if not sources():
        print('OSTIMNG VERSION CHECK SKIPPED')
        print('  plugins/OStimNG is not checked out; run git submodule update --init')
        return 0

    print(f'  {len(pairs)} RELOCATION_ID site(s), {len(singles)} single-id site(s)')
    print(f'  {len(pre)} pre-AE librar(y|ies), {len(ae)} AE librar(y|ies)')

    failures: list[str] = []

    if not pre:
        failures.append(f'no pre-AE address library found under {LIB_DIR}')
    if not ae:
        failures.append(f'no AE address library found under {LIB_DIR}')

    # A 1.5.x runtime aborts on a missing SE id; an AE one on a missing AE id.
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

    if singles:
        # Not fatal: REL::ID is namespace-ambiguous here, so it is reported so a
        # reader can decide rather than being silently counted as covered.
        print()
        print('  note: single-id sites are namespace-ambiguous and not checked:')
        for value, path, line in singles[:10]:
            print(f'    {path}:{line}  id {value}')

    if failures:
        print()
        print(f'OSTIMNG VERSION CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print()
    print('OSTIMNG VERSION CHECK OK')
    print(f'  every RELOCATION_ID has an id in every supported runtime library')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
