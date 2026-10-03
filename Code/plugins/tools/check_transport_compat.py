#!/usr/bin/env python3
"""Keep the documented plugin-transport compatibility claims true.

The companion-plugin documentation states facts that are cheap to break and
expensive to notice: that the two plugin opcodes were appended rather than
inserted, that they sit at or above the previous maximum, and that the chat
opcodes the standalone bridge matches on keep their indices. Those are the
claims a reader relies on when reasoning about a mixed-version session, so they
are checked against the source instead of trusted.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
OPCODES_REL = 'Code/encoding/Opcodes.h'

# The standalone STRPM bridge matches these two indices in the official 1.8.0
# build; this fork has to keep the same numbering for the fallback to work.
CHAT_SEND_INDEX = 38
CHAT_BROADCAST_INDEX = 36

INI_REL = 'Code/plugins/packaging/STRPluginMessagingAPI.ini'
DOC_REL = 'docs/COMPANION-PLUGINS.md'


def members(text: str, enum_name: str) -> list[str]:
    body = re.search(r'enum ' + enum_name + r'[^{]*\{(.*?)\n\};', text, re.S).group(1)
    out = []
    for line in body.splitlines():
        line = line.split('//')[0].strip()
        if not line:
            continue
        out.append(line.rstrip(',').split('=')[0].strip())
    return out


text = (ROOT / OPCODES_REL).read_text(encoding='utf-8')
client = members(text, 'ClientOpcode')
server = members(text, 'ServerOpcode')

failures: list[str] = []


def index_of(names: list[str], member: str) -> int:
    if member not in names:
        failures.append(f'{member} is missing from {OPCODES_REL}')
        return -1
    return names.index(member)


send_index = index_of(client, 'kSendChatMessageRequest')
broadcast_index = index_of(server, 'kNotifyChatMessageBroadcast')
request_index = index_of(client, 'kPluginMessagingRequest')
notify_index = index_of(server, 'kNotifyPluginMessaging')

if send_index >= 0 and send_index != CHAT_SEND_INDEX:
    failures.append(
        f'kSendChatMessageRequest moved to index {send_index}; the standalone bridge matches '
        f'{CHAT_SEND_INDEX} and would stop working. Appending is fine, inserting is not.'
    )

if broadcast_index >= 0 and broadcast_index != CHAT_BROADCAST_INDEX:
    failures.append(
        f'kNotifyChatMessageBroadcast moved to index {broadcast_index}; the standalone bridge '
        f'matches {CHAT_BROADCAST_INDEX} and would stop working.'
    )

# An older peer rejects any opcode at or above its own maximum, so the new
# opcodes must not sit below the pre-existing ones.
if request_index >= 0 and 'kSetTimeCommandRequest' in client:
    if request_index <= client.index('kSetTimeCommandRequest'):
        failures.append('kPluginMessagingRequest was inserted before existing client opcodes')

if notify_index >= 0 and 'kNotifySetTimeResult' in server:
    if notify_index <= server.index('kNotifySetTimeResult'):
        failures.append('kNotifyPluginMessaging was inserted before existing server opcodes')

# The packaged ini decides which transport a companion plugin actually lands
# on, and this document states which one that is. They drifted apart once
# already: the ini was moved off the framework runtime and the document kept
# claiming the runtime was named, which is exactly the kind of claim a reader
# plans a deployment around. Bind the prose to the shipped value.
ini_text = (ROOT / INI_REL).read_text(encoding='utf-8')
shipped = re.search(r'^\s*STRBridgeModule\s*=\s*(\S+)\s*$', ini_text, re.M)
shipped_value = shipped.group(1) if shipped else None
doc_text = (ROOT / DOC_REL).read_text(encoding='utf-8')

if shipped_value is None:
    failures.append(
        f'{INI_REL} has no STRBridgeModule setting; the facade would silently '
        f'fall back to its own built-in default'
    )
else:
    if shipped_value not in doc_text:
        failures.append(
            f'{DOC_REL} never names {shipped_value}, which is what {INI_REL} '
            f'ships as STRBridgeModule; a reader cannot tell which transport a '
            f'companion plugin lands on'
        )
    if 'names the framework runtime' in doc_text:
        failures.append(
            f'{DOC_REL} still claims the packaged ini names the framework '
            f'runtime, but {INI_REL} names {shipped_value}'
        )

# The document now states that the facade prefers an already-loaded framework
# runtime over the name the ini ships. That claim is only true while the patch
# implementing it is present, and a reverted or dropped patch would leave the
# prose describing a transport the plugins do not actually reach - the same
# class of drift the STRBridgeModule check above exists to catch.
PATCH_REL = 'Code/plugins/patches/STRPluginMessagingAPI/0001-prefer-the-framework-transport.patch'
probe_claim = 'already loaded'
if 'GetModuleHandleW' not in doc_text or probe_claim not in doc_text:
    failures.append(
        f'{DOC_REL} does not describe the already-loaded-runtime probe; a reader '
        f'cannot tell that the facade prefers the framework over the ini value'
    )
else:
    patch_path = ROOT / PATCH_REL
    if not patch_path.is_file():
        failures.append(
            f'{PATCH_REL} is missing, but {DOC_REL} says the facade probes for an '
            f'already-loaded framework runtime'
        )
    else:
        patch_text = patch_path.read_text(encoding='utf-8')
        for required in ('GetModuleHandleW', 'SkyrimTogetherRuntime.dll',
                         'SkyrimTogetherRuntime_1_5.dll'):
            if required not in patch_text:
                failures.append(
                    f'{PATCH_REL} does not mention {required}; it no longer '
                    f'implements the probe {DOC_REL} describes'
                )
        # Only the patch's own added lines matter, and only their code: the
        # comment explaining why LoadLibraryW is not used would otherwise read
        # as the violation it warns against.
        added_code = [
            line[1:].split('//')[0]
            for line in patch_text.splitlines()
            if line.startswith('+') and not line.startswith('+++')
        ]
        if any('LoadLibraryW' in line for line in added_code):
            failures.append(
                f'{PATCH_REL} calls LoadLibraryW in added code; the probe must '
                f'never load a runtime, because the two are different ABIs and '
                f'both sit on disk after any install'
            )

# The companion plugins do not open a socket of their own: they ride the chat field.
# Three separate limits decide whether an envelope survives that trip, they live in
# three files that do not reference each other, and the smallest one silently wins.
# They drifted apart once already - the wire bound was set to one chat line while the
# relay resource accepted eight times that - and the failure was invisible: what got
# cut was the tail of a hex payload, and a shortened hex payload is still valid hex,
# so the far side decoded a shorter buffer instead of rejecting the message. Bind the
# wire bound to the relay's own limit so the transport contract is expressible.
ENVELOPE_REL = 'plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua'
WIRE_LIMITS_REL = 'Code/encoding/WireLimits.h'

envelope_path = ROOT / ENVELOPE_REL
if not envelope_path.is_file():
    failures.append(
        f'{ENVELOPE_REL} is missing, but it is the relay that defines how large a STRPM '
        f'envelope may be; without it the wire bound cannot be checked against the transport'
    )
else:
    relay_text = envelope_path.read_text(encoding='utf-8')
    relay_limit = re.search(r'^\s*local\s+MAX_ENVELOPE_LENGTH\s*=\s*(\d+)', relay_text, re.M)
    wire_text = (ROOT / WIRE_LIMITS_REL).read_text(encoding='utf-8')
    wire_limit = re.search(r'kMaxChatMessageBytes\s*=\s*([0-9+\s]+);', wire_text)
    if relay_limit is None:
        failures.append(
            f'{ENVELOPE_REL} no longer declares MAX_ENVELOPE_LENGTH; the envelope size the '
            f'bridge fragments for cannot be read from it any more'
        )
    elif wire_limit is None:
        failures.append(
            f'{WIRE_LIMITS_REL} no longer declares kMaxChatMessageBytes; the bound on the '
            f'field the STRPM envelope rides cannot be read from it any more'
        )
    else:
        relay_chars = int(relay_limit.group(1))
        # The bound is written as a sum so its intent survives a reader who only sees the
        # header: the relay's own limit plus the metadata the relay appends to it.
        wire_bytes = sum(int(part) for part in wire_limit.group(1).split('+'))
        # The relay appends its own authenticated sender metadata to every envelope it
        # forwards, so the wire bound has to cover the envelope plus that tail. The bridge
        # also fragments below the relay limit for the same reason.
        if wire_bytes <= relay_chars:
            failures.append(
                f'{WIRE_LIMITS_REL} bounds kMaxChatMessageBytes to {wire_bytes} bytes, but '
                f'{ENVELOPE_REL} accepts an envelope of up to {relay_chars} characters and then '
                f'appends its own sender metadata before relaying it. A bound below the relay '
                f'limit truncates the envelope, and a truncated hex payload is still valid '
                f'hex, so the receiver decodes a shorter buffer instead of dropping the message'
            )

# Every length field inside a packet is a claim about how much of it is there, and the
# reader allocates from those claims. The string bounds in WireLimits.h run after the read,
# so they cannot bound that allocation; the packet size can, and it is the one thing the
# receive entry points know and the encoding layer does not. There are three of them: the
# game's two, plus the admin client, which reads its own transport. A bound below what the
# senders produce would silently drop legitimate traffic instead - the server serialises
# into 1 << 20, the client and the admin client into 1 << 16 - and the failure would look
# like a desync rather than like a dropped packet, so both halves are checked here.
PACKET_LIMIT_REL = 'Code/encoding/WireLimits.h'
PACKET_GATE_SITES = (
    ('Code/server/GameServer.cpp', 'GameServer::OnConsume'),
    ('Code/client/Services/Generic/TransportService.cpp', 'TransportService::OnConsume'),
    ('Code/admin/Transport.cpp', 'AdminApp::OnConsume'),
)

packet_text = (ROOT / PACKET_LIMIT_REL).read_text(encoding='utf-8')
packet_limit = re.search(r'kMaxPacketBytes\s*=\s*(1\s*<<\s*(\d+))', packet_text)
if packet_limit is None:
    failures.append(
        f'{PACKET_LIMIT_REL} no longer declares kMaxPacketBytes; a packet of any size is'
        f' handed to the parser again, and every length field inside it is believed'
    )
else:
    packet_bytes = 1 << int(packet_limit.group(2))
    # The server's own send buffer. Anything below it rejects packets this framework wrote.
    if packet_bytes < (1 << 20):
        failures.append(
            f'{PACKET_LIMIT_REL} bounds kMaxPacketBytes to {packet_bytes}, but the server'
            f' serialises into a 1 << 20 buffer before handing the bytes to the transport, so'
            f' this drops packets the framework itself produces'
        )
    for rel, entry in PACKET_GATE_SITES:
        path = ROOT / rel
        if not path.is_file():
            failures.append(f'{rel} is missing, but it is a receive entry point')
            continue
        source = path.read_text(encoding='utf-8')
        # The definition, not a mention: bound the body at the closing brace.
        start = source.find(f'{entry}(')
        end = source.find('\n}\n', start) if start >= 0 else -1
        body = source[start:end] if start >= 0 and end > start else ''
        if 'WireLimits::WithinPacketLimit' not in body:
            failures.append(
                f'{rel} {entry} does not call WireLimits::WithinPacketLimit; the size of the'
                f' packet is known only there, so without it the bound does nothing'
            )

# The relay appends its own sender metadata to the tail of every envelope it forwards,
# and the bridge reads that tail back. The envelope in front of it is written by a peer,
# and both sides split fields by scanning for '|': the relay's own parser keeps the last
# occurrence of a name while the bridge's kept the first. A peer that wrote '|sender=' or
# '|senderName=' into its own envelope therefore had its value win on the receiving client
# while the relay's authenticated value was ignored - the sender name is the key every
# companion plugin uses to recognise a peer, so this was identity spoofing. The relay now
# refuses the reserved names and the bridge reads them from the tail. Both halves are
# checked here, because either one alone restores the old behaviour: a tail reader with no
# relay-side refusal still lets the peer's copy sit in the envelope, and a relay-side
# refusal with a first-match reader still trusts it if any other path forwards one.
RECEIVE_REL = 'plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp'
RESOLVER_REL = 'plugins/STRPluginMessagingAPI/src/STRPMProxyResolverBridge.h'
RESERVED_FIELD_CLAIM = ('sender', 'senderPlayerId', 'senderName', 'serverTick')

receive_path = ROOT / RECEIVE_REL
resolver_path = ROOT / RESOLVER_REL
for missing in (envelope_path, receive_path, resolver_path):
    if not missing.is_file():
        failures.append(
            f'{missing.relative_to(ROOT).as_posix()} is missing, but it is one of the three'
            f' files that decide whether a relayed envelope is attributed to its real sender'
        )

if envelope_path.is_file():
    relay_text = envelope_path.read_text(encoding='utf-8')
    if 'RESERVED_FIELDS' not in relay_text:
        failures.append(
            f'{ENVELOPE_REL} no longer declares RESERVED_FIELDS; a client can write'
            f' |sender=/|senderName= into its own envelope again and the relay will append'
            f' its authenticated copy behind the forged one'
        )
    else:
        for name in RESERVED_FIELD_CLAIM:
            if f'"{name}"' not in relay_text:
                failures.append(
                    f'{ENVELOPE_REL} no longer reserves {name}, which the relay writes itself'
                )
        # The definition alone proves nothing; the call is what drops the envelope.
        refusal = relay_text.find('= reservedFieldPresent(fields)')
        append = relay_text.find('"|sender="')
        if refusal < 0:
            failures.append(
                f'{ENVELOPE_REL} declares RESERVED_FIELDS but never calls reservedFieldPresent,'
                f' so a reserved field in a peer envelope is forwarded unexamined'
            )
        elif append >= 0 and refusal > append:
            failures.append(
                f'{ENVELOPE_REL} checks for reserved fields after it appends its own sender'
                f' metadata; the peer envelope has already been forwarded by then'
            )

for path, rel in ((receive_path, RECEIVE_REL), (resolver_path, RESOLVER_REL)):
    if not path.is_file():
        continue
    source = path.read_text(encoding='utf-8')
    body = re.search(r'ReadRelayField\(std::string_view packet[^)]*\)\s*\{(.*?)\n        \}', source, re.S)
    if body is None:
        failures.append(
            f'{rel} has no ReadRelayField; the relay fields it reads are the ones a peer can'
            f' write into its own envelope, so they have to be read from the tail'
        )
    elif 'found = token.substr' not in body.group(1) or 'return token.substr' in body.group(1):
        failures.append(
            f'{rel} ReadRelayField returns the first match again; the peer writes the head of'
            f' the envelope and the relay writes the tail, so the first match is the forged one'
        )
    for name in ('sender', 'senderName' if path is receive_path else 'senderPlayerId'):
        if f'ReadRelayField(envelope, "{name}")' not in source:
            failures.append(
                f'{rel} reads {name} with ReadField rather than ReadRelayField; the relay'
                f' writes that name at the tail of the envelope, not in the peer-written head'
            )

print(f'client opcodes: {len(client) - 1} in use, plugin request at index {request_index}')
print(f'server opcodes: {len(server) - 1} in use, plugin notify at index {notify_index}')
print(f'chat opcodes  : send {send_index}, broadcast {broadcast_index}')

if failures:
    print()
    print(f'TRANSPORT COMPATIBILITY FAILED ({len(failures)})')
    for failure in failures:
        print('  -', failure)
    sys.exit(1)

print()
print('TRANSPORT COMPATIBILITY OK')
print('  new opcodes appended, chat indices unchanged, docs remain accurate')