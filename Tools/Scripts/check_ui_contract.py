#!/usr/bin/env python3
"""Bind the C++ side's UI vocabulary to the strings and translations it lands on.

The overlay is a CEF browser, so the client and the server talk to it by building
JSON text; there is no type, no schema and no compiler between the two languages.
Two vocabularies cross that boundary:

  * the connection-failure codes. C++ writes {"error": "<code>"} and the UI turns
    the code into a translation key by concatenation -
    'SERVICE.ERROR.ERRORS.' + code.toUpperCase() - so a code with no catalog entry
    shows the player a raw key, and a code missing from one language shows it in
    that language only. Nothing compared the three lists.
  * the mods-mismatch direction. The reply is one list built from two
    comparisons, so each entry says which one it came from. It said it with
    "Id == 0 means install" until this gate was written, and 0 is a real id - the
    first plugin in the load order, and the first ESL for a lite one (see the
    markers in Code/encoding/Structs/Mods.h) - so a client whose first ESL was
    absent from the server was told to INSTALL a mod it had to REMOVE. The words
    are now explicit, and this is what keeps the two sides speaking the same ones.
  * the system-message strings, on both sides of the bridge: the client's
    SendSystemMessage and the overlay's pushSystemMessage. Both land on the chat's
    system branch, which pipes the content through transloco, so the argument is a
    translation key and a sentence is looked up as one and found nowhere.

    python Tools/Scripts/check_ui_contract.py          # the gate
    python Tools/Scripts/check_ui_contract.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
I18N = ROOT / 'Code' / 'skyrim_ui' / 'src' / 'assets' / 'i18n'
ERROR_SERVICE = ROOT / 'Code' / 'skyrim_ui' / 'src' / 'app' / 'services' / 'error.service.ts'
TRANSPORT = ROOT / 'Code' / 'client' / 'Services' / 'Generic' / 'TransportService.cpp'
CODE_ROOTS = [ROOT / 'Code' / 'client', ROOT / 'Code' / 'server']
UI_SRC = ROOT / 'Code' / 'skyrim_ui' / 'src'

# Every string handed to the chat as a system message, from both sides of the
# bridge: the client's C++ builds one with SendSystemMessage("...") and the overlay
# pushes its own with pushSystemMessage('...'). The chat renders a system message
# with `content | transloco`, so this argument is a translation KEY and never prose:
# a sentence is looked up as a key, is found nowhere, and comes out in English for
# every language - which looks correct to an English player and is wrong for the
# other thirteen. Three of these were sentences.
SYSTEM_MESSAGE = re.compile(r'SendSystemMessage\(\s*"([^"]+)"')
# The overlay's calls wrap across lines (chat/commands.ts passes parameters on the
# lines below the key), which `\s` and `[^']` already cover.
SYSTEM_MESSAGE_TS = re.compile(r"pushSystemMessage\(\s*'([^']+)'")

# The language whose entries every key must have. Transloco is configured with
# fallbackLang 'en' and useFallbackTranslation, so a key present here renders as
# English elsewhere; that is a translation gap to report, not a broken message.
FALLBACK_LANGUAGE = 'en.json'

# The escaped literal the C++ builds its payload with: \"error\": \"<code>\"
ERROR_CODE = re.compile(r'\\"error\\": \\"([A-Za-z_0-9]+)\\"')
# One mods-mismatch entry: ["<filename>","<direction>"] assembled with length 0
# between the two, so only the direction is a plain literal.
LITERAL = re.compile(r'"([a-z][a-z_]{2,})"')
UNION_ENTRY = re.compile(r"'([A-Za-z][A-Za-z_0-9]*)'")
COMPARE = re.compile(r"mod\[1\]\s*[!=]==\s*'([a-z_]+)'")

# Tokens that are JSON structure or the error name of the block they sit in, not
# vocabulary this gate is about.
STRUCTURAL = {'error', 'data', 'mods'}

LANGUAGES_MIN = 2


def flatten(obj, prefix=''):
    out = {}
    if isinstance(obj, dict):
        for k, v in obj.items():
            out.update(flatten(v, prefix + k + '.'))
    else:
        out[prefix.rstrip('.')] = obj
    return out


def error_codes() -> dict[str, list[str]]:
    """code -> the files that can send it."""
    found: dict[str, list[str]] = {}
    for base in CODE_ROOTS:
        if not base.is_dir():
            continue
        for path in sorted(base.rglob('*.cpp')):
            text = path.read_text(encoding='utf-8', errors='ignore')
            for code in ERROR_CODE.findall(text):
                found.setdefault(code, []).append(path.relative_to(ROOT).as_posix())
    return found


def union_codes() -> set[str]:
    if not ERROR_SERVICE.is_file():
        return set()
    text = ERROR_SERVICE.read_text(encoding='utf-8', errors='ignore')
    start = text.find('export interface ErrorEvent')
    if start < 0:
        return set()
    end = text.find('}', start)
    return set(UNION_ENTRY.findall(text[start:end]))


def directions_in_cpp() -> tuple[set[str], str]:
    """The direction words the mismatch payload can carry."""
    if not TRANSPORT.is_file():
        return set(), ''
    text = TRANSPORT.read_text(encoding='utf-8', errors='ignore')
    start = text.find('case AR::kModsMismatch:')
    if start < 0:
        return set(), ''
    end = text.find('case AR::kClientModsDisallowed:', start)
    block = text[start:end if end > 0 else len(text)]
    # The block also names the error itself; drop structure and the code.
    codes = set(ERROR_CODE.findall(block))
    words = {w for w in LITERAL.findall(block) if w not in STRUCTURAL and w not in codes}
    return words, block


def directions_in_ts() -> set[str]:
    if not ERROR_SERVICE.is_file():
        return set()
    return set(COMPARE.findall(ERROR_SERVICE.read_text(encoding='utf-8', errors='ignore')))


def ui_literal_keys() -> set[str]:
    """SERVICE.ERROR.ERRORS.* keys the UI asks for by name rather than by code.

    A key reached this way is not built from an error code, so it is reachable
    without any C++ sending it (MODS_MISMATCH_INSTALL/REMOVE are exactly that).
    """
    found: set[str] = set()
    src = ROOT / 'Code' / 'skyrim_ui' / 'src'
    if not src.is_dir():
        return found
    pattern = re.compile(r'SERVICE\.ERROR\.ERRORS\.[A-Z0-9_]+')
    for path in sorted(src.rglob('*')):
        if path.suffix in ('.ts', '.html'):
            found.update(pattern.findall(path.read_text(encoding='utf-8', errors='ignore')))
    return found


def check(failures: list[str], notes: list[str]) -> int:
    if not I18N.is_dir():
        failures.append(f'{I18N.relative_to(ROOT).as_posix()} is missing; the UI strings cannot be checked')
        return 0

    checks = 0
    languages: dict[str, set[str]] = {}
    for path in sorted(I18N.glob('*.json')):
        if path.name == 'overwrite.json':
            continue  # a build-time overlay, not a language
        try:
            languages[path.name] = set(flatten(json.loads(path.read_text(encoding='utf-8'))))
        except json.JSONDecodeError as exc:
            failures.append(f'{path.relative_to(ROOT).as_posix()}: not valid JSON ({exc})')
    if len(languages) < LANGUAGES_MIN:
        failures.append(
            f'only {len(languages)} language file(s) parsed; with fewer than {LANGUAGES_MIN} the '
            f'per-language half of this check is not being made')

    codes = error_codes()
    if not codes:
        failures.append(
            'no {"error": "<code>"} payload was found in the C++ sources, so the catalog and the '
            'type union were compared against nothing; the payload builder moved or changed shape')
        return 0

    union = union_codes()
    if not union:
        failures.append(
            f'{ERROR_SERVICE.relative_to(ROOT).as_posix()}: the ErrorEvent union could not be read, so '
            f'the client half of this contract is not being checked')

    for code in sorted(codes):
        key = 'SERVICE.ERROR.ERRORS.' + code.upper()
        missing = sorted(name for name, keys in languages.items() if key not in keys)
        if missing:
            failures.append(
                f'{"SERVICE.ERROR.ERRORS." + code.upper()}: the client can send error "{code}" '
                f'({", ".join(sorted(set(codes[code])))}) but {len(missing)} language(s) have no entry: '
                f'{", ".join(missing)}; the UI builds this key by concatenation, so the player sees it raw')
        if union and code not in union:
            failures.append(
                f'error "{code}" is sent by {", ".join(sorted(set(codes[code])))} but is not in the '
                f'ErrorEvent union in {ERROR_SERVICE.relative_to(ROOT).as_posix()}; the union is what '
                f'tells the next reader which codes exist')

    # A catalog entry the UI can never ask for is dead weight a reader would
    # trust. Reachable means "built from a code C++ sends" or "asked for by name
    # in the UI", and it is reported rather than failed: removing the "not purely
    # vanilla" warning was deliberate (DiscoveryService logs it now), so its
    # strings are unreachable on purpose and may come back.
    reachable = {'SERVICE.ERROR.ERRORS.' + c.upper() for c in codes} | ui_literal_keys()
    dead = set()
    for keys in languages.values():
        dead.update(k for k in keys if k.startswith('SERVICE.ERROR.ERRORS.') and k not in reachable)
    if dead:
        notes.append(
            f'{len(languages)} language file(s) carry {sorted(dead)}, which no error code can reach and '
            f'no UI source asks for by name')

    produced_words, block = directions_in_cpp()
    wanted = directions_in_ts()
    if not block:
        failures.append(
            f'{TRANSPORT.relative_to(ROOT).as_posix()}: the case AR::kModsMismatch block was not found, '
            f'so the direction vocabulary cannot be compared with the UI')
    elif produced_words != {'install', 'remove'}:
        failures.append(
            f'{TRANSPORT.relative_to(ROOT).as_posix()}: the mods-mismatch payload carries the direction '
            f'words {sorted(produced_words)}, expected [\'install\', \'remove\']; a word added here has '
            f'to be added to the UI in the same commit')
    if wanted != {'install', 'remove'}:
        failures.append(
            f'{ERROR_SERVICE.relative_to(ROOT).as_posix()}: the UI switches on {sorted(wanted)}, expected '
            f'[\'install\', \'remove\']; an entry the UI does not name would be dropped silently, and one '
            f'it names that C++ does not send would never match')

    # 4. What the client shows the player must be a key, not a sentence.
    literals: dict[str, list[str]] = {}
    for base in CODE_ROOTS:
        if not base.is_dir():
            continue
        for path in sorted(base.rglob('*.cpp')):
            text = path.read_text(encoding='utf-8', errors='ignore')
            for match in SYSTEM_MESSAGE.finditer(text):
                line = text[:match.start()].count('\n') + 1
                literals.setdefault(match.group(1), []).append(
                    f'{path.relative_to(ROOT).as_posix()}:{line}')

    if not literals:
        failures.append(
            'no SendSystemMessage string was found in the client sources, so the "a key, not a sentence" '
            'rule covered nothing; the overlay send helper moved or changed shape')

    # The overlay pushes its own system messages, and they land on the same rule: the
    # chat pipes both through `content | transloco`. Counted separately, because an
    # empty scan on one side is exactly what a moved or renamed helper looks like, and
    # merging first would let the other side's hits hide it.
    overlay: dict[str, list[str]] = {}
    for path in sorted(UI_SRC.rglob('*.ts')):
        text = path.read_text(encoding='utf-8', errors='ignore')
        for match in SYSTEM_MESSAGE_TS.finditer(text):
            line = text[:match.start()].count('\n') + 1
            overlay.setdefault(match.group(1), []).append(
                f'{path.relative_to(ROOT).as_posix()}:{line}')

    if not overlay:
        failures.append(
            'no pushSystemMessage string was found in the overlay sources, so the "a key, not a sentence" '
            'rule covered nothing there; the overlay send helper moved or changed shape')

    for text, sites in overlay.items():
        literals.setdefault(text, []).extend(sites)

    for text, sites in sorted(literals.items()):
        if text not in languages.get(FALLBACK_LANGUAGE, set()):
            failures.append(
                f'{", ".join(sites)} sends "{text}" to the chat, which renders system messages with '
                f'`content | transloco`; that is a translation key, and {FALLBACK_LANGUAGE} has no such '
                f'key, so what the player sees is the string itself')
        else:
            checks += 1

    # A key that exists only in the fallback language still reads as English, which
    # is the configured behaviour - reported so a translator can find it, not failed.
    untranslated = sorted(
        text for text in literals
        if text in languages.get(FALLBACK_LANGUAGE, set())
        and any(text not in keys for name, keys in languages.items() if name != FALLBACK_LANGUAGE))
    if untranslated:
        notes.append(f'{untranslated} are missing from at least one language and fall back to English')

    return len(codes)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  every error code the C++ can send has SERVICE.ERROR.ERRORS.<CODE> in every language')
        print('  every error code the C++ can send is listed in the UI ErrorEvent union')
        print('  the mods-mismatch direction vocabulary is exactly {install, remove} on both sides')
        print('  every system-message literal - SendSystemMessage in the C++ and pushSystemMessage in '
              'the overlay - is a key en.json carries')
        codes = error_codes()
        print(f'  codes reachable from C++: {len(codes)}')
        for code in sorted(codes):
            print(f'    {code:<22} {", ".join(sorted(set(codes[code])))}')
        print(f'  direction words in C++: {sorted(directions_in_cpp()[0])}')
        print(f'  direction words in UI : {sorted(directions_in_ts())}')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    count = check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'UI CONTRACT CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('UI CONTRACT CHECK OK')
    print(f'  {count} error code(s) reachable from C++, each with a catalog entry in every language '
          f'and a member in the UI union; direction vocabulary bound on both sides')
    return 0


if __name__ == '__main__':
    sys.exit(main())
