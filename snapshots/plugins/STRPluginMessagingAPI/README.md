# STR Plugin Messaging API

Shared messaging and STR player-proxy resolution for Skyrim Together Reborn compatibility mods.

Current release version: **v0.9.3**.

STRPluginMessagingAPI gives SKSE mods one common layer over the **official Skyrim Together Reborn 1.8.0 connection**. It is intended for mods such as AnimSyncTogether, OStimTogether, MorphSyncTogether, IEDSyncTogether and TradeTogether so each project does not need to duplicate networking or guess which Skyrim Actor represents a remote STR player.

## Architecture

```text
consumer SKSE mod
    ↓
STRPluginMessagingAPI.dll
    ├─ messaging API
    └─ ProxyResolver API: ConnectionID -> local proxy FormID
    ↓
STRPluginMessagingBridge.dll
    ↓
official STR 1.8.0 runtime
    ↓
official STR server + strpm-chat-relay Lua resource
```

STRPM payloads use reserved `STRPM|v2|...` chat envelopes. The bridge intercepts those packets before STR's normal chat UI so transport packets do not appear as yellow chat messages. Ordinary STR chat remains untouched.

## v0.9.3 — authenticated identity heartbeat and validated ProxyResolver cold start

v0.9.3 finalizes the ProxyResolver cold-start fix and the chat-suppression regression discovered during two-client testing.

The server relay is now **v4 / 0.4.0**. For the reserved `strpm.identity.v1` heartbeat, the server replaces the empty payload with the authenticated STR `PlayerId` of the sender while retaining the authenticated sender `ConnectionID` metadata. The receiving bridge registers the reserved identity channel through the normal STRPM API and joins:

```text
ConnectionID
    ↓ authenticated relay metadata
PlayerId
    ↓ STR remote-player lifecycle
local proxy FormID
```

This removes the previous dependency on VEH handler ordering when discovering `ConnectionID -> PlayerId`.

The validated two-client regression test confirmed:

- Player1 resolves Player2 to the correct local dynamic proxy FormID;
- Player2 resolves Player1 to the correct local dynamic proxy FormID;
- both diagnostic clients complete the bidirectional handshake;
- both diagnostic clients finish with `PROXY RESOLVE OK`;
- identity bootstrap works before consumer traffic;
- the server no longer prints the transient `Dropped envelope from unknown character` message during the validated test;
- STRPM technical envelopes no longer appear as yellow chat messages;
- ordinary STR chat remains unaffected.

The dedicated OverlayApp chat UI suppression bootstrap, accidentally omitted from the v0.9.1 startup path, is active again. The identity heartbeat remains periodic, but duplicate heartbeat observations are no longer spammed into the bridge log; only first discovery or an identity change is logged.

## v0.9.0 — fixed-buffer receive capture outside the VEH

v0.9.0 changes the STR receive architecture after TradeTogether exposed a receiver freeze that remained possible with v0.8.3.

v0.8.3 deferred the final consumer callback, but the `TransportService::OnConsume` vectored exception handler still performed the expensive part of reception synchronously: raw packet copying into dynamic vectors, chat-string parsing, fragment reconstruction, `unordered_map`/mutex work, hex decoding and delivery into the bridge callback thunk. That work can run while STR's receive path is in an exception/breakpoint context and may participate in lock/reentrancy cycles with gameplay/UI work on another thread.

v0.9.0 moves the entire dynamic receive pipeline out of that context.

The VEH now performs only bounded operations:

```text
TransportService::OnConsume breakpoint / VEH
        ↓
check opcode 36
        ↓
copy raw packet with ReadProcessMemory
into a preallocated fixed slot
        ↓
minimal local prefix check for STRPM|v2|
        ↓
suppress the STRPM chat packet
        ↓
return immediately
```

A dedicated receiver thread then performs the normal dynamic processing:

```text
fixed raw-packet queue
        ↓
raw STR chat decode
        ↓
fragment reconstruction
        ↓
hex payload decode
        ↓
v0.8.3 completed-message FIFO
        ↓
registered consumer callback
```

Important implementation properties:

- 64 preallocated raw capture slots;
- 16 KiB per slot, comfortably above the bridge's 3500-character chat-envelope maximum;
- no `std::string`, `std::vector`, `unordered_map`, mutex, file logging or consumer callback from the receive VEH;
- multiple STR receive threads can reserve slots through atomic state transitions;
- captured packets carry an atomic order number so the dispatcher processes them in capture order;
- if the fixed queue is saturated, the packet is not suppressed and STR is allowed to process it normally rather than silently losing it;
- the public API and `STRPM|v2|` wire prefix remain unchanged.

Consumer callbacks are still **not Skyrim's game thread**. Consumer mods must continue to schedule Skyrim object lookup, UI work and game-state mutation through their normal SKSE/game-thread path.

## v0.8.3 — completed-message callback dispatch

