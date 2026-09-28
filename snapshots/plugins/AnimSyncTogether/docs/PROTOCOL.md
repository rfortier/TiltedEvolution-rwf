# AnimSync Together protocol notes

## Responsibility boundary

AnimSync Together does not discover Skyrim Together Reborn player proxies.

STRPluginMessagingAPI (STRPM) owns STR player identity and resolves each remote `ConnectionID` to the local Skyrim proxy FormID. Proxy FormIDs are client-local and must never be transmitted as global player identifiers.

## v0.3.0 integration

AnimSync registers a listener with STRPM ProxyResolver API v1.

Mapping callbacks can originate outside the game thread, so AnimSync copies the event and queues all Skyrim object lookup and animation-sink changes through `SKSE::TaskInterface`.

Mapping lifecycle behavior:

- `kAdded`: store `ConnectionID -> FormID` and attach the animation sink;
- `kUpdated`: detach the old FormID, replace the mapping, attach the new FormID;
- `kRemoved`: detach the old proxy and remove the mapping;
- `kCleared`: detach every known remote proxy and clear local mapping state.

The local player sink remains independent of STRPM.

## Future animation message shape

Captured local-player animation events will later be reduced to a transport message containing:

- protocol version;
- animation graph tag;
- optional payload when required;
- monotonic sequence number / deduplication token.

The sender identity does not need to be embedded in the payload: STRPM already authenticates the sender and supplies `message->sender.connectionID`. The receiver resolves that ConnectionID through ProxyResolver before replay.

## Planned progression

1. **Done:** capture local animation graph events.
2. **Done:** delegate all proxy identity/discovery to STRPM.
3. **Done in v0.3.0:** consume STRPM mapping lifecycle and observe remote proxy graphs.
4. Compare events reproduced natively by STR against local-player events.
5. Build an allow-list of missing deterministic events.
6. Add filtering, deduplication and loop prevention.
7. Send missing animation events over a dedicated STRPM channel.
8. Replay the allow-list on the FormID resolved from the authenticated sender ConnectionID.
9. Extend to furniture transitions and higher-level integrations.

## Explicitly out of scope for AnimSync Together

- scanning `RE::ProcessLists` to discover STR proxies;
- identifying proxies from dynamic FormID ranges;
- matching remote players by actor name;
- maintaining STR membership or player identity;
- implementing a separate LAN discovery or transport stack.
