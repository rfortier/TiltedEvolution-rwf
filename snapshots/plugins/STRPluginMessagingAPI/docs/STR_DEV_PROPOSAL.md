# STR Plugin Messaging API Proposal

## Summary

Expose a small plugin messaging API that lets SKSE mods send opaque,
namespaced payloads through the existing Skyrim Together Reborn connection.

STR would only relay the data. It would not need to parse OStim, RaceMenu, IED,
trade, or any other mod-specific protocol.

## Main Use Case

Compatibility mods often need to synchronize small pieces of state between the
same players already connected through STR.

Today, my mods solve this by opening their own UDP sockets:

- OStimTogether uses UDP for scene state.
- MorphSyncTogether uses UDP for morph and overlay snapshots.
- IEDSyncTogether uses UDP for equipment display slots.
- TradeTogether uses UDP for targeted trade requests.

This duplicates discovery, authentication, peer routing, relay behavior, and
firewall setup in every mod.

## Proposed STR Responsibilities

- Maintain a registry of plugin channels.
- Accept opaque byte payloads from local SKSE plugins.
- Attach sender identity to incoming plugin messages.
- Route payloads to the requested target using the existing STR connection.
- Enforce basic limits such as payload size and rate limits.
- Drop messages for channels with no receiver on the target client.

## Proposed Mod Responsibilities

- Use globally namespaced channel names, for example
  `chaos.morph_sync_together.snapshot.v1`.
- Serialize and version their own payloads.
- Keep gameplay changes on the game thread if STR invokes callbacks from a
  networking thread.
- Handle missing API support by falling back to their current transport or
  disabling networked features gracefully.

## Transport Contract

The API should support:

- Broadcast to all connected players.
- Send to a specific STR connection ID.
- Optional send to host/server.
- Sender connection ID and display name on receive.
- Reliable and ordered flags for event streams.
- Unreliable/default delivery for periodic snapshots.

STR should not guarantee that all messages are delivered unless the reliable
flag is accepted for that connection mode. The caller should receive an explicit
result when a requested delivery mode is unsupported.

## Security and Safety

- Payloads should only travel inside the current STR session.
- STR should enforce a maximum payload size.
- STR should rate-limit each channel or each sender.
- Channel names should be ASCII and length-limited.
- STR should not deserialize mod payloads.

## Export Shape

The draft header expects STR to expose:

```cpp
STRPM_EXPORT STRPM::Result STRPM_CALL STR_QueryPluginMessagingInterface(
    std::uint32_t requestedVersion,
    const STRPM::Interface** outInterface);
```

The returned `STRPM::Interface` contains function pointers for registration,
unregistration, sending, local connection identity, and optional logging.

See `include/STRPluginMessagingAPI/STRPluginMessagingAPI.h`.

The standalone implementation in this repository currently provides the same
mod-facing API over one shared UDP broker. That makes it possible to migrate
mods now, while keeping the future STR-backed transport as an internal broker
implementation detail.

## Why This Helps

This would let external compatibility mods reuse STR's already-established
connection instead of asking users to open extra ports for every addon. It
would also make STR-based mod synchronization easier to debug because all peer
routing would happen in one place.
