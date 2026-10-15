#!/usr/bin/env python3
"""Keep the plugin-messaging envelope intact from a plugin's send to a plugin's receive.

`STRPM::Message` carries `flags` and `sequence` next to the payload, and the
standalone bridge puts both inside its own datagrams. The framework transport is
the other backend of the same documented contract, and it is the one that lost
them: `Send` accepted a `flags` argument and never read it, the receive path
filled in `message.flags = STRPM::kMessageNone; message.sequence = 0;`, and
`kMessageAllowLoopback` was not implemented at all. Nothing fails loudly - the
payload arrives, every consumer logs a sequence of 0, and a plugin that asks for
a loopback simply never hears its own message.

The wire messages had nowhere to put the two fields, which is why this was not a
missing assignment: the envelope had to be added to `PluginMessagingRequest` and
`NotifyPluginMessaging`, carried through the server relay, and read back out on
arrival. Five places have to agree, and three of them are invisible to the
compiler - the two .cpp halves are only tied together by the order they write and
read, and the client service can accept a flag set and drop it on the floor
without a warning. That is what this gate reads:

  R1  PluginMessagingService::Send reads its flags parameter inside the body
  R2  Send stamps that parameter into the request, with a non-literal sequence
  R3  OnPluginMessage takes message.flags/sequence from the received message
  R4  both wire messages declare Flags/Sequence and write and read them in one order
  R5  the server relays both fields instead of dropping them

    python Tools/Scripts/check_message_flags_parity.py          # the gate
    python Tools/Scripts/check_message_flags_parity.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

CLIENT_SERVICE = ROOT / 'Code' / 'client' / 'Services' / 'PluginMessagingService.cpp'
SERVER_SERVICE = ROOT / 'Code' / 'server' / 'Services' / 'PluginMessagingService.cpp'
MESSAGES = ROOT / 'Code' / 'encoding' / 'Messages'

# The two wire messages a plugin payload travels in, with the type each envelope
# field has to keep. `Flags` is the contract's uint32 bit set and `Sequence` its
# uint64 counter; widening either one desynchronises every reader of it.
WIRE_MESSAGES = {
    'PluginMessagingRequest': {'Flags': 'std::uint32_t', 'Sequence': 'std::uint64_t'},
    'NotifyPluginMessaging': {'Flags': 'std::uint32_t', 'Sequence': 'std::uint64_t'},
}

ENVELOPE_FIELDS = ('Flags', 'Sequence')

# A constant where a value belongs is the exact shape of the original defect:
# `message.sequence = 0` is a literal, and `message.flags = STRPM::kMessageNone`
# is the enum's zero written out. Both pass every compiler and every other gate.
INTEGER_LITERAL = re.compile(r'^\s*(?:0[xX][0-9a-fA-F]+|\d+)[uUlL]*\s*$')
FLAG_CONSTANT = re.compile(r'^\s*STRPM::kMessage\w*\s*$')

WRITE_FIELD = re.compile(r'Serialization::Write\w+\s*\(\s*aWriter\s*,\s*(\w+)\s*\)')
READ_ASSIGNMENT = re.compile(r'(\w+)\s*=\s*[^;]{0,160}?Serialization::Read\w+\s*\(\s*aReader\s*\)', re.S)
ASSIGNMENT = re.compile(r'(\w+)\s*\.\s*(\w+)\s*=\s*([^;]+);')


def relative(path: pathlib.Path) -> str:
    return path.relative_to(ROOT).as_posix()


def line_of(text: str, offset: int, base_line: int) -> int:
    """The 1-based line of `offset`, counted from the line the enclosing body starts on."""
    return base_line + text.count('\n', 0, offset)


def matching_brace(text: str, start: int) -> int:
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return i
    return len(text)


def find_definition(text: str, qualified: str) -> tuple[str, str, int] | None:
    """(parameter list, body, line the body starts on) for `Class::name(...) {`.

    Only a definition matches: whatever follows the closing parenthesis has to
    reach a `{` before the next `;`, which is what keeps a declaration or a call
    site from being mistaken for the body this check reads.
    """
    for match in re.finditer(re.escape(qualified) + r'\s*\(', text):
        i = match.end() - 1
        depth = 0
        while i < len(text):
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        else:
            return None

        params = text[match.end():i]

        j = i + 1
        while j < len(text) and text[j] not in '{;':
            j += 1
        if j < len(text) and text[j] == '{':
            return params, text[j + 1:matching_brace(text, j)], text.count('\n', 0, j) + 1

    return None


def parameter_names(params: str) -> list[str]:
    """The name of each parameter, in order, with any default value stripped."""
    names: list[str] = []
    depth = 0
    current = ''
    for char in params:
        if char in '(<[':
            depth += 1
        elif char in ')>]':
            depth -= 1
        if char == ',' and depth == 0:
            names.append(current)
            current = ''
        else:
            current += char
    names.append(current)

    found: list[str] = []
    for parameter in names:
        name = re.search(r'(\w+)\s*$', parameter.split('=')[0].strip())
        if name:
            found.append(name.group(1))
    return found


def assignments(body: str) -> list[tuple[str, str, str, int]]:
    """(owner, field, right-hand side, offset) for every `owner.field = value;`."""
    return [(m.group(1), m.group(2), m.group(3).strip(), m.start()) for m in ASSIGNMENT.finditer(body)]


def is_constant(value: str) -> bool:
    return bool(INTEGER_LITERAL.match(value) or FLAG_CONSTANT.match(value))


def check_client(failures: list[str]) -> None:
    text = CLIENT_SERVICE.read_text(encoding='utf-8', errors='ignore')

    send = find_definition(text, 'PluginMessagingService::Send')
    if send is None:
        failures.append(f'{relative(CLIENT_SERVICE)}: PluginMessagingService::Send is not defined here, '
                        f'so nothing proves the plugin-facing flag set reaches the wire')
    else:
        params, body, body_line = send
        names = parameter_names(params)

        # The contract signature is five parameters and the flag set is the last
        # of them; anything else means this check would be reading the wrong one.
        if len(names) != 5:
            failures.append(
                f'{relative(CLIENT_SERVICE)}:{body_line - 1}: Send takes {len(names)} parameter(s); the '
                f'contract signature has five (channel, target, data, size, flags)')
        else:
            flags_param = names[-1]

            if not re.search(r'\b' + re.escape(flags_param) + r'\b', body):
                failures.append(
                    f'{relative(CLIENT_SERVICE)}:{body_line - 1}: Send accepts `{flags_param}` and never '
                    f'reads it in the body - a plugin\'s flags stop at this function')

            found = assignments(body)

            if not any(field == 'Flags' and value == flags_param for _owner, field, value, _pos in found):
                failures.append(
                    f'{relative(CLIENT_SERVICE)}:{body_line - 1}: Send never copies `{flags_param}` into the '
                    f'request (`Flags = {flags_param};` is missing), so the flag set stops here')

            sequences = [a for a in found if a[1] == 'Sequence']
            if not sequences:
                failures.append(
                    f'{relative(CLIENT_SERVICE)}:{body_line - 1}: Send never stamps a sequence on the '
                    f'request, so every receiver is handed the same one')
            for _owner, _field, value, pos in sequences:
                if is_constant(value):
                    failures.append(
                        f'{relative(CLIENT_SERVICE)}:{line_of(body, pos, body_line)}: `Sequence = {value}` is '
                        f'a constant, so every payload this client sends carries the same sequence')

    receive = find_definition(text, 'PluginMessagingService::OnPluginMessage')
    if receive is None:
        failures.append(f'{relative(CLIENT_SERVICE)}: PluginMessagingService::OnPluginMessage is not defined '
                        f'here, so nothing proves the received envelope is handed to the plugins')
    else:
        params, body, body_line = receive
        names = parameter_names(params)
        incoming = names[0] if names else ''
        found = assignments(body)

        # The message field is the contract's lowercase `flags`/`sequence`; the
        # wire field is the one this fix added to the notify.
        for field, wire_field in (('flags', 'Flags'), ('sequence', 'Sequence')):
            matches = [a for a in found if a[1] == field]
            if not matches:
                failures.append(
                    f'{relative(CLIENT_SERVICE)}:{body_line - 1}: OnPluginMessage never sets '
                    f'`message.{field}`, so a receiving plugin is handed a default')
                continue
            for _owner, _field, value, pos in matches:
                line = line_of(body, pos, body_line)
                if is_constant(value):
                    failures.append(
                        f'{relative(CLIENT_SERVICE)}:{line}: `message.{field} = {value};` is a constant, so '
                        f'every payload this client receives reports the same {field}')
                elif value != f'{incoming}.{wire_field}':
                    failures.append(
                        f'{relative(CLIENT_SERVICE)}:{line}: `message.{field} = {value};` does not come from '
                        f'the received message; it has to be `{incoming}.{wire_field}`')


def check_wire_message(name: str, field_types: dict[str, str], failures: list[str]) -> None:
    header = MESSAGES / f'{name}.h'
    source = MESSAGES / f'{name}.cpp'

    header_text = header.read_text(encoding='utf-8', errors='ignore')
    for field, field_type in field_types.items():
        if not re.search(r'\b' + re.escape(field_type) + r'\s+' + field + r'\s*\{', header_text):
            failures.append(
                f'{relative(header)}: no `{field_type} {field}` declaration; the wire message cannot carry '
                f'it and the receiver has to invent a value')

    source_text = source.read_text(encoding='utf-8', errors='ignore')
    write = find_definition(source_text, f'{name}::SerializeRaw')
    read = find_definition(source_text, f'{name}::DeserializeRaw')

    if write is None or read is None:
        missing = 'SerializeRaw' if write is None else 'DeserializeRaw'
        failures.append(f'{relative(source)}: {name}::{missing} is not defined here, so the two halves of '
                        f'this message cannot be compared')
        return

    written = [m.group(1) for m in WRITE_FIELD.finditer(write[1]) if m.group(1) in ENVELOPE_FIELDS]
    read_back = [m.group(1) for m in READ_ASSIGNMENT.finditer(read[1]) if m.group(1) in ENVELOPE_FIELDS]

    if written == read_back and sorted(written) == sorted(ENVELOPE_FIELDS):
        return

    detail = [
        f'    write: {" ".join(written) or "(nothing)"}',
        f'    read : {" ".join(read_back) or "(nothing)"}',
    ]
    if sorted(written) != sorted(ENVELOPE_FIELDS) or sorted(read_back) != sorted(ENVELOPE_FIELDS):
        detail.append(f'    both halves have to carry {" and ".join(ENVELOPE_FIELDS)} exactly once')
    elif written != read_back:
        detail.append('    same fields, DIFFERENT ORDER')
    failures.append(f'{relative(source)}: {name}::SerializeRaw and {name}::DeserializeRaw disagree about the '
                    f'envelope\n' + '\n'.join(detail))


def check_server(failures: list[str]) -> None:
    text = SERVER_SERVICE.read_text(encoding='utf-8', errors='ignore')
    relay = find_definition(text, 'PluginMessagingService::OnPluginMessage')

    if relay is None:
        failures.append(f'{relative(SERVER_SERVICE)}: PluginMessagingService::OnPluginMessage is not defined '
                        f'here, so nothing proves the relay carries the envelope')
        return

    _params, body, body_line = relay
    for field in ENVELOPE_FIELDS:
        pattern = re.compile(r'\b\w+\s*\.\s*' + field + r'\s*=\s*\w+\s*\.\s*' + field + r'\b')
        if not pattern.search(body):
            failures.append(
                f'{relative(SERVER_SERVICE)}:{body_line - 1}: the server never relays `{field}` from the '
                f'request into the notification, so it arrives at every receiver as a default')


def check(failures: list[str]) -> None:
    sources = [CLIENT_SERVICE, SERVER_SERVICE]
    for name in WIRE_MESSAGES:
        sources.extend((MESSAGES / f'{name}.h', MESSAGES / f'{name}.cpp'))

    missing = [path for path in sources if not path.is_file()]
    if missing:
        for path in missing:
            failures.append(f'{relative(path)}: missing, so this check covers nothing here')
        return

    check_client(failures)
    for name, field_types in WIRE_MESSAGES.items():
        check_wire_message(name, field_types, failures)
    check_server(failures)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print('  R1  PluginMessagingService::Send reads its flags parameter inside the body')
        print('  R2  Send stamps that parameter into the request, with a non-literal sequence')
        print('  R3  OnPluginMessage takes message.flags/sequence from the received message')
        print('  R4  both wire messages declare Flags/Sequence and write and read them in one order')
        print('  R5  the server relays both fields instead of dropping them')
        return 0

    failures: list[str] = []
    check(failures)

    if failures:
        print(f'MESSAGE FLAGS PARITY CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('MESSAGE FLAGS PARITY CHECK OK')
    print('  the flags and the sequence a plugin sends are the ones a receiver is handed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
