# IEDSyncTogether

Compatibility layer between Immersive Equipment Displays (IED) and Skyrim Together Reborn.

## v0.9.0 — persistent raw-transform watchdog + Custom Item rendering

The `strpm` branch now reproduces both standard IED slot displays and captured IED Custom Items on the correct remote STR proxy.

```text
local evaluated IED state
        ↓
slots + custom objects
        ↓
plugin/local FormID + attachment mode + exact local transform
        ↓
LocalIEDState / STRPM
        ↓
STRPM ProxyResolver
        ↓
remote STR proxy
        ↓
IED creates/attaches IEDSyncTogether-owned Custom Items
        ↓
IEDSyncTogether watchdog maintains exact NiAVObject::local transforms
```

### Requirements

- Skyrim Together Reborn 1.8.0
- Immersive Equipment Displays
- STRPluginMessagingAPI v0.8.2 or newer compatible build with ProxyResolver available
- the relay/server files required by that STRPM build

For renderer testing, disable IED **NPC Displays** on both clients so remote displays visible in game are created by IEDSyncTogether rather than IED's normal NPC display path.

## Local capture

The authoritative local capture records:

- the 19 official IED equipment slots through `IED.GetSlottedForm`;
- loaded IED `OBJECT ... [FormID]` scene objects;
- stable `plugin + local FormID` identity;
- slot/custom classification;
- visibility/culling state;
- object, attachment and evaluated anchor node names;
- exact local XYZ position;
- raw 3x3 rotation matrix;
- scale.

The raw matrix remains the wire representation. No Euler angles are serialized or used for the final remote orientation.

## STRPM transport and ProxyResolver

State is sent on:

```text
strpm.iedsynctogether.state.v1
```

using reliable + ordered STRPM messages.

The resilient delivery behavior remains active:

- changed state is retained as pending while transport is unavailable;
- pending state retries every 1 second;
- the current full state is retransmitted every 10 seconds as a heartbeat;
- late-joining peers receive the current state without forcing an equipment change.

IEDSyncTogether uses STRPM's public ProxyResolver API only. It does not scan `ProcessLists`, compare actor names, or guess dynamic proxy FormIDs. Proxy mapping callbacks are forwarded to the SKSE game-task queue before actor lookup or IED rendering.

## Standard slot renderer

Standard favorites continue to use the v0.8 raw-scenegraph approach:

1. resolve `plugin + local FormID` on the receiving client;
2. select the evaluated remote node/anchor;
3. create an IED Custom Item owned by `IEDSyncTogether.esp`;
4. preserve left-weapon semantics where required;
5. call `IED.Evaluate`;
6. find the actual generated `OBJECT ... [FormID]` scene object;
7. restore its exact captured local position, raw 3x3 matrix and scale directly.

This is the path that fixed the previously incorrect spear/bow/shield orientations.

## Persistent transform watchdog

v0.8.0 proved the raw transform itself is correct, but logs showed that IED can later re-evaluate an entry and restore its own temporary/default transform. The old immediate retries all completed before some of those late IED evaluations, so an item could remain incorrectly oriented until the next 10-second STRPM heartbeat.

v0.9.0 replaces those immediate retries with a persistent local watchdog:

```text
100 ms tick
   ↓
for every tracked remote IEDSyncTogether display
   ↓
measure rotation / position / scale delta
   ↓
if already correct: do nothing
if changed by IED: restore exact raw transform immediately
```

The watchdog generates no network traffic. It only reads and, when needed, repairs IEDSyncTogether-owned remote scene objects on the Skyrim game thread.

Expected correction logs:

```text
REMOTE IED WATCHDOG acquired: ...
REMOTE IED WATCHDOG corrected: ...
```

If IED resets the same object again later, trace logs show:

```text
REMOTE IED WATCHDOG recorrected: ...
```

The target interval is 100 ms, so a late IED reset should no longer leave a visibly wrong orientation for several seconds.

## Custom Item renderer

v0.9.0 enables remote rendering for captured `IEDObjectKind::kCustom` objects.

The renderer preserves the captured IED attachment mode directly from the scene graph:

```text
OBJECT P <node>  → parent attachment mode
OBJECT R <node>  → reference attachment mode
```

It also resolves the transmitted form and applies the same raw scenegraph transform through the watchdog.

This specifically covers Helmet Toggle 2 / DAV-style states already captured in previous tests.

### Helmet in hand

Captured locally as:

```text
OBJECT P AnimObjectR
```

Remote rendering uses:

```text
node = AnimObjectR
attachmentMode = parent
```

### Helmet at the waist

Captured locally as:

```text
OBJECT R ExtraPelvisArmorHelmet1
```

Remote rendering uses:

```text
node = ExtraPelvisArmorHelmet1
attachmentMode = reference
```

The exact captured position, raw rotation matrix and scale are then maintained on the generated armor object.

### Helmet absent

When the sender's next full state no longer contains the Custom Item, IEDSyncTogether rebuilds its owned remote entries without that object. The remote helmet therefore disappears without any Helmet Toggle-specific protocol.

The implementation is generic: other visible IED Custom Items using resolvable forms and `OBJECT P/R` attachment nodes follow the same path.

## Diagnostic logs

Startup should include:

```text
IEDSyncTogether v0.9.0 loading
REMOTE IED transform watchdog started: interval=100ms
STRPM branch mode: ... raw-scenegraph slot/custom renderer + transform watchdog ...
```

For a standard slot:

```text
REMOTE IED SLOT queued: ...
rawTransform=watchdog
```

For the helmet or another Custom Item:

```text
REMOTE IED CUSTOM queued: ...
attachmentMode=parent|reference
rawTransform=watchdog
expectedParent="OBJECT P/R ..."
```

When the generated object becomes available:

```text
REMOTE IED WATCHDOG acquired: ... kind=custom ...
REMOTE IED WATCHDOG corrected: ... afterDelta(rot=0...,pos=0...,scale=0...)
```

The renderer summary now reports:

```text
REMOTE IED RENDER queued: ...
visibleSlots=...
customRendered=...
trackedTransforms=...
watchdogIntervalMs=100
```

## Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-vortex.ps1
```

Expected archive:

```text
dist/IEDSyncTogether-v0.9.0.zip
```

## v0.9.0 test protocol

Install the same v0.9.0 archive on Player1 and Player2 together with the current STRPM/ProxyResolver build.

1. Keep IED NPC Displays disabled on both clients.
2. Connect both players to the same STR server.
3. Leave Kahel's normal favorites visible and verify their orientation remains correct after equipment/IED state changes.
4. Trigger several changes quickly and watch for any multi-second wrong-orientation phase; the watchdog should correct late IED resets within roughly one tick.
5. Put Kahel's helmet in hand and verify Player2 sees it on `AnimObjectR`.
6. Move the helmet to the waist and verify Player2 sees it on `ExtraPelvisArmorHelmet1`.
7. Hide/remove the Helmet Toggle Custom Item and verify it disappears remotely.
8. Repeat in the opposite direction if Elir has a suitable Custom Item state.
9. Send both `IEDSyncTogether.log` files if any display is missing, flickers persistently, or the watchdog repeatedly recorrects the same item.

## Safety

IED remains responsible for creation, attachment, enable/disable and cleanup through its public Papyrus Custom Item API. IEDSyncTogether only updates the local transform of the specific scene objects it created for remote synchronization. No private IED detours are reintroduced.

## License

MIT
