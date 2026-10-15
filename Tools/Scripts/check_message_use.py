#!/usr/bin/env python3
"""Keep every wire message either sent or handled - no orphans in the table.

A message that exists in the factory, has an opcode and serialises fine can still
be dead: nothing constructs it, or nothing subscribes to it. That is how
NotifyDrawWeapon shipped. The client sent DrawWeaponRequest when a player drew or
sheathed, the server applied it to its own CharacterComponent and stopped, and the
notify - which was in the enum, in the factory, in the visitor list and #included
by the client's InventoryService - was never constructed by anyone. The drawn state
was only replicated at spawn, so a player who drew after that was invisible to
everyone who already had them loaded. Nothing failed; the feature was just absent.

Two rules, both mechanical:

  R1  a ClientMessage must be either sent by the client or handled by the server
  R2  a ServerMessage must be either sent by the server or handled by the client

"Sent" is a construction (`T x;` / `MakeUnique<T>` / a `T` parameter), "handled" is
a dispatcher subscription (`sink<T>` / `sink<PacketEvent<T>>`). Both sides are
read from the tree, so a message used only by tests or only by the plugin protocol
is not counted.

    python Tools/Scripts/check_message_use.py          # the gate
    python Tools/Scripts/check_message_use.py --list   # show what is compared
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CODE = ROOT / 'Code'

# (label, factory header, the side that receives it, the side that sends it)
DIRECTIONS = [
    ('ClientMessage', 'Code/encoding/Messages/ClientMessageFactory.h', 'server', 'client'),
    ('ServerMessage', 'Code/encoding/Messages/ServerMessageFactory.h', 'client', 'server'),
]

# Handled outside the dispatcher, by the override in GameServer/TransportService
# that runs after the generated handler table is filled. Listed with the reason so
# a new entry cannot be added just to silence the gate.
HANDLED_SPECIALLY = {
    'AuthenticationRequest': 'overridden in GameServer::HandleAuthenticationRequest',
    'AuthenticationResponse': 'overridden in TransportService::HandleAuthenticationResponse',
}

# Sent or handled somewhere the scan cannot see, with the reason.
EXEMPT: dict[str, str] = {
    # A message type that exists end to end and has no producer. The feature it was
    # written for is live through another message - player dialogue is broadcast as
    # NotifyChatMessageBroadcast with MessageType kPlayerDialogue (server
    # OverlayService::sendPlayerMessage), which is why the client's
    # OverlayService::OnPlayerDialogue never runs. The opcode and the factory slot
    # stay: Opcodes.h says entries are only ever appended, so removing a mid-table
    # enumerator would renumber every opcode after it and break older plugins that
    # hook the table by index.
    'NotifyPlayerDialogue': 'superseded by NotifyChatMessageBroadcast with kPlayerDialogue; the '
                            'opcode stays because Opcodes.h only ever appends',
    # Reserved: container inventory changes are carried by the object messages
    # (ObjectService / AssignObjectsRequest), and these two have never had a sender
    # or a handler. Kept for the same append-only reason, and recorded here so the
    # next reader does not have to work out whether they are wired up somewhere.
    'RequestObjectInventoryChanges': 'reserved, never wired: container inventory goes through '
                                     'ObjectService; the opcode stays because Opcodes.h only appends',
    'NotifyObjectInventoryChanges': 'reserved, never wired: see RequestObjectInventoryChanges',
}

VISITOR = r'CreateMessageVisitor<(.*?)>;'
# A construction, in any of the forms this tree uses:
#   Foo foo;                     a local
#   Foo()                        a temporary passed straight to Send()
#   MakeUnique<Foo>()            the factories
#   Map<Player*, Foo> messages;  a container keyed by player (the batch broadcasts)
#   const Foo& / Foo*            a parameter or member
# The first version only matched the first two and only inside the sending side's
# folder, so it called StringCacheUpdate (built in the shared encoding library) and
# the batch-broadcast maps dead. A gate that reports working code is worse than no
# gate: it sends someone to "fix" a message that is being sent.
CONSTRUCT = (r'(?:\b{name}\s+\w+\s*[;{{(=]'
             r'|\b{name}\s*\(\s*\)'
             r'|MakeUnique<\s*{name}\s*>'
             r'|<\s*(?:\w+\s*\*?\s*,\s*)*{name}\s*>'
             r'|\b{name}\s*[&*]'
             r'|const\s+{name}\b)')
SINK = r'sink<\s*(?:PacketEvent<\s*)?{name}\s*>?\s*>'


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding='utf-8', errors='ignore')


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def message_types(factory: str) -> list[str]:
    match = re.search(VISITOR, strip_comments(read(ROOT / factory)), re.S)
    if not match:
        return []
    return [x.strip() for x in re.split(r'[,\s]+', match.group(1)) if x.strip()]


def side_files(side: str) -> list[pathlib.Path]:
    directory = CODE / side
    if not directory.is_dir():
        return []
    return sorted(p for p in directory.rglob('*') if p.suffix in ('.cpp', '.h', '.hpp'))


def sources(side: str, name: str) -> str:
    """Everything on one side that could construct or subscribe to `name`.

    The message's own header and source are excluded: they hold its constructor and
    its serialisation, which are not a use. Counting them made every message look
    sent - an earlier version of this gate included the whole encoding folder and
    reported OK while NotifyDrawWeapon had no producer anywhere.
    """
    own = {f'{name}.h', f'{name}.cpp'}
    parts = [strip_comments(read(p)) for p in side_files(side) if p.name not in own]
    parts += [strip_comments(read(p)) for p in sorted((CODE / 'encoding').rglob('*.cpp'))
              if p.name not in own]
    return '\n'.join(parts)


def check(failures: list[str], notes: list[str]) -> int:
    checked = 0
    for label, factory, receiver, sender in DIRECTIONS:
        types = message_types(factory)
        if not types:
            failures.append(f'{factory}: the visitor type list could not be read, so no message in '
                            f'this direction would be checked')
            continue

        for name in sorted(set(types)):
            checked += 1
            if name in EXEMPT:
                notes.append(f'{name} is exempt: {EXEMPT[name]}')
                continue
            # Where a message is sent: the sending side's sources plus the shared
            # encoding library (StringCache builds its updates there), minus the
            # message's own files - its constructor is not a use.
            sender_text = sources(sender, name)
            receiver_text = sources(receiver, name)
            sent = re.search(CONSTRUCT.format(name=name), sender_text) is not None
            handled = re.search(SINK.format(name=name), receiver_text) is not None
            if not sent and not handled:
                failures.append(
                    f'{name} is neither sent by the {sender} nor handled by the {receiver}: the message '
                    f'exists in the factory and on the wire and nothing uses it. Either wire it up or '
                    f'remove it')
                continue
            if not sent:
                if name in HANDLED_SPECIALLY:
                    notes.append(f'{name} is handled specially ({HANDLED_SPECIALLY[name]}) and nothing '
                                 f'constructs it outside tests; that is expected for a handshake')
                    continue
                # A handler nobody feeds is the same defect as a sender nobody
                # listens to: the state it updates can never change. This is the
                # branch that would have caught NotifyDrawWeapon before the fix -
                # the client subscribed to it, so "neither sent nor handled" was
                # false and the gate passed while the feature was absent.
                failures.append(
                    f'{name} is handled by the {receiver} but nothing on the {sender} constructs it; the '
                    f'handler can never run and whatever it updates can never change')
            elif not handled:
                if name in HANDLED_SPECIALLY:
                    continue
                failures.append(
                    f'{name} is sent by the {sender} but the {receiver} never subscribes to it; every '
                    f'packet is decoded and dropped')

    for name in sorted(set(EXEMPT) | set(HANDLED_SPECIALLY)):
        if not any(name in message_types(factory) for _l, factory, _r, _s in DIRECTIONS):
            failures.append(f'{name} is listed here but is not a message in any factory; drop the entry '
                            f'rather than leaving it to cover nothing')

    if checked == 0:
        failures.append('no message was checked; this gate covered nothing')
    notes.append(f'{checked} message(s) checked across {len(DIRECTIONS)} direction(s)')
    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        for label, factory, receiver, sender in DIRECTIONS:
            types = sorted(set(message_types(factory)))
            print(f'{label}: {len(types)} message(s) - sent by {sender}, handled by {receiver}')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'MESSAGE USE CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('MESSAGE USE CHECK OK')
    print('  every wire message is either sent or handled')
    return 0


if __name__ == '__main__':
    sys.exit(main())
