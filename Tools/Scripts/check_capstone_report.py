#!/usr/bin/env python3
"""Keep the 1.5.97 matching report consistent with the map that ships.

`Tools/ida/st_capstone_report.json` is the intermediate log of the similarity
matching that produced the 1.5.97 offsets: for each AE id it records a candidate
`se_rva` and the similarity the matcher scored it at, or a reason it found none
("no bracket", "ambiguous 0.84 vs 0.80", "low similarity 0.62"). It is evidence,
not a shipped artifact - the shipped artifact is
`versionlib-ae-to-se-1-5-97-0.map`, built from the override tables.

Nothing compared the two, so a reader had no way to tell which candidates were
accepted, which were rejected, and whether a rejection was deliberate:

  * 47 of the 49 resolved candidates agree with the shipped map;
  * 2 do not - id 37511 (`s_payGoldToContainer`, similarity 0.82) and id 47303
    (`actorHasEquippedRangedWeapon_m`, 0.74).

Both were checked against the real 1.5.97 executable, and in both the shipped map
is right and the report's candidate is wrong:

  37511  the map's 0x5e5dd0 is a thin forwarder that reads rcx/rdx/r8d and tail
         calls - the three arguments the client passes (`this`, container, amount).
         The candidate 0x5e3a50 ignores rdx entirely: a different function.
  47303  the map's 0x7bcb40 tests `cmpb $0x29,0x1a(%rax)` (form type 0x29) and a
         range of 7 - the ranged-weapon test the client calls. The candidate
         0x7bcaf0 tests a different form type.

The pattern is not luck: every disagreement sits in the two lowest similarity
bands, and every candidate scored 0.90 or above agrees (31 of 31). So the
rejections are the matcher's low-confidence guesses being overruled, and the gate
below records them explicitly rather than leaving a silent 2-of-49 discrepancy.

    python Tools/Scripts/check_capstone_report.py          # the gate
    python Tools/Scripts/check_capstone_report.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
REPORT = ROOT / 'Tools' / 'ida' / 'st_capstone_report.json'
SHIPPED_MAP = ROOT / 'GameFiles' / 'Skyrim' / 'SKSE' / 'Plugins' / 'versionlib-ae-to-se-1-5-97-0.map'

# Candidates the shipped map deliberately does not use, with the reason each was
# overruled. Listed so a third disagreement cannot appear unnoticed, and so the
# two known ones are not mistaken for unresolved errors.
REJECTED: dict[int, str] = {
    37511: 'similarity 0.82; the shipped offset is the forwarder matching the client call signature',
    47303: 'similarity 0.74; the shipped offset is the one testing form type 0x29',
}

# A candidate at or above this similarity was accepted in every case observed, so
# a disagreement above it means the matcher and the map diverged somewhere new.
TRUSTED_SIMILARITY = 0.90


def report() -> dict[str, dict]:
    if not REPORT.is_file():
        return {}
    return json.loads(REPORT.read_text(encoding='utf-8'))


def shipped() -> dict[int, int]:
    out: dict[int, int] = {}
    if not SHIPPED_MAP.is_file():
        return out
    for line in SHIPPED_MAP.read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        parts = line.split()
        if len(parts) >= 2:
            try:
                out[int(parts[0], 0)] = int(parts[1], 0)
            except ValueError:
                continue
    return out


def similarity(status: str) -> float | None:
    match = re.search(r'sim=([\d.]+)', status)
    return float(match.group(1)) if match else None


def check(failures: list[str], warnings: list[str]) -> int:
    entries = report()
    if not entries:
        failures.append(f'{REPORT.relative_to(ROOT).as_posix()} is missing or empty')
        return 0

    shipped_map = shipped()
    if not shipped_map:
        failures.append(
            f'{SHIPPED_MAP.relative_to(ROOT).as_posix()} is missing or empty; the report candidates '
            f'cannot be compared against what ships')
        return 0

    checked = 0
    for raw_id, entry in sorted(entries.items(), key=lambda kv: int(kv[0])):
        ae_id = int(raw_id)
        candidate = entry.get('se_rva')
        if candidate is None:
            # No candidate: the status must say why, so a silent null is visible.
            status = entry.get('status', '')
            if not status:
                failures.append(f'{raw_id}: no candidate and no status explaining why')
            continue

        checked += 1
        actual = shipped_map.get(ae_id)
        if actual is None:
            failures.append(
                f'{raw_id} ({entry.get("var")}): the report resolved 0x{candidate:x} but the id is '
                f'not in the shipped map at all')
            continue

        if actual == candidate:
            if ae_id in REJECTED:
                failures.append(
                    f'{raw_id}: this gate records it as rejected ({REJECTED[ae_id]}), but the map now '
                    f'agrees with the candidate; drop the entry rather than leaving it stale')
            continue

        # A disagreement: either recorded, or a new one.
        if ae_id not in REJECTED:
            sim = similarity(entry.get('status', ''))
            detail = f' (similarity {sim})' if sim is not None else ''
            extra = ''
            if sim is not None and sim >= TRUSTED_SIMILARITY:
                extra = (f'; every candidate at or above {TRUSTED_SIMILARITY} has been accepted, so '
                         f'this one disagreeing is new')
            failures.append(
                f'{raw_id} ({entry.get("var")}): the report resolved 0x{candidate:x}{detail} but the '
                f'shipped map has 0x{actual:x}{extra}. Either the map is wrong or the candidate was '
                f'rejected - if rejected, record it in REJECTED with the reason')

    # A recorded rejection that no longer disagrees outlives its reason.
    for ae_id, reason in REJECTED.items():
        entry = entries.get(str(ae_id))
        if entry is None:
            failures.append(
                f'id {ae_id} is recorded as rejected ({reason}) but the report no longer lists it; '
                f'drop the entry rather than leaving it to cover nothing')

    # The correlation the rejections rest on: low similarity is where the matcher
    # is overruled. If a high-similarity candidate ever disagrees, the reasoning
    # above no longer holds and this should be re-examined rather than trusted.
    bands: dict[str, list[bool]] = {}
    for raw_id, entry in entries.items():
        candidate = entry.get('se_rva')
        if candidate is None:
            continue
        sim = similarity(entry.get('status', ''))
        if sim is None:
            continue
        band = '>=0.90' if sim >= TRUSTED_SIMILARITY else '<0.90'
        bands.setdefault(band, []).append(shipped_map.get(int(raw_id)) == candidate)
    for band, outcomes in sorted(bands.items()):
        if band == '>=0.90' and not all(outcomes):
            failures.append(
                f'{sum(1 for o in outcomes if not o)} candidate(s) at similarity >= '
                f'{TRUSTED_SIMILARITY} disagree with the shipped map; that band was previously '
                f'unanimous, so the rejections recorded here need re-deriving')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        # --list is documentation: it names what the check compares and the counts
        # it starts from. It used to print three *verdicts* ("every resolved
        # candidate either agrees...", "every recorded rejection still disagrees",
        # "no candidate at similarity >= X disagrees") as literals without ever
        # calling check() - the outcome of a comparison it had not run. The rules
        # are named below as rules, and the verdict only exists on the run that
        # computes it.
        entries = report()
        shipped_map = shipped()
        resolved = [e for e in entries.values() if e.get('se_rva') is not None]
        agree = sum(1 for k, e in entries.items()
                    if e.get('se_rva') is not None and shipped_map.get(int(k)) == e['se_rva'])
        print('this gate compares (run without --list for the verdict):')
        print('  each resolved candidate against the shipped map, allowing recorded rejections')
        print('  each recorded rejection against the report, so it has not outlived its reason')
        print(f'  candidates at similarity >= {TRUSTED_SIMILARITY} against the shipped map, where '
              f'that band is expected to stay unanimous')
        print(f'  {len(entries)} report entries, {len(resolved)} with a candidate, {agree} agreeing')
        for ae_id, reason in sorted(REJECTED.items()):
            print(f'    rejected {ae_id}: {reason}')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    if warnings:
        for warning in warnings:
            print(f'  note: {warning}')

    if failures:
        print(f'CAPSTONE REPORT CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('CAPSTONE REPORT CHECK OK')
    print(f'  {checked} resolved candidate(s) agree with the shipped map or are recorded as rejected')
    return 0


if __name__ == '__main__':
    sys.exit(main())
