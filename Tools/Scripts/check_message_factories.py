#!/usr/bin/env python3
"""Keep the opcode tables and the factories that fill them complete.

Every message factory holds a table of extractors, one slot per opcode:

    static std::function<...> s_serverMessageExtractor[kServerOpcodeMax];

and the slots are filled by walking a hand-written type list:

    auto s_visitor = CreateMessageVisitor<AuthenticationResponse, ..., NotifyPluginMessaging>;
    ServerMessageFactory::Visit(extractor);      // extractor assigns [T::Opcode]

Extract() reads the opcode, checks the range, and calls the slot:

    if (data >= kServerOpcodeMax) [[unlikely]] return {nullptr};
    return s_serverMessageExtractor[opcode](aReader);

The range check is not a completeness check. A slot the visitor never named stays
an EMPTY std::function, and calling through an empty std::function throws
std::bad_function_call - from a noexcept function, so the process terminates on a
packet that merely carried an opcode this build has no message for. That is a
remotely triggerable crash reachable by one byte on the wire, and the only thing
standing between the tree and it was that nobody had yet deleted a name from the
list (the server list already carried three names twice, which is what hand
editing this table looks like).

So this gate holds the three lists to each other, for all four factories:

  R1  every opcode in the enum has a message type in the visitor list
  R2  every message type in the visitor list has an opcode in the enum
  R3  no type is listed twice (a repeat is how the list drifts out of step)
  R4  every message header that declares an Opcode is named by a factory

    python Tools/Scripts/check_message_factories.py          # the gate
    python Tools/Scripts/check_message_factories.py --list   # show what is compared
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# (enum name, the header that defines it, the factory header, the messages' folder)
FACTORIES = [
    ('ClientOpcode', 'Code/encoding/Opcodes.h', 'Code/encoding/Messages/ClientMessageFactory.h',
     'Code/encoding/Messages'),
    ('ServerOpcode', 'Code/encoding/Opcodes.h', 'Code/encoding/Messages/ServerMessageFactory.h',
     'Code/encoding/Messages'),
    ('ClientAdminOpcode', 'Code/admin_protocol/Opcodes.h',
     'Code/admin_protocol/AdminMessages/ClientAdminMessageFactory.h',
     'Code/admin_protocol/AdminMessages'),
    ('ServerAdminOpcode', 'Code/admin_protocol/Opcodes.h',
     'Code/admin_protocol/AdminMessages/ServerAdminMessageFactory.h',
     'Code/admin_protocol/AdminMessages'),
]

ENUM = r'enum {name} : unsigned char\s*\{{(.*?)\}};'
VISITOR = r'CreateMessageVisitor<(.*?)>;'
# the header says which enum its opcode belongs to, so no name has to be guessed:
# RemoveSpellRequest declares kRequestRemoveSpell and AdminShutdownRequest declares
# kAdminShutdown, and a gate that assumed the two names match would report both as
# broken. Both message directions also share one folder, so the enum in the
# declaration is the only thing that says which factory a header belongs to.
OPCODE_DECL = r'static constexpr (\w+Opcode) Opcode = (k\w+);'


def read(rel: str) -> str:
    path = ROOT / rel
    return path.read_text(encoding='utf-8', errors='ignore') if path.is_file() else ''


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def enum_members(name: str, header: str) -> list[str]:
    match = re.search(ENUM.format(name=re.escape(name)), strip_comments(read(header)), re.S)
    if not match:
        return []
    return [x for x in re.findall(r'\b(k\w+)\b', match.group(1)) if not x.endswith('OpcodeMax')]


def visitor_types(factory: str) -> list[str]:
    match = re.search(VISITOR, strip_comments(read(factory)), re.S)
    if not match:
        return []
    return [x.strip() for x in re.split(r'[,\s]+', match.group(1)) if x.strip()]


def declared_opcodes(folder: str) -> dict[str, tuple[str, str]]:
    """message header stem -> (the opcode enum it belongs to, the opcode name)."""
    out: dict[str, tuple[str, str]] = {}
    for path in sorted((ROOT / folder).glob('*.h')):
        match = re.search(OPCODE_DECL, strip_comments(read(str(path.relative_to(ROOT)))))
        if match:
            out[path.stem] = (match.group(1), match.group(2))
    return out


def check(failures: list[str], notes: list[str]) -> int:
    checked = 0
    for enum_name, enum_header, factory, folder in FACTORIES:
        members = enum_members(enum_name, enum_header)
        types = visitor_types(factory)
        declared = {stem: value for stem, value in declared_opcodes(folder).items()
                    if value[0] == enum_name}
        rel_factory = factory

        if not members:
            failures.append(f'{enum_header}: {enum_name} could not be read; the enum shape changed')
            continue
        if not types:
            failures.append(f'{rel_factory}: the visitor type list could not be read; this factory '
                            f'would then be reported as complete against nothing')
            continue
        if not declared:
            failures.append(f'{folder}: no header declares a {enum_name} opcode, so this factory '
                            f'would be checked against an empty set of messages')
            continue

        by_opcode = {opcode: stem for stem, (_enum, opcode) in declared.items()}
        named = set(types)

        # R1: every opcode has an extractor slot, i.e. a message type in the visitor.
        for member in members:
            stem = by_opcode.get(member)
            if stem is None:
                failures.append(
                    f'{enum_name} has {member} but no message header declares it; the opcode has no '
                    f'extractor and nothing can decode a packet carrying it')
            elif stem not in named:
                failures.append(
                    f'{rel_factory}: {stem}.h declares {member}, but the visitor list does not name '
                    f'{stem}; its extractor slot stays an empty std::function and Extract() would '
                    f'terminate the process on that opcode')
            else:
                checked += 1

        # R2: every listed type has an opcode in this enum.
        for name in sorted(named):
            entry = declared.get(name)
            if entry is None:
                failures.append(
                    f'{rel_factory}: the visitor list names {name}, which declares no {enum_name} '
                    f'opcode; either the message was never given one or the name is stale')
            elif entry[1] not in members:
                failures.append(
                    f'{rel_factory}: the visitor list names {name}, whose opcode {entry[1]} is not in '
                    f'{enum_name}')

        # R3: no type twice.
        for name in sorted({t for t in types if types.count(t) > 1}):
            failures.append(
                f'{rel_factory}: {name} is listed {types.count(name)} times in the visitor; a repeat '
                f'means the list is being edited by hand and the next edit can drop a name instead '
                f'of repeating one')

        # R4: every message header with an opcode in this enum is reachable.
        for stem, (_enum, opcode) in sorted(declared.items()):
            if opcode not in members:
                failures.append(f'{folder}/{stem}.h declares {opcode}, which is not in {enum_name}')
            elif stem not in named:
                failures.append(
                    f'{folder}/{stem}.h declares {opcode} but {rel_factory} does not name {stem}; the '
                    f'message can be built and sent, and can never be decoded')

    if checked == 0:
        failures.append('no opcode was matched against a message type; this gate covered nothing')

    notes.append(f'{checked} opcode(s) matched to a message type across {len(FACTORIES)} factories')
    return checked


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        for enum_name, enum_header, factory, folder in FACTORIES:
            members = enum_members(enum_name, enum_header)
            types = visitor_types(factory)
            print(f'{enum_name}: {len(members)} opcode(s), {len(types)} visitor entr(ies) '
                  f'({len(set(types))} distinct)')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'MESSAGE FACTORY CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('MESSAGE FACTORY CHECK OK')
    print('  every opcode has an extractor and every message type has an opcode')
    return 0


if __name__ == '__main__':
    sys.exit(main())
