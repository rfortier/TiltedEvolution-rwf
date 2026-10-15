#!/usr/bin/env python3
"""Keep the two scene-update messages honest about what their fields are for.

A message field that the receiving side never reads is the quietest defect in the
protocol: the bytes are transferred, check_wire_symmetry passes, and the feature
behind the field simply never happens. RequestQuestSceneUpdate carried one -
`Status`, written by

    update.Status = stageId == RequestQuestUpdate::StageUpdateForced;

which compares a quest's current stage against the constant 5, and read by
nothing on the server. See docs/PITFALLS.md section 135.

WHY THIS GATE IS SCOPED TO TWO MESSAGES AND NOT ALL OF THEM
-----------------------------------------------------------
The general rule was written first and thrown away. Searching the tree by field
name cannot work: "Status", "Id" and "Stage" belong to a dozen messages at once,
so one message reading its own field excuses every other message's. Scoping the
search to the handler body does not rescue it either - a handler that forwards its
packet to a helper is normal here, and the first version of that scan called
CharacterSpawnRequest::FactionsContent and StringCacheUpdate::Values unread, i.e.
it flagged fields whose features demonstrably work. A gate that reports working
code gets switched off, so it is not shipped (the measurement is recorded in
section 135 rather than the gate).

What is shipped is the part that can be made exact: the two messages of the scene
path, whose handlers have both been read end to end, so a field named in them and
read nowhere in that body really has no reader. The scope is an explicit list, and
a list that stops matching fails rather than quietly checking nothing.

    python Tools/Scripts/check_wire_field_reads.py          # the gate
    python Tools/Scripts/check_wire_field_reads.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# message -> (header, handler file, handler name). Both handlers have been read in
# full, which is what makes a body-scoped scan exact here.
SUBJECT: dict[str, tuple[str, str, str]] = {
    'RequestQuestSceneUpdate': (
        'Code/encoding/Messages/RequestQuestSceneUpdate.h',
        'Code/server/Services/QuestService.cpp',
        'OnQuestSceneChanges',
    ),
    'NotifyQuestSceneUpdate': (
        'Code/encoding/Messages/NotifyQuestSceneUpdate.h',
        'Code/client/Services/Generic/QuestService.cpp',
        'OnQuestSceneUpdate',
    ),
    # The server's gameplay policy, carried inside AuthenticationResponse and
    # NotifySettingsChange. Unlike a message, a shared struct is read where it is
    # used rather than in the handler that stores it - PvpEnabled in Actor.cpp and
    # MagicTarget.cpp, SyncPlayerHomes in ObjectService, SyncPlayerCalendar in
    # CalendarService - so its scope is the whole client, not one body. The empty
    # handler name says so.
    'ServerSettings': (
        'Code/encoding/Structs/ServerSettings.h',
        'Code/client',
        '',
    ),
}

# Fields that are deliberately not read, with the reason. Empty, and a field that
# stops existing fails rather than staying exempt.
EXEMPT: dict[str, str] = {
    # Sent to every client and read by nobody: auto-party-join is decided entirely
    # on the server (GameServer::AllowsAutoPartyJoin adds the players and sends them
    # SendPartyJoinedEvent), so the client learns the outcome through the ordinary
    # party messages and never needs the flag. It stays on the wire because
    # ServerSettings is serialised as a fixed field list - dropping one would change
    # every field after it - and it is recorded here so the next reader does not have
    # to work out whether a client is supposed to act on it.
    'ServerSettings.AutoPartyJoin': 'informational: auto-join is decided server side and the client '
                                    'learns the result from the party messages',
}

# A wire type, with or without a base: `struct X final : ClientMessage` and the
# plain `struct ServerSettings` both have to parse, because the same rule applies
# to a struct that travels inside a message.
STRUCT = re.compile(r'struct\s+(\w+)\s*(?:final\s*)?(?::|\{)')
MEMBER = re.compile(r'^\s*(?:[A-Za-z_][\w:<>,\s\*&]*?)\s+(\w+)\s*(?:\{[^}]*\})?\s*;')


def read(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        return ''
    return path.read_text(encoding='utf-8', errors='ignore')


def body_of(text: str, start: int) -> str:
    open_at = text.find('{', start)
    if open_at < 0:
        return ''
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[open_at:i]
    return text[open_at:]


def fields_of(text: str) -> list[str]:
    m = STRUCT.search(text)
    if not m:
        return []
    # The struct's own opening brace, not the next one: `struct X final : Base {`
    # puts it at the match end, while `struct X {` puts it one character on - and
    # body_of() searching from the wrong place returned an inline method's body
    # instead of the member list (ServerSettings' operator==), so every field of a
    # struct that declares a method first looked absent.
    open_at = text.find('{', m.end() - 1)
    if open_at < 0:
        return []
    out = []
    for line in body_of(text, open_at).splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(('//', 'static', 'using', 'template')):
            continue
        mm = MEMBER.match(line)
        if mm:
            out.append(mm.group(1))
    return out


def handler_body(rel: str, name: str) -> str:
    text = read(rel)
    definition = re.search(rf'\b\w+::{re.escape(name)}\s*\(', text)
    if not definition:
        return ''
    return body_of(text, definition.end())


def scope_of(rel: str, name: str) -> str:
    """Where a subject's fields may be read: one handler, or a whole directory."""
    if name:
        return handler_body(rel, name)
    directory = ROOT / rel
    if not directory.is_dir():
        return ''
    parts = []
    for path in sorted(directory.rglob('*')):
        if path.suffix in ('.cpp', '.h', '.hpp'):
            parts.append(path.read_text(encoding='utf-8', errors='ignore'))
    return '\n'.join(parts)


