# STR internal connection plan

This project now has two layers:

1. `STR_QueryPluginMessagingInterface`, used by my SKSE/mod-side projects.
2. `STRPM_QueryTransportInterface`, implemented by whichever transport can carry
   the bytes.

The public mod API stays stable. The private packaged runtime is STR-only:

- `STR`: require an internal STR bridge.
- If the bridge is missing, `send()` returns `kNotConnected`.
- No UDP socket is opened by the default build.

The old standalone UDP broker remains available only for development builds
compiled with `STRPM_ENABLE_UDP_BACKEND=ON`.

## What the official scripting link changes

The STR developers pointed at the official Tilted Online scripting guide:

- <https://wiki.tiltedphoques.com/tilted-online/guides/scripting.md>
- <https://wiki.tiltedphoques.com/tilted-online/guides/scripting/gameserver.md>
- <https://wiki.tiltedphoques.com/tilted-online/guides/scripting/event-handlers.md>

That confirms STR server resources can be loaded from the server `resources`
folder through a manifest and Lua entrypoint. The documented API exposes server
events such as player join/quit, update ticks, and chat messages. It also allows
the server script to send chat messages to one player or all players.

It does not currently document a generic binary plugin-message API. In other
words, scripting is useful for a private server-side relay, but it does not by
itself let an SKSE plugin send arbitrary bytes through STR from the client.

## Practical bridge options

### Option A: native STR bridge

This is the clean long-term shape.

A private or official STR-side module exports:

```cpp
STRPM_QueryTransportInterface(version, outInterface)
```

The runtime loads it from:

```text
Data/SkyrimTogetherReborn/STRPluginMessagingBridge.dll
```

The bridge is responsible for mapping `STRPM::send()` onto STR's internal
client/server messaging layer, then calling the receive callback when a plugin
payload arrives.

Current runtime behavior:

- `Mode=STR` is the packaged default.
- If the bridge is missing or fails to start, UDP is not used.
- `Mode=Auto` and `Mode=UDP` only matter in custom builds compiled with
  `STRPM_ENABLE_UDP_BACKEND=ON`.

### Option B: server scripting chat tunnel

This is an experimental private path suggested by the scripting docs.

The server resource can:

1. Listen to `onChatMessage`.
2. Detect a private `STRPM|...` envelope.
3. Call `cancelEvent(...)` so the original chat message is not rebroadcast.
4. Relay the envelope using `GameServer:SendChatMessage(...)` or
   `GameServer:SendGlobalChatMessage(...)`.

This still needs a client-side bridge/filter. Otherwise relayed envelopes are
ordinary STR chat messages and may be visible in the STR overlay.

The prototype resource lives in:

```text
extras/str-server-resources/strpm-chat-relay/
```

## Why not hook SkyrimTogether.exe directly?

The installed STR binaries expose very few useful exports. The client transport
types are present in the binary, but not as a stable public ABI. A hook based on
addresses or signatures would be version-fragile and harder to justify to the
STR developers.

The bridge ABI keeps my plugin clean: STR can either accept an official version
later, or I can keep a private bridge for my own closed group while waiting for
approval.
