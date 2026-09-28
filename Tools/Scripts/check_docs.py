#!/usr/bin/env python3
"""Keep the documentation honest about the numbers it states.

CODE_GUIDELINES.md makes documentation part of every version change. That rule is
only worth writing down if something checks it, because every claim this file
verifies has already drifted at least once:

  * the 1.5.x coverage figure was recounted by hand and published as 3080/3075
    while the generator said something else
  * Tools/ida/README.md and docs/LAN-RADMIN-GUIDE.md still quoted the old totals
    after the generator was fixed
  * v1.1.4 shipped with no README row in either language, so the version table
    and the changelog disagreed about which versions exist

Each check below compares a claim in prose against the file that owns the value,
rather than against another paragraph. Where a number has a single source, the
source is read; where a set has a single source, the set is counted.

    python Tools/Scripts/check_docs.py          # the gate
    python Tools/Scripts/check_docs.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IDS_MANIFEST = 'Tools/missing_1_5_97_ids.txt'
MAP = 'GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map'
README = 'README.md'
README_EN = 'README_EN.md'
CHANGELOG = 'CHANGELOG.md'
IDA_README = 'Tools/ida/README.md'
LAN_GUIDE = 'docs/LAN-RADMIN-GUIDE.md'

# Every doc that quotes the coverage totals. Each has drifted at least once, so
# each is checked rather than assumed to follow the others.
COVERAGE_DOCS = [README, README_EN, IDA_README, LAN_GUIDE]

# The tag series this repository cuts itself.
#
# It has to be spelled out because the namespace is shared: upstream
# TiltedEvolution publishes v1.0.x, v1.1.x, v1.2.0 ... v1.8.x, and this fork
# merged those commits, so a local clone carries both families. v1.1.x exists in
# both and means different commits - this repository's v1.1.0 is 03cf2cbb while
# upstream's is fd8748f0 - so a tag name alone cannot say whose it is.
#
# Only these series are required to have a changelog section, because only these
# are releases this repository made. When the first tag of a new series is cut,
# add its prefix here; the check below reports any tag it cannot classify rather
# than skipping it silently.
OWN_TAG_SERIES = ('1.1.',)


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding='utf-8')


def coverage() -> tuple[int, int, int]:
    """(mapped, total, map_entries) from the tools that own the numbers."""
    sys.path.insert(0, str(ROOT / 'Tools' / 'Scripts'))
    import gen_ae_to_se_map as gen  # noqa: E402

    ids = gen.collect_codebase_ids()
    mapped = set()
    for line in read(MAP).splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        mapped.add(int(line.split()[0]))
    present = len(ids & mapped)
    return present, len(ids), len(mapped)


def manifest_ids() -> list[int]:
    """The ae= rows of the id manifest, which must agree with the real gap."""
    found = []
    for line in read(IDS_MANIFEST).splitlines():
        m = re.match(r'^#\s*ae=(\d+)\s*\|', line)
        if m:
            found.append(int(m.group(1)))
    return found


def tags() -> list[str]:
    out = subprocess.run(['git', 'tag', '--list', 'v*'], cwd=str(ROOT),
                         capture_output=True, text=True)
    if out.returncode != 0:
        return []
    return [t for t in out.stdout.split() if t]


def changelog_versions() -> set[str]:
    return set(re.findall(r'^##\s+v?([0-9]+\.[0-9]+\.[0-9]+)', read(CHANGELOG), re.M))


# A version cell may carry the stable marker after the number, because the table
# marks the recommended release as the one to install. It may carry nothing else:
# allowing an arbitrary tail made this match the game-version table too ("1.5.3 ~
# 1.5.97" starts with a version), which would let a missing release row pass on
# the strength of an unrelated row - the exact vacuous pass this check exists to
# prevent.
STABLE_LABEL = r'(?:\s*\u2b50\s*\*\*(?:\u7a33\u5b9a\u7248|stable)\*\*)?'


def readme_versions(rel: str) -> set[str]:
    return set(re.findall(
        r'^\|\s*\*{0,2}([0-9]+\.[0-9]+\.[0-9]+)\*{0,2}' + STABLE_LABEL + r'\s*\|',
        read(rel), re.M))


def readme_stable_versions(rel: str) -> list[str]:
    """Versions whose table row carries the stable marker."""
    return re.findall(
        r'^\|\s*\*{0,2}([0-9]+\.[0-9]+\.[0-9]+)\*{0,2}\s*\u2b50\s*'
        r'\*\*(?:\u7a33\u5b9a\u7248|stable)\*\*',
        read(rel), re.M)


def check_coverage(failures: list[str]) -> None:
    """Every doc that quotes the 1.5.x coverage must quote the current totals."""
    mapped, total, entries = coverage()
    pct = 100.0 * mapped / total

    # The manifest's own header states the count and the totals.
    header = read(IDS_MANIFEST)
    m = re.search(r'manifest\s+\((\d+)\s+ids\)', header)
    if not m:
        failures.append(f'{IDS_MANIFEST}: the header no longer states an id count')
    else:
        stated = int(m.group(1))
        if stated != total - mapped:
            failures.append(
                f'{IDS_MANIFEST}: header says {stated} missing ids, the map has '
                f'{total - mapped}')

    m = re.search(r'Coverage\s+(\d+)/(\d+)\s+codebase ids', header)
    if not m:
        failures.append(f'{IDS_MANIFEST}: the header no longer states coverage')
    elif (int(m.group(1)), int(m.group(2))) != (mapped, total):
        failures.append(
            f'{IDS_MANIFEST}: header says coverage {m.group(1)}/{m.group(2)}, '
            f'the generator says {mapped}/{total}')

    # The per-id list must be exactly the ids the map cannot resolve.
    listed = set(manifest_ids())
    if listed:
        real = set()
        sys.path.insert(0, str(ROOT / 'Tools' / 'Scripts'))
        import gen_ae_to_se_map as gen  # noqa: E402
        real = set(gen.collect_codebase_ids())
        for line in read(MAP).splitlines():
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            real.discard(int(line.split()[0]))
        if listed != real:
            only_listed = sorted(listed - real)
            only_real = sorted(real - listed)
            if only_listed:
                failures.append(
                    f'{IDS_MANIFEST}: lists ids that ARE mapped: {only_listed}')
            if only_real:
                failures.append(
                    f'{IDS_MANIFEST}: does not list unmapped ids: {only_real}')

    # Every doc that quotes the totals must quote THESE totals, and must not
    # quote a different pair as if it were current. Checking only that the right
    # number appears somewhere is not enough: a doc can state it in one
    # paragraph and an outdated one in another, which is how this drifted the
    # first time.
    #
    # Historical passages are legitimate - Tools/ida/README.md records what an
    # earlier run reported, and says so. The rule is therefore about tense, not
    # about the number: a stale pair is a failure unless the sentence around it
    # marks it as superseded. That is a judgement this check cannot make, so it
    # looks for the marker instead, and the marker has to be present.
    STALE_OK = re.compile(
        r'superseded|Superseded|at the time|historical|Historical|当时|已过时|留档')

    current = re.compile(r'\b(\d{3,4})\s*/\s*(\d{3,4})\b')
    # Chinese order is reversed: "3082 个地址中已解析 3069"
    zh = re.compile(r'\b(\d{3,4})\s*个地址[^。\n]{0,24}?(\d{3,4})\b')
    en = re.compile(r'\b(\d{3,4})\s+of\s+the\s+(\d{3,4})\s+addresses')

    for rel in COVERAGE_DOCS:
        text = read(rel)
        lines = text.splitlines()
        current_pairs = set()
        stale_unmarked = []
        for i, line in enumerate(lines):
            for pattern in (current, zh, en):
                for m in pattern.finditer(line):
                    a, b = int(m.group(1)), int(m.group(2))
                    # Chinese order puts the total first; normalise.
                    if pattern is zh:
                        a, b = b, a
                    # Only pairs that look like an id-coverage claim. The map
                    # count is a separate metric that also reads as n/n - 3698
                    # offsets validated is not 3698 ids mapped - so a pair whose
                    # two halves are equal is the offset tally, not this one.
                    if a == b or not (2000 <= a and b <= 6000):
                        continue
                    if (a, b) == (mapped, total):
                        current_pairs.add((a, b))
                        continue
                    # A stale pair is only acceptable when the surrounding text
                    # says it is stale.
                    window = '\n'.join(lines[max(0, i - 3):i + 4])
                    if not STALE_OK.search(window):
                        stale_unmarked.append((a, b, i + 1))

        if not current_pairs:
            failures.append(
                f'{rel}: states no current coverage figure - it should quote '
                f'{mapped}/{total} like the others')
        for a, b, line_no in stale_unmarked:
            failures.append(
                f'{rel}:{line_no}: quotes {a}/{b} as if current, but the '
                f'generator says {mapped}/{total}; mark it superseded or update '
                f'it (gen_ae_to_se_map.py::collect_codebase_ids)')


def check_version_tables(failures: list[str]) -> None:
    """Both README version tables must cover every changelog version, and match."""
    released = changelog_versions()
    if not released:
        failures.append(f'{CHANGELOG}: no "## <version>" section found')
        return

    for rel in (README, README_EN):
        rows = readme_versions(rel)
        missing = sorted(released - rows)
        if missing:
            failures.append(
                f'{rel}: no version-table row for {", ".join(missing)} '
                f'(CHANGELOG has a section for each)')

    # The two READMEs must not disagree about which versions exist.
    zh, en = readme_versions(README), readme_versions(README_EN)
    if zh != en:
        failures.append(
            f'{README} and {README_EN} list different versions: '
            f'zh-only={sorted(zh - en)} en-only={sorted(en - zh)}')


def check_tags_have_sections(failures: list[str]) -> None:
    """A tag without a changelog section is a release nobody can read about."""
    released = changelog_versions()
    all_tags = tags()

    # A checkout that fetched no tags would make this loop run zero times and
    # report success - passing because it could not see the thing it checks.
    # That is the failure mode this guard exists for, so it is refused rather
    # than trusted. CI sets fetch-depth: 0 / fetch-tags: true for this reason.
    own_series = [t for t in all_tags if t.lstrip('v').startswith(OWN_TAG_SERIES)]
    if not own_series:
        failures.append(
            f'no {" or ".join(OWN_TAG_SERIES)} tags are visible, so the '
            f'tag/changelog check cannot run; this checkout probably has no tags '
            f'(CI needs fetch-depth: 0 and fetch-tags: true) - refusing to pass '
            f'vacuously')
        return

    for tag in all_tags:
        version = tag.lstrip('v')
        # Upstream's tags are not this repository's releases.
        if not version.startswith(OWN_TAG_SERIES):
            continue
        if version not in released:
            failures.append(
                f'{tag} is tagged but {CHANGELOG} has no "## v{version}" section')


def check_recommended_version(failures: list[str]) -> None:
    """The banner must name a release that exists and is the marked stable one."""
    zh = re.search(r'推荐 Mod 版本[:：]\s*\**\s*v?([0-9]+\.[0-9]+\.[0-9]+)', read(README))
    en = re.search(r'Recommended mod version[:：]\s*\**\s*v?([0-9]+\.[0-9]+\.[0-9]+)',
                   read(README_EN))
    if not zh or not en:
        failures.append('the recommended mod version is missing from a README banner')
        return
    if zh.group(1) != en.group(1):
        failures.append(
            f'the READMEs recommend different mod versions: {zh.group(1)} vs {en.group(1)}')
    released = changelog_versions()
    if zh.group(1) not in released:
        failures.append(
            f'the READMEs recommend {zh.group(1)}, which has no {CHANGELOG} section')

    # The banner and the version table both name the release to install, so they
    # have to name the same one. Recommending a build the table does not mark -
    # or marking two, or none - leaves the reader to guess, which is the state
    # this check exists to prevent.
    for rel, label in ((README, zh.group(1)), (README_EN, en.group(1))):
        marked = readme_stable_versions(rel)
        if not marked:
            failures.append(
                f'{rel}: no version-table row carries the stable marker, so the '
                f'banner recommending {label} is unverifiable')
        elif len(marked) > 1:
            failures.append(
                f'{rel}: {len(marked)} rows carry the stable marker '
                f'({", ".join(marked)}); exactly one release is the stable one')
        elif marked[0] != label:
            failures.append(
                f'{rel}: the banner recommends {label} but the stable row is '
                f'{marked[0]}; they must name the same release')


CHECKS = {
    'coverage': check_coverage,
    'version-tables': check_version_tables,
    'tags-have-sections': check_tags_have_sections,
    'recommended-version': check_recommended_version,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list', action='store_true', help='list the checks and exit')
    args = ap.parse_args()

    if args.list:
        for name, fn in CHECKS.items():
            lines = [l.strip() for l in (fn.__doc__ or '').splitlines() if l.strip()]
            print(f'  {name:<22} {lines[0] if lines else ""}')
        return 0

    failures: list[str] = []
    for fn in CHECKS.values():
        fn(failures)

    if failures:
        print(f'DOC CHECK FAILED ({len(failures)})')
        for f in failures:
            print(f'  - {f}')
        return 1

    mapped, total, entries = coverage()
    print('DOC CHECK OK')
    print(f'  coverage {mapped}/{total} stated consistently in {len(COVERAGE_DOCS)} doc(s)')
    print(f'  {entries} map entries; id manifest agrees with the real gap')
    print(f'  {len(changelog_versions())} changelog version(s), all present in both READMEs')
    return 0


if __name__ == '__main__':
    sys.exit(main())
