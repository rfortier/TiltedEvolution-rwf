#!/usr/bin/env python3
"""Keep the one open question about lite (ESL) files answerable from a user log.

The client reports its loaded files to the server (TransportService's
authentication request), and the server assigns every later form id through that
list (ModsComponent, then ModSystem on the way back). Membership is decided by
Mod::IsLoaded(), which asks whether standardId is 0xFF.

For a lite file that question is not answered anywhere in this repository: 0xFF
is the engine's "not in the standard space" value and an ESL is by definition not
in the standard space, so IsLoaded() may be false for every ESL. The engine
headers that would settle it are not in this tree, and the two answers differ by
a lot:

  * if lite files pass, the lite path below them works (ModSystem fills
    m_liteToServer and resolves 0xFE form ids through it);
  * if they do not, every form an ESL adds loses its server id, and with the mod
    policy on the server asks the player to install a mod they already have - a
    refusal they cannot resolve.

Guessing is not a fix, so the decision is instrumented instead: the reporting loop
names each file it drops with isLite/standardId/liteId, and both ends of the lite
path report their counts, all at info - the level the shipped build logs at, since
TiltedOnlineApp leaves set_level(debug) commented out. One player log with an ESL
installed closes the question in either direction, which is docs/FOLLOW-UP.md
section 6.

This gate is what keeps that possible. It does not judge the engine's behaviour -
it cannot - it checks the things whose disappearance would quietly make the
question unanswerable again:

  * the diagnostic exists, names the three deciding fields, and is not at a level
    the shipped build filters out;
  * the counts that make it a yes/no answer are still reported at both ends;
  * the lite path itself is still wired (the maps m_liteToServer fills, and the
    0xFE lookup that reads it);
  * the struct fields and assertions the diagnostic reads still exist;
  * the open item is still recorded, so the instrument and the gap it exists for
    are retired together rather than one at a time.

    python Tools/Scripts/check_lite_mod_path.py          # the gate
    python Tools/Scripts/check_lite_mod_path.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

TRANSPORT = 'Code/client/Services/Generic/TransportService.cpp'
MOD_SYSTEM = 'Code/client/Systems/ModSystem.cpp'
TES = 'Code/client/Games/TES.h'
FOLLOW_UP = 'docs/FOLLOW-UP.md'

# The line the reporting loop prints for each dropped file. Anchoring on the
# sentence rather than a line number is deliberate: the repo's own lesson is that
# line numbers rot while a claim does not.
SKIP_ANCHOR = 'is not loaded by the standard id rule'
# The level the shipped build actually logs at. set_level(debug) is commented out
# in TiltedOnlineApp.cpp, so a diagnostic below info is invisible in the logs
# players send - which would make this instrument worthless.
VISIBLE_LEVELS = ('spdlog::info', 'spdlog::warn', 'spdlog::error', 'spdlog::critical')
INVISIBLE_LEVELS = ('spdlog::debug', 'spdlog::trace')

# The three fields that decide the question, as the diagnostic has to READ them.
# Written as member accesses rather than as the names in the format string: the
# format text could keep saying "standardId" while the argument printed something
# else, and the argument is the half that carries the answer.
DECIDING_FIELDS = ('->IsLite()', '->standardId', '->liteId')

# Counts that turn the diagnostic into an answer: reported total, lite among the
# reported, dropped by the rule, and the mappings the receive side established.
COUNTS = (
    ('TransportService', TRANSPORT, 'reported'),
    ('TransportService', TRANSPORT, 'dropped by the standard id rule'),
    ('ModSystem', MOD_SYSTEM, 'lite mappings established'),
)

# The lite path the question is about. Each is a fragment that has to be present.
LITE_PATH = (
    (MOD_SYSTEM, 'm_liteToServer.emplace(', 'the receive side stops recording which server id a lite mod got'),
    (MOD_SYSTEM, 'hiByte == 0xFE', 'form ids in the ESL space stop being recognised as lite'),
    (MOD_SYSTEM, 'm_liteToServer.find(', 'lite form ids stop resolving through the map that was filled'),
)

# Fields and assertions the diagnostic reads. If a rename moves them, the log line
# would either not compile or - worse - print a field that no longer means this.
STRUCT = (
    (TES, 'bool IsLite()'),
    (TES, 'uint16_t GetId()'),
    (TES, 'standardId != 0xFF'),
    (TES, 'offsetof(Mod, standardId)'),
    (TES, 'offsetof(Mod, liteId)'),
)


def read(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        return ''
    return path.read_text(encoding='utf-8', errors='ignore')


def window(text: str, anchor: str, before: int, after: int) -> str | None:
    """The text around an anchor, or None when the anchor is gone."""
    index = text.find(anchor)
    if index < 0:
        return None
    return text[max(0, index - before):index + len(anchor) + after]


def check(failures: list[str], notes: list[str]) -> int:
    checks = 0

    transport = read(TRANSPORT)
    if not transport:
        failures.append(
            f'{TRANSPORT} is missing; the diagnostic that answers the lite question lives there')
        return 0

    # 1. The diagnostic exists, names the deciding fields, and is visible.
    around = window(transport, SKIP_ANCHOR, 200, 400)
    if around is None:
        failures.append(
            f'{TRANSPORT}: the line reporting a file dropped by IsLoaded() is gone (no "{SKIP_ANCHOR}"); '
            f'without it no user log can say whether lite files are being dropped, which is '
            f'docs/FOLLOW-UP.md section 6')
    else:
        checks += 1
        if not any(level in around for level in VISIBLE_LEVELS):
            failures.append(
                f'{TRANSPORT}: the dropped-file diagnostic is not at a level the shipped build prints; '
                f'it must be one of {list(VISIBLE_LEVELS)} because TiltedOnlineApp leaves '
                f'set_level(debug) commented out')
        if any(level in around for level in INVISIBLE_LEVELS):
            failures.append(
                f'{TRANSPORT}: the dropped-file diagnostic uses {[l for l in INVISIBLE_LEVELS if l in around]}, '
                f'which the shipped log level filters out')
        missing = [f for f in DECIDING_FIELDS if f not in around]
        if missing:
            failures.append(
                f'{TRANSPORT}: the dropped-file diagnostic no longer names {missing}; those three fields '
                f'are the whole question (isLite/standardId/liteId), and a line without them cannot '
                f'answer it')

    # 2. The counts that make it a yes/no answer.
    for label, rel, fragment in COUNTS:
        if fragment not in read(rel):
            failures.append(f'{rel}: {label} no longer reports "{fragment}"; the answer needs the counts')
        else:
            checks += 1

    # 3. The lite path the question is about is still wired.
    for rel, fragment, consequence in LITE_PATH:
        if fragment not in read(rel):
            failures.append(f'{rel}: "{fragment}" is gone - {consequence}')
        else:
            checks += 1

    # 4. The fields the diagnostic reads still exist.
    for rel, fragment in STRUCT:
        if fragment not in read(rel):
            failures.append(
                f'{rel}: "{fragment}" is gone; the dropped-file diagnostic reads that field or '
                f'assertion, so either it stopped compiling or it is now printing something else')
        else:
            checks += 1

    # 5. The gap and its instrument are retired together. The anchor is the
    #    top-level heading, with the space after the number: "## 6." alone also
    #    matches the subsection "## 6.1", so renaming the section while leaving its
    #    body behind would have read as "still recorded".
    follow_up = read(FOLLOW_UP)
    if not re.search(r'^## 6\. ', follow_up, re.M):
        failures.append(
            f'{FOLLOW_UP}: the open item about lite files (section 6) is gone while the instrument '
            f'remains; either the question was answered - then drop this gate and the instrumentation '
            f'in the same commit - or the record was lost')
    elif 'check_lite_mod_path.py' not in follow_up:
        failures.append(
            f'{FOLLOW_UP}: section 6 no longer names check_lite_mod_path.py, so the item and the gate '
            f'that keeps it answerable are no longer tied together')
    else:
        checks += 1

    notes.append(
        'the lite-file question is still open: one log with an ESL installed answers it (see the '
        '"mod list:" and "server mod list:" lines)')

    return checks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  the dropped-file diagnostic exists, names isLite/standardId/liteId and is visible at the shipped level')
        print(f'  the counts that answer it are reported: {[c[2] for c in COUNTS]}')
        print(f'  the lite path is still wired: {[c[1] for c in LITE_PATH]}')
        print(f'  the fields it reads still exist in {TES}')
        print(f'  {FOLLOW_UP} section 6 still records the question and names this gate')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    checks = check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'LITE MOD PATH CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('LITE MOD PATH CHECK OK')
    print(f'  {checks} anchor(s) present: the lite question is still answerable from a user log, '
          f'and the lite path under it is still wired')
    return 0


if __name__ == '__main__':
    sys.exit(main())
