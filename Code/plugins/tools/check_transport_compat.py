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