v0.8.3 introduced the first receive-thread safety layer. Completed STRPM messages were copied into a bounded FIFO and consumer callbacks moved onto a bridge dispatcher thread instead of being invoked directly by the `OnConsume` interception callback.

That was necessary but incomplete: raw chat decoding and fragment reconstruction still occurred in the VEH. v0.9.0 retains the v0.8.3 completed-message queue and adds the earlier fixed-buffer raw-packet stage.

## v0.8.2 — session/log cleanup

v0.8.2 is a small cleanup patch on top of the validated v0.8.1 ProxyResolver.

Two runtime behaviors changed:

1. the optional diagnostic client treats each STR connection as a separate E2E session. A disconnect clears peer/ACK state and the next `OnConnected` restarts at `probe=1`;
2. repeated internal ProxyResolver flushes no longer log `mapping ready` when `ConnectionID -> FormID` is unchanged. Public mapping events were already change-only.

## ProxyResolver

Consumer mods must **not** scan Skyrim `ProcessLists`, compare actor names or cache guessed dynamic FormIDs. STRPM owns STR-version-specific resolution and publishes:

```text
STR ConnectionID
      ↕ authenticated relay metadata
STR PlayerId
      ↕ official STR player lifecycle
local Skyrim proxy FormID
```

Relay v4 appends authenticated sender metadata and provides the authenticated sender PlayerId through the reserved identity heartbeat. The bridge joins that identity with the local proxy FormID observed from STR's remote-player lifecycle.

### Public API

Load the resolver independently of the messaging interface:

```cpp
const auto* resolver = STRPM::LoadProxyResolverFromModule();
```

Resolve the sender of an STRPM message:

```cpp
STRPM::ProxyFormID formID{};
const auto result = resolver->resolve(message->sender.connectionID, &formID);
if (result == STRPM::Result::kOk) {
    // formID is the local Skyrim Actor proxy for that remote STR player.
}
```

Consumers can also subscribe to mapping lifecycle changes:

```cpp
resolver->registerListener(&OnProxyMappingChanged, userData);
```

Events are:

- `kAdded`
- `kUpdated`
- `kRemoved`
- `kCleared`

A listener registered after mappings already exist receives an immediate snapshot through `kAdded` events.

**Important:** ProxyResolver returns a FormID, not a raw `Actor*`. Resolver callbacks can originate from an STR/bridge thread. Consumer mods should perform Skyrim object lookup/game mutations on the game thread.

## Compatibility target

```text
Skyrim Together Reborn: 1.8.0
TiltedEvolution tag:    v1.8.0
TiltedEvolution commit: 9c23efa422bbc1e5c06eef5522ca73971a513e35
TiltedConnect commit:   c20165c35c4d024bb456430eeb0abb554e34c7f4
```

Official Nexus executable used for runtime validation:

```text
Size:    7,058,432 bytes
SHA-256: 77f23c9c82c412252b5c4491a09d7ab4349cbc6c77992c4766882f54798cb99d
```

## Server resource

v0.9.3 uses relay **v4 / 0.4.0**. The server console must report:

```text
[STRPM] Chat relay v4 loaded (authenticated identity payload enabled)
```

The wire prefix remains `STRPM|v2|`.

## FOMOD

The package contains three installation modes:

```text
Client + Server   Recommended for the player hosting the STR server
Client Only       For players joining another server
Server Files Only For a dedicated/server-only update
```

The server option deploys the resource under:

```text
Data/SkyrimTogetherReborn/resources/strpm-chat-relay/
```

including both `main.lua` and `strpm-chat-relay.manifest`.

## Build

### Normal release package

```powershell
.\build-vortex.ps1
```

Output:

```text
dist/STRPluginMessagingAPI-v0.9.3.zip
```

The public release archive intentionally excludes `STRPluginMessagingDiagnostic.dll`.

### Diagnostic test package

```powershell
.\build-test-vortex.ps1
```

or:

```bat
build-test-vortex.bat
```

Output:

```text
dist/STRPluginMessagingAPI-v0.9.3-test.zip
```

The test archive additionally contains:

```text
Data/SKSE/Plugins/STRPluginMessagingDiagnostic.dll
```

The normal package contains:

```text
Data/SKSE/Plugins/STRPluginMessagingAPI.dll
Data/SKSE/Plugins/STRPluginMessagingAPI.ini
Data/SKSE/Plugins/STRPluginMessagingBridge.dll
```

## Remaining work

- discover/report the local STR connection ID directly;
- finalize `Host` target semantics;
- migrate remaining consumers fully to STRPM messaging + ProxyResolver;
- remove obsolete experimental UI-suppression source files after the validated 0.9.3 release cycle.

## Design rule

All STR-version-specific discovery and hooking belongs inside `STRPluginMessagingBridge.dll`. The public API and consumer mods remain independent of STR internal addresses so a future STR update only requires a bridge compatibility update.
