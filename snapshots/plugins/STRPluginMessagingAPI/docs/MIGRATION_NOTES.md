# Migration Notes For My Mods

These notes describe how my current STR-based mods can move from their own UDP
sockets to `STRPluginMessagingAPI`.

## Shared Plan

Each mod can keep its existing payload format at first and only replace the
transport layer. The current broker is already functional over one shared UDP
port. If STR later exposes an official relay API, the broker can become the
adapter to STR's connection without changing every mod again.

Current transport responsibilities to remove later:

- LAN discovery.
- Manual peer IP and port config.
- Shared secret signing.
- Peer cache and expiration.
- Relay handling.
- Per-mod UDP port.

Replacement responsibilities:

- Register one or more STR plugin channels.
- Send payloads to `kAllPlayers` or a specific `ConnectionID`.
- For name-based targeting, set `Target::displayName` with
  `TargetKind::kPlayer`.
- Use callback payloads as the existing packet entry point.
- Call `setLocalDisplayName()` after the mod can read the Skyrim player name.
- Resolve the sender's remote actor using `Sender::displayName` first.

## OStimTogether

Suggested channel: `chaos.ostim_together.scene.v1`.

Use reliable and ordered messages for scene start, node, speed, and stop events.
Furniture alignment and actor pose data can stay in the same payload format
initially.

## MorphSyncTogether

Suggested channels:

- `chaos.morph_sync_together.body_morph.v1`
- `chaos.morph_sync_together.overlay.v1`

Snapshots can remain chunked if they approach the payload limit. Periodic
snapshots may use default delivery, while multi-chunk snapshots should request
reliable ordered delivery if STR supports it.

## IEDSyncTogether

Suggested channel: `chaos.ied_sync_together.slots.v1`.

The current `plugin + local FormID` payload can stay unchanged. The receiver
still applies the visual slot data only to STR remote-player proxies.

## TradeTogether

Suggested channel: `chaos.trade_together.offer.v1`.

Trade requests should target a specific `ConnectionID` and use reliable ordered
delivery. The consent dialog and inventory opening logic can remain entirely
inside the mod.

## Fallback

During migration, each mod can choose the broker first and fall back to its
current per-mod UDP transport only when the API is missing or too old.
