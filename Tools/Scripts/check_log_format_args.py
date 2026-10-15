#!/usr/bin/env python3
"""Keep every log call's placeholders and arguments in agreement.

spdlog formats through fmt, and fmt throws `fmt::format_error` when the format
string asks for an argument that was not passed:

    spdlog::warn("{}: dropping {} of {}", __FUNCTION__, count);   // three {}, two args

The throw happens inside the sink, at the moment the line is written, so it lands
in whatever the caller's exception handling is - and a log line that only fires on
a rare path ("the address library id is not mapped", "a message could not be
built") is exactly the line that is never exercised until the day it matters. A
log call that crashes instead of logging is worse than no log call.

Printed literal braces are fine (`{{` and `}}` are escapes), and a call whose
format string is a variable is skipped rather than guessed at.

    python Tools/Scripts/check_log_format_args.py          # the gate
    python Tools/Scripts/check_log_format_args.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CODE = ROOT / 'Code'

LEVELS = 'trace|debug|info|warn|error|critical'
# spdlog::info(...), logger->info(...), and the fmt entry points the tree uses
# directly: fmt::format decides what an error message or a dialog says, and it
# throws for the same reason spdlog does.
CALL = re.compile(rf'(?:spdlog::|->|\b)({LEVELS})\s*\(|fmt::(format|print|format_to|vformat|sprintf)\s*\(')
# A format string argument: one or more adjacent literals, optionally glued to
# __FUNCTION__ / __func__ (the idiom in this tree: `__FUNCTION__ ": quest {:X}"`).
LITERAL_PART = re.compile(r'"(?:[^"\\]|\\.)*"')

MIN_CALLS = 300
SKIPPED_OK = 0.35  # share of calls whose format is not a literal


def strip_comments(text: str) -> str:
    # Comments only: string literals have to survive, because the format string
    # lives in one and the arguments live beside it.
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def call_args(text: str, open_paren: int) -> tuple[str, int] | None:
    """The argument list of the call whose '(' is at open_paren, and where it ends."""
    depth, i, n = 0, open_paren, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            i += 1
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                return text[open_paren + 1:i], i
        i += 1
    return None


def split_args(args: str) -> list[str]:
    out, depth, current, i, n = [], 0, [], 0, len(args)
    while i < n:
        c = args[i]
        if c == '"':
            start = i
            i += 1
            while i < n and args[i] != '"':
                i += 2 if args[i] == '\\' else 1
            i += 1
            current.append(args[start:i])
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(''.join(current))
            current = []
        else:
            current.append(c)
        i += 1
    if current:
        out.append(''.join(current))
    return [a.strip() for a in out if a.strip()]


def placeholder_count(fmt: str) -> int:
    """`{}`, `{:X}`, `{:#04x}` count; `{{` and `}}` are literal braces."""
    count, i, n = 0, 0, len(fmt)
    while i < n:
        c = fmt[i]
        if c == '{':
            if i + 1 < n and fmt[i + 1] == '{':
                i += 2
                continue
            end = fmt.find('}', i)
            if end < 0:
                break
            count += 1
            i = end + 1
        else:
            i += 1
    return count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    failures: list[str] = []
    checked = skipped = 0
    for path in sorted(CODE.rglob('*')):
        if path.suffix not in ('.cpp', '.h', '.hpp', '.inl'):
            continue
        text = strip_comments(path.read_text(encoding='utf-8', errors='ignore'))
        for match in CALL.finditer(text):
            # `foo->info(` must be a call, not a member of a longer word.
            if match.start() and (text[match.start() - 1].isalnum() or text[match.start() - 1] == '_'):
                continue
            open_paren = match.end() - 1
            extracted = call_args(text, open_paren)
            if extracted is None:
                continue
            body, _end = extracted
            parts = split_args(body)
            if not parts:
                continue
            first = parts[0]
            literals = LITERAL_PART.findall(first)
            if not literals or LITERAL_PART.sub('', first).replace('__FUNCTION__', '').replace('__func__', '').strip():
                skipped += 1
                continue
            fmt = ''.join(literal[1:-1] for literal in literals)
            placeholders = placeholder_count(fmt)
            given = len(parts) - 1
            checked += 1
            if placeholders != given:
                line = text[:match.start()].count('\n') + 1
                failures.append(
                    f'{path.relative_to(ROOT).as_posix()}:{line}: {match.group(1)} has {placeholders} '
                    f'placeholder(s) and {given} argument(s): {fmt[:70]!r}')

    if checked < MIN_CALLS:
        failures.append(
            f'only {checked} log call(s) with a literal format were parsed, below the floor of '
            f'{MIN_CALLS}; the call shape changed and this gate is not looking at the logging it '
            f'claims to')
    share = skipped / max(checked + skipped, 1)
    if share > SKIPPED_OK:
        failures.append(
            f'{share:.0%} of the calls seen were skipped because their format is not a literal, above '
            f'the {SKIPPED_OK:.0%} this gate assumes; it is covering much less than it reports')

    if args.list:
        print(f'checked: {checked} log call(s) with a literal format, {skipped} skipped (variable format)')
        print('  placeholders `{{}}` / `{{:X}}` / `{{:#04x}}` count; `{{{{` and `}}}}` are literal braces')
        return 0

    if failures:
        print(f'LOG FORMAT CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('LOG FORMAT CHECK OK')
    print(f'  {checked} log call(s) pass as many arguments as their format string asks for')
    return 0


if __name__ == '__main__':
    sys.exit(main())
