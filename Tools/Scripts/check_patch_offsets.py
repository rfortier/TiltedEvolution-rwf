#!/usr/bin/env python3
"""Keep the checked-in patch offsets and the code that uses them in agreement.

`GamePatch::Site` carries two offsets: the one measured on 1.6.x, and the 1.5.x
one. The 1.5.x value is not guessed - it comes from
`Tools/ida/st_patch_offsets_1597.py`, which aligns the two disassemblies
instruction by instruction and refuses any candidate whose mnemonic or length
disagrees. Its output, `Tools/ida/st_patch_offsets_1597.tsv`, is checked in.

Nothing connected that file to the code. The offsets reach the client as
literals in `GamePatch::At(pAnchor, {modern, legacy, "1.5.97"}, ...)` calls
spread across eight files, and the evidence file is prose to the compiler. So a
row could be regenerated with a different number, or a call site edited to a
number no run ever produced, and both sides would keep their own story:

  * the tsv is the *evidence* - it says which 1.5.x offset a measurement
    supports, and every row without one records why ("instruction not in an
    aligned run", "id not mapped on 1.5.97");
  * the code is the *claim* - it decides whether the patch is applied at all.

The dangerous direction is a call site carrying a 1.5.x offset that no row
backs, because that writes bytes into a running game on the strength of a
number nobody measured. The other direction - a row with a 1.5.x offset that
the code does not use - is not a defect (dropping a cosmetic patch on 1.5.x is
deliberate, and "frame end" does exactly that) but it is worth reporting, since
it usually means the row was refreshed and the call site was not.

So this gate checks the claim against the evidence, in one direction strictly
and the other as a warning:

    python Tools/Scripts/check_patch_offsets.py          # the gate
    python Tools/Scripts/check_patch_offsets.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'Tools' / 'ida' / 'st_patch_offsets_1597.tsv'
CLIENT = ROOT / 'Code' / 'client'

# The build every 1.5.x offset has to have been measured on. `At()` only applies
# a legacy offset when the running version matches this prefix, so a site whose
# provenance says something else is applied to a build it was not measured on.
LEGACY_PREFIX = '1.5.97'

# Call sites whose 1.5.x offset is deliberately absent: the measurement produced
# no number for them, and the tsv records why. Listed with the reason so a new
# site cannot be added here just to silence the gate.
NO_LEGACY_EXPECTED: dict[str, str] = {
    'window style': 'the 1.5.x instruction is not in an aligned run',
    'skip startup movie': 'the 1.5.x instruction is not in an aligned run',
    'tasklet thread names': 'the anchor id is not mapped on 1.5.97',
    # The tsv used to record an aligned 0x9 here while the code deliberately passed
    # none, so the exemption below covered a row that contradicted the call site.
    # The table now records no offset for it and says why (the generator's own input
    # already called the site "1.6 only"), which turns restoring that offset in the
    # code from a silently accepted edit into a failure: the row says there is none.
    'frame end': 'the pre-AE path hooks the anchor itself instead of patching inside it',
}

# Sites where the tsv has a 1.5.x offset that the code deliberately does not use.
# Empty: every such row is now either used by the code or records no offset. Kept
# as the mechanism, and the loop below fails when an entry outlives its row.
UNUSED_LEGACY_OK: dict[str, str] = {}

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')

# The full call: `GamePatch::At(pAnchor, {0x84E, 0x929, "1.5.97"}, "stats menu appear")`.
# The site name is the string AFTER the closing brace of the offset list, so the
# whole call has to be matched - the braces alone do not carry it.
AT_CALL = re.compile(r'GamePatch::At\s*\(\s*(\w+)\s*,\s*\{([^}]*)\}\s*(?:,\s*"([^"]*)")?\s*\)', re.S)
# `static constexpr NullCheck kModern{0x374, 0x379, 0x50, 0x138};`
CONST_STRUCT = re.compile(r'constexpr\s+(\w+)\s+(\w+)\s*\{([^}]*)\}')


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


def evidence() -> dict[str, dict]:
    """The tsv, keyed by the site name it records."""
    out: dict[str, dict] = {}
    if not EVIDENCE.is_file():
        return out
    for line in EVIDENCE.read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) < 5:
            continue
        ae_id, ae_off, se_off, note, site = parts[0], parts[1], parts[2], parts[3], parts[4]
        out[site.strip()] = {
            'ae_id': int(ae_id),
            'ae_offset': ae_off.strip(),
            'se_offset': None if se_off.strip() == '-' else se_off.strip(),
            'note': note.strip(),
            'site': site.strip(),
        }
    return out


def sources() -> list[pathlib.Path]:
    return sorted(p for p in CLIENT.rglob('*.cpp')) + sorted(p for p in CLIENT.rglob('*.h'))


def constants(text: str) -> dict[str, list[str]]:
    """Every `constexpr T name{...}` in a file, name -> the field list."""
    out: dict[str, list[str]] = {}
    for _type, name, body in CONST_STRUCT.findall(strip_comments(text)):
        out[name] = [f.strip() for f in body.split(',') if f.strip()]
    return out


def resolve(token: str, consts: dict[str, list[str]], field: str) -> str | None:
    """A numeric literal, a sum of literals, or `kName.field`.

    The struct fields are ordered jumpOut, jumpBack, slot, frame, and only the
    first is ever used as an `At()` offset - so a member access is resolved
    positionally against that one field rather than by name. A sum is evaluated
    because call sites spell a site as "the instruction plus its opcode", e.g.
    `0x55 + 2` for the operand of a two-byte instruction.
    """
    token = token.strip()
    if token.startswith('"'):
        return None

    def literal(part: str) -> int | None:
        part = part.strip()
        if re.fullmatch(r'0x[0-9A-Fa-f]+', part):
            return int(part, 16)
        if re.fullmatch(r'\d+', part):
            return int(part, 10)
        return None

    if '+' in token:
        total = 0
        for part in token.split('+'):
            value = literal(part)
            if value is None:
                return None
            total += value
        return '0x%x' % total

    value = literal(token)
    if value is not None:
        return '0x%x' % value

    member = re.fullmatch(r'(\w+)\.(\w+)', token)
    if member:
        fields = consts.get(member.group(1))
        if fields:
            first = literal(fields[0])
            if first is not None:
                return '0x%x' % first
    return None


def rows_for(ev: dict[str, dict], site: str) -> list[dict]:
    """The evidence rows a call site could refer to.

    The two naming spaces are not identical and were never linked: the tsv is
    written by the alignment tool and qualifies its rows ("menu unfreeze (call
    site)", "projectile null check (jump out)"), while the call site carries the
    short name. So a row belongs to a site when the row's name is the site name
    or the site name followed by a qualifier - and when several qualify (the two
    "stats menu controls" rows), the offset decides which one is meant.
    """
    return [row for name, row in ev.items() if name == site or name.startswith(site + ' ')]


def check(failures: list[str], warnings: list[str]) -> int:
    """Every 1.5.x offset the code applies is one the evidence backs."""
    ev = evidence()
    if not ev:
        failures.append(
            f'{EVIDENCE.relative_to(ROOT).as_posix()} is missing or empty; the 1.5.x offsets in '
            f'the client would then be backed by nothing')
        return 0

    checked = 0
    for path in sources():
        raw = path.read_text(encoding='utf-8', errors='ignore')
        if 'GamePatch::At' not in raw:
            continue
        text = strip_comments(raw)
        consts = constants(raw)

        for match in AT_CALL.finditer(text):
            args = [a.strip() for a in match.group(2).split(',') if a.strip()]
            site = match.group(3)
            if not args or not site:
                failures.append(
                    f'{path.relative_to(ROOT).as_posix()}: an At() call has no site name or no '
                    f'offset; the name is what ties the call to its measurement')
                continue

            numbers = [a for a in args if not a.startswith('"')]
            measured_on = next((a.strip('"') for a in args if a.startswith('"')), None)

            modern = resolve(numbers[0], consts, 'jumpOut') if numbers else None
            if modern is None:
                failures.append(
                    f'{path.relative_to(ROOT).as_posix()}: cannot resolve the modern offset for '
                    f'patch {site!r} ({numbers[0] if numbers else "?"!r}); this gate cannot verify it')
                continue

            legacy = resolve(numbers[1], consts, 'jumpOut') if len(numbers) > 1 else None

            candidates = rows_for(ev, site)
            if not candidates:
                failures.append(
                    f'{path.relative_to(ROOT).as_posix()}: patch {site!r} has no row in '
                    f'{EVIDENCE.name}; either the measurement was never recorded or the site was '
                    f'renamed and the evidence not')
                continue

            # The offset is what identifies the row when a name covers several.
            row = next((r for r in candidates if r['ae_offset'].lower() == modern), None)
            if row is None:
                recorded = ', '.join(r['ae_offset'] for r in candidates)
                failures.append(
                    f'{path.relative_to(ROOT).as_posix()}: patch {site!r} uses modern offset '
                    f'{modern}, which no {EVIDENCE.name} row records for it (recorded: {recorded})')
                continue

            checked += 1

            if legacy is None:
                if row['se_offset'] is not None and site not in UNUSED_LEGACY_OK:
                    warnings.append(
                        f'{site!r}: {EVIDENCE.name} records a 1.5.x offset ({row["se_offset"]}) that '
                        f'the call site does not pass; the patch is skipped on 1.5.x')
                if row['se_offset'] is None and site not in NO_LEGACY_EXPECTED:
                    warnings.append(
                        f'{site!r}: no 1.5.x offset in the code and none in {EVIDENCE.name}, but '
                        f'this site is not listed as expected to have none')
            else:
                if row['se_offset'] is None:
                    failures.append(
                        f'{path.relative_to(ROOT).as_posix()}: patch {site!r} applies a 1.5.x offset '
                        f'({legacy}) that {EVIDENCE.name} does not record - the row says '
                        f'{row["note"]!r}. A number nobody measured must not be written into a '
                        f'running game')
                elif legacy != row['se_offset'].lower():
                    failures.append(
                        f'{path.relative_to(ROOT).as_posix()}: patch {site!r} uses 1.5.x offset '
                        f'{legacy} but {EVIDENCE.name} records {row["se_offset"]}')
                elif measured_on != LEGACY_PREFIX:
                    failures.append(
                        f'{path.relative_to(ROOT).as_posix()}: patch {site!r} carries a 1.5.x offset '
                        f'measured on {measured_on!r}, but the evidence is for {LEGACY_PREFIX}; At() '
                        f'only applies it on that prefix, so the two disagree')

    # A site listed as expected-to-have-none must still exist and still have none,
    # otherwise the exemption outlives its reason and silently widens.
    for site, reason in {**NO_LEGACY_EXPECTED, **UNUSED_LEGACY_OK}.items():
        if not rows_for(ev, site):
            failures.append(
                f'{EVIDENCE.name} no longer records {site!r}, but this gate exempts it ({reason}); '
                f'drop the exemption rather than leaving it to cover nothing')

    # The corpus floor. Every loop above is driven by `GamePatch::At(...)` calls
    # found in the client sources, so a run that found none - the files moved, the
    # macro was renamed, the sources stopped being C++ - compares nothing and the
    # summary line would still print `0 patch site(s) agree`. That is not a pass:
    # it means this gate no longer sees the thing it exists to compare.
    if checked == 0:
        failures.append(
            f'no `GamePatch::At(...)` call site was found under '
            f'{CLIENT.relative_to(ROOT).as_posix()}, so nothing was compared against '
            f'{EVIDENCE.name}; an empty scan means the sources moved, not that the offsets agree')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        ev = evidence()
        print('checked:')
        print(f'  every GamePatch::At() call resolves against {EVIDENCE.relative_to(ROOT).as_posix()}')
        print('  the modern offset matches the recorded ae_offset')
        print('  a 1.5.x offset is only applied when a row records one')
        print(f'  {len(ev)} evidence row(s)')
        for site, row in ev.items():
            legacy = row['se_offset'] or '-'
            print(f'    {site[:52]:<54} ae={row["ae_offset"]:<8} se={legacy}')
        return 0

    failures: list[str] = []
    warnings: list[str] = []
    checked = check(failures, warnings)

    if warnings:
        print(f'  {len(warnings)} note(s):')
        for warning in warnings:
            print(f'    - {warning}')

    if failures:
        print(f'PATCH OFFSET CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('PATCH OFFSET CHECK OK')
    print(f'  {checked} patch site(s) agree with {EVIDENCE.name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
