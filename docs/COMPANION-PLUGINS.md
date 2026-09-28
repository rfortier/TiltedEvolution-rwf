# Companion plugins: transport, compatibility, and what happens when it is absent

This document is for whoever has to debug a companion-plugin problem, and for
whoever decides what this framework promises. It records which transport a
payload actually travels on, when the fallback applies, and which combinations
are known to work.

## The two transports

A companion plugin moves bytes between players in one of two ways.

| | Native framework transport | Standalone chat-tunnel bridge |
| --- | --- | --- |
| Module that implements it | SkyrimTogetherRuntime.dll (this repository) | STRPluginMessagingBridge.dll (STRPluginMessagingAPI) |
| Wire representation | a real framework message (kPluginMessagingRequest / kNotifyPluginMessaging) | STRPM-prefixed text envelopes inside chat |
| Requires a matching server | yes: this framework's server | no: works on an unmodified Skyrim Together Reborn server with the relay resource installed |
| Payload ceiling | 24 KiB per message | 24 KiB split into chat-sized fragments |
| How it finds the host | it *is* the host | scans the running process for a private logging anchor |
| Peer to local actor mapping | from the framework's own entity components | from a proxy table the bridge maintains |
| Survives a framework update | yes, it is compiled against it | no, it is bound to one exact official build |

The native transport is the default and the supported path. The bridge exists
for one scenario: players whose session is hosted by an **unmodified Skyrim
Together Reborn server**, which has no plugin channel of its own and can only
carry bytes inside chat.

## How a transport gets chosen

```text
plugin calls STR_QueryPluginMessagingInterface
        |
        +-- SkyrimTogetherRuntime.dll exports it  ->  native transport, done
        |
        +-- STRPluginMessagingAPI.dll is loaded instead
                |
                +-- reads [Transport] from STRPluginMessagingAPI.ini
                        |
                        +-- STRBridgeModule = SkyrimTogetherRuntime.dll
                        |     -> the facade receives payloads through the
                        |        native transport and dispatches them itself
                        |
                        +-- STRBridgeModule = STRPluginMessagingBridge.dll
                              -> chat-tunnel bridge; needs the official
                                 1.8.0 client and the server relay resource
```

Both modules export the same four entry points, so which one a plugin gets is
decided by which module it queries - and, for the facade, by the ini.

The packaged ini still names the **bridge**, but inside this framework the
facade no longer trusts that name alone. Before loading what the ini names, it
probes for a framework runtime that is *already loaded* - both possible names -
and uses that in preference. This repository patches the facade to do so
(`Code/plugins/patches/STRPluginMessagingAPI/`), because the ini alone cannot
express the choice: the runtime is SkyrimTogetherRuntime.dll on 1.6.x/1.7.x and
SkyrimTogetherRuntime_1_5.dll on 1.5.x, the facade's loader matches exact module
names, and one ini has one slot for the name.

The probe deliberately uses `GetModuleHandleW` and **never `LoadLibraryW`**. The
two runtimes are different ABIs compiled from different struct layouts, and both
files sit in the game root after any install, so loading a name that is merely
present on disk would pull a cross-ABI image into the process. A runtime that is
not already loaded is not this plugin's to load; when neither is found the ini's
value is used exactly as before, so the standalone-bridge scenario is unchanged.

The consequence is worth stating plainly: **a plugin that loads
STRPluginMessagingAPI.dll by name now lands on the framework's own transport
inside this framework**, even though none of the seven companion plugins
queries the framework directly - they all hard-code the facade's module name.

That matters because the bridge cannot work here at all. Its receive resolver
scans `SkyrimSE.exe`'s own allocation for the framework's `TransportService`
RTTI, and that object lives in a separate module, so the resolver never becomes
ready, `Send` returns `kNotConnected` on every call, and plugin sync fails
silently. The packaged ini names the bridge only so that a session hosted by an
unmodified Skyrim Together Reborn server still has a transport; that path is
unaffected by the probe, which finds no framework runtime there.

## Identity

A plugin addresses a peer by echoing back the connectionID it was given for a
Sender. Inside this framework that value is the **PlayerId** the server
authenticates, not a transport connection handle: a client is never told its
own connection handle and could not address with it.

SenderPlayerId is filled in by the server from the connection a request arrived
on, never from the payload, so a plugin cannot speak for another player. A
request naming an unknown PlayerId is dropped - that is a normal race against a
disconnect, not an error worth logging loudly.

## Known-good combinations

| Framework | Server | Companion plugins | Result |
| --- | --- | --- | --- |
| this framework | this framework's server | any subset, same versions | native transport; supported |
| this framework | this framework's server | mismatched plugin versions | payloads still flow; a plugin that changed its own payload format will misread a peer and must tolerate it |
| this framework | unmodified STR Reborn server | any subset | native transport unavailable; set STRBridgeModule=STRPluginMessagingBridge.dll and install the server relay resource |
| older framework build | newer server | any | the new opcodes are rejected by the old peer and vice versa; see below |

## Version skew between framework builds

kPluginMessagingRequest and kNotifyPluginMessaging were appended to the opcode
tables, never inserted, and they sit exactly at the old OpcodeMax. That is
deliberate: a peer built before them reads the opcode, sees a value at or above
its own maximum, and rejects the packet instead of misparsing it into an
unrelated message. The observable behaviour of a mixed session is therefore
"plugin sync silently does nothing", not corruption or a crash.

kSendChatMessageRequest and kNotifyChatMessageBroadcast keep indices 38 and 36
across this change, which is what the chat-tunnel bridge matches on, so a client
still running the bridge stays compatible.

## When plugin sync does not work

1. **Both players must install the same plugin, at the same version.** The
   framework relays bytes and does not compare plugin versions. A plugin that
   changes its payload format will misread a peer on an older build.
2. **Check the log for the transport in use.** STRPM-prefixed lines come from
   the framework's transport; the bridge logs to its own file.
3. **A rate-limited sender is dropped, not queued.** The server charges per
   byte with a burst allowance, so a plugin sending large snapshots in a tight
   loop will lose payloads while ordinary traffic is unaffected.
4. **ProxyResolver answers from the framework's own components.** A peer is
   resolved to the local entity that represents them, and `kTargetNotFound` is a
   normal answer rather than an error: a player who has not been assigned a
   character yet, or who has just left, genuinely has no proxy. A plugin must
   treat that as "no mapping right now" and may register a mapping listener
   instead of polling.

## What this framework deliberately does not do

- It does not interpret plugin payloads. Channel names are opaque and bounded;
  payloads are opaque and bounded.
- It does not guarantee delivery. A dropped or rate-limited payload degrades a
  plugin; it never degrades the session.
- It does not expose a server-side plugin host yet. A request targeting the
  server is accepted and dropped rather than misrouted, so a sender is not told
  something was relayed when nothing was.
- It does not push to the plugin repositories. They are pinned submodules; a
  packaged file this project owns lives in GameFiles and overrides the upstream
  copy at package time (see the payloadNote in Code/plugins/plugins.json).
