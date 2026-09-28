# Official STR chat-tunnel transport

This transport keeps the stock Skyrim Together client protocol and stock server binary.
The server-side relay uses only the documented STR scripting API.

## Confirmed stock STR path

Outgoing chat in the official client is converted to `SendChatMessageRequest` by
`OverlayClient::ProcessChatMessage()` and passed to `TransportService::Send()`.
Incoming server chat is decoded as `NotifyChatMessageBroadcast` and dispatched to
`OverlayService::OnChatMessageReceived()` before being forwarded to the CEF overlay.

The stock server scripting API exposes `onChatMessage`, `cancelEvent`,
`GameServer:SendChatMessage(connectionId, message)` and
`GameServer:SendGlobalChatMessage(message)`. This is enough to implement routing without
changing STR opcodes or rebuilding the server.

## Remaining client-side boundary

The official STR client does not expose a documented C/SKSE ABI for either
`TransportService::Send()` or the `NotifyChatMessageBroadcast` dispatcher. Therefore a
client bridge cannot call or subscribe to those facilities through a supported public
interface today.

The practical stock-client implementation is a thin, version-specific bridge that:

1. Locates the official client's outgoing chat path.
2. Injects an STRPM envelope as an ordinary `SendChatMessageRequest`.
3. Observes `NotifyChatMessageBroadcast` before the message reaches the overlay.
4. Consumes `STRPM|v2|...` envelopes and converts them to STRPM receive callbacks.
5. Leaves all non-STRPM chat untouched.

All version-specific discovery/hooking must stay in `STRPluginMessagingBridge.dll`; the
public STRPM API and the server resource remain version-independent.

## STR 1.8.0 resolver

The first supported target is the public `v1.8.0` source tag at commit
`9c23efa422bbc1e5c06eef5522ca73971a513e35`.

The bridge intentionally does not contain absolute executable addresses. At runtime it:

1. Locates the loaded `SkyrimTogether.exe` PE image.
2. Restricts string scanning to its `.rdata` section and code scanning to `.text`.
3. Finds the unique 1.8.0 logging literal used inside `OverlayClient::ProcessChatMessage()`:
   `Send chat message of type {}: '{}' `.
4. Resolves the RIP-relative `LEA` instruction that references that literal.
5. Calls `RtlLookupFunctionEntry()` on the xref to obtain the function's exact x64 unwind
   bounds from the executable `.pdata` metadata instead of guessing a prologue.
6. Enumerates direct `CALL rel32` targets inside those bounds.

The bridge remains deliberately unarmed until the actual public binary confirms which
post-log call target is `TransportService::Send()` and until the incoming chat handler
has been independently resolved. A failed or ambiguous resolution returns
`kNotConnected`; it never guesses an address.

The diagnostic output is written to:

```text
Data\SKSE\Plugins\STRPluginMessagingBridge.log
```

A successful outgoing-path probe should include entries similar to:

```text
send-chat anchor copies: 1
send-chat RIP xrefs: 1
ProcessChatMessage candidate: 0x...-0x... (RVA 0x...-0x...)
direct CALL candidates in ProcessChatMessage: N
  call[0] site=... target=...
  ...
resolver status: ProcessChatMessage located; transport call still intentionally unarmed
```

RVA values are logged specifically so results can be compared across machines despite
ASLR.

## Envelope v2

Each transport fragment is an ASCII chat payload:

```text
STRPM|v2|msg=<id>|seq=<n>|channel=<channel>|target=<target>|flags=<n>|part=<i>|parts=<n>|payload=<hex>
```

The relay appends authenticated sender metadata derived from the STR server session:

```text
|sender=<connectionId>|senderName=<name>|serverTick=<tick>
```

Targets supported by the scripting relay are:

- `all`
- `id:<connectionId>`
- `server`

`host` cannot be resolved through the documented scripting API and must not silently
fall back to broadcast.

## Fragmentation

The resource accepts individual envelopes up to 4096 characters and at most 64
fragments per logical message. The client bridge is responsible for fragmentation and
reassembly. Fragment identifiers should be unique for the local session and sequence.

## Visibility

The server calls `cancelEvent()` for client-originated STRPM envelopes so they are not
rebroadcast as normal chat. Server-to-client relays still arrive as standard STR chat
packets, so the client bridge must intercept them before `OverlayService` forwards them
to the CEF UI. Without that client interception, transport envelopes will be visible.

## Compatibility conclusion

The server can stay completely stock. The network protocol and opcode tables can stay
completely stock. A small client-side compatibility bridge is still required because
STR currently exposes no public plugin messaging or chat injection/subscription ABI.