def check(failures: list[str], notes: list[str]) -> int:
    checked = 0
    for message, (header_rel, handler_rel, handler_name) in sorted(SUBJECT.items()):
        header = read(header_rel)
        if not header:
            failures.append(f'{header_rel} is missing; {message} cannot be checked')
            continue
        fields = fields_of(header)
        if not fields:
            failures.append(
                f'{header_rel}: no field could be parsed out of {message}, so this subject would be '
                f'checked against nothing')
            continue
        # An empty handler name means the whole directory is the scope, which is how
        # a shared struct is checked: its readers are wherever it is used.
        body = scope_of(handler_rel, handler_name)
        if not body:
            failures.append(
                f'{handler_rel}: {handler_name or "the directory"} yielded nothing to search; the '
                f'scope this gate reads {message} in has moved')
            continue

        seen: set[str] = set()
        for field in fields:
            key = f'{message}.{field}'
            # A read: the field is accessed and not immediately assigned.
            if re.search(rf'(?:\.|->){re.escape(field)}\b(?!\s*=[^=])', body):
                checked += 1
                continue
            if key in EXEMPT:
                seen.add(key)
                note = EXEMPT[key]
                notes.append(f'{key} is exempt: {note}')
                continue
            failures.append(
                f'{handler_rel}: nothing in {handler_name or "that directory"} reads {key}; {message} '
                f'serialises it and the receiver drops it, so whatever fills it has no effect')

        for key in EXEMPT:
            if key.startswith(message + '.') and key not in seen:
                failures.append(f'{key} is exempt but no longer a field of {message}; drop the exemption')

    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked (explicit scope: the two scene-update messages):')
        for message, (header_rel, handler_rel, handler_name) in sorted(SUBJECT.items()):
            fields = fields_of(read(header_rel))
            print(f'  {message}: {fields}')
            print(f'      read in {handler_rel}::{handler_name}')
        print(f'  exemptions: {sorted(EXEMPT) if EXEMPT else "none"}')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    checked = check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'WIRE FIELD READ CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('WIRE FIELD READ CHECK OK')
    print(f'  {checked} field(s) of {len(SUBJECT)} wire type(s) are read where they arrive')
    return 0


if __name__ == '__main__':
    sys.exit(main())
