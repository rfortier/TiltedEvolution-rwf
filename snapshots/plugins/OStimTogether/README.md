# OStim Together

Current development version: **0.37.7**.

OStim Together synchronizes OStim Standalone scenes between Skyrim Together Reborn players. The `strpm` branch uses **STRPluginMessagingAPI only**; there is no UDP fallback.

The root `VERSION` file is the single source of truth for CMake, DLL startup logs and archive names. Small fixes increment the patch number; larger feature/architecture work increments the minor number and resets the patch number.

## Current status

v0.37.7 is the OStim Standalone 7.5c release candidate with corrected Threads interface version handling.

The first 7.5c multiplayer test showed that OStim Together v0.37.5 rejected the new `OStim.dll` runtime version `7.5.0.3` before the Threads interface exchange. This left STR networking alive but prevented creation of the remote OStim mirror thread, so the remote proxy continued following ordinary Skyrim Together movement and did not enter the synchronized undress/animation path.

v0.37.6 added support for `7.5.0.3` after verifying that the current OStim 7.5c `GraphActor` prefix retains the same `animationIndex`, `singleSpeed`, expression strings and `offset` ordering used by OStim 7.5b. The multiplayer regression test then confirmed remote mirror creation, node changes, speed changes, STOP handling and undress/animation synchronization on 7.5c.

That test also exposed a version-reporting semantic change: current OStim 7.5c `ThreadInterface::getVersion()` returns the packed OStim plugin version (`0x07050003`, decimal `117768195`) rather than the historical raw Threads ABI ordinal. v0.37.7 therefore normalizes packed interface-version tokens to the ABI guaranteed by each explicitly supported OStim runtime before capability checks are applied. OStim 7.5b/7.5c normalize to Threads ABI v3; OStim 7.4c retains the conservative ABI v2 fallback path.

### Core synchronization

OStim Together currently provides:

- player-to-player OStim scene start synchronization;
- player + NPC scene mirroring without forcing multiplayer-only alignment paths;
- exact OStim 7.5 furniture synchronization through Threads ABI v3;
- OStim 7.4c furniture fallback;
- wall-scene startup handling;
- shared native OStim NODE / SPEED / STOP controls;
- participant-authored OStim alignment synchronization;
- clock-calibrated free-standing scene phase replay;
- bounded mirror-self post-animation position correction;
- remote-proxy logical-origin correction for shared free scenes;
- equipment/outfit protection during scenes;
- safe orphan proxy-only OStim thread cleanup;
- targeted multiplayer consent / Add Actor gating;
- optional PPA runtime target synchronization;
- optional OCum Ascended RaceMenu overlay synchronization.

## OCum Ascended support

### Supported

OCum RaceMenu `CumOverlays` are synchronized between the true local player and the corresponding remote Skyrim Together proxy.

OStim Together keeps the true local PlayerCharacter fully owned by OCum/RaceMenu and mirrors the resulting overlay state read-only. The supported path includes the RaceMenu texture/decal properties and the live visibility metadata used by the remote proxy overlay bridge.

### Known limitation — OCum 3D meshes

OCum's OStim equip-object meshes:

```text
ocumvagmesh
ocumanmesh
```

are **not supported on remote Skyrim Together player proxies**.

The final v0.37.4 diagnostic proved that this is not a packet-loss or actor-resolution problem. On the receiving client:

```text
ADDONOBJ received
-> remote STR proxy resolved
-> OActor.EquipObject(...) returned true
-> OActor.IsObjectEquipped(...) returned true at T+250 ms
-> OActor.IsObjectEquipped(...) returned true again at T+1250 ms
-> 3D mesh still not rendered on the remote proxy
```

The same mesh remains visible on the true local player. This means OStim accepts and retains the equip-object state, but the remote STR proxy does not materialize/render that 3D equip object.

v0.37.5 and later therefore intentionally treat `ocumvagmesh` and `ocumanmesh` as **local-only** and suppress their network synchronization. Repeated retries are not performed for these OCum meshes in the supported release path.

This limitation does **not** affect OCum RaceMenu texture overlays.

## Support matrix

| Feature | Status |
| --- | --- |
| OStim scene start / stop | Supported |
| OStim node changes | Supported |
| OStim speed changes | Supported |
| Shared scene control | Supported |
| Furniture scenes | Supported |
| Wall scenes | Supported |
| Free-standing alignment | Supported |
| PPA runtime target sync | Supported with optional integration |
| OCum RaceMenu `CumOverlays` | Supported with optional integration |
| OCum 3D vaginal mesh (`ocumvagmesh`) | **Not supported remotely** |
| OCum 3D anal mesh (`ocumanmesh`) | **Not supported remotely** |

## Compatibility

Validated / explicitly supported OStim runtime layouts:

- OStim 7.4c — `7.4.0.3`;
- OStim 7.5b — `7.5.0.2`;
- OStim 7.5c — `7.5.0.3`.

OStim 7.5b and 7.5c use the same graph-prefix layout required by OStim Together's read-only animation/alignment probes. The free-scene phase/alignment specialization targets Threads API v3. Furniture and wall handling retain their dedicated paths.

For interface-version reporting, OStim Together accepts both legacy raw ABI ordinals and current packed OStim version tokens. Packed 7.5b/7.5c tokens are normalized to Threads ABI v3 before capability checks; 7.4c uses the conservative ABI v2 fallback.

The mandatory Add Actor gate `OSKSE.pex` compatibility patch matches the current OStim 7.5c `OSKSE.psc` public function surface and must win its file conflict against OStim's original script.

For multiplayer testing and release use, both Skyrim Together clients should run the same OStim Together version and compatible OStim/OCum/PPA versions.

## Optional integrations

### OCum Ascended

The FOMOD option synchronizes supported OCum RaceMenu overlays. OCum 3D vaginal/anal equip-object meshes are deliberately excluded from the supported remote feature set.

### PPA — Procedural Penis Animations

The optional PPA integration synchronizes runtime penetration target information between scene participants. If the PPA runtime is missing or unsupported, OStim Together disables the integration safely.

## Build

v0.37.7 changes the native OStim Threads version-normalization path only. It does **not** change `OStimTogetherOCum.psc`, so an already-current compiled optional OCum integration does not need to be recompiled solely for 0.37.7.

Build the release FOMOD with:

```powershell
$env:VCPKG_ROOT="C:\dev\vcpkg"
.\build-fomod.ps1
```

Expected output:

```text
dist\OStimTogether-v0.37.7-FOMOD.zip
```

FOMOD layout:

- `00 Core` — required DLL/INI plus mandatory Add Actor consent scripts;
- `10 OCum Ascended` — optional supported OCum RaceMenu overlay integration;
- `20 Procedural Penis Animations` — optional PPA synchronization.

The FOMOD build checks that compiled Papyrus files are not older than their sources and will stop with an explicit message if recompilation is required.

## Development note

The v0.37.4 OCum 3D-object diagnostics remain useful as historical evidence in Git history, but v0.37.5+ no longer presents those meshes as a supported feature. Future work may revisit them only if Skyrim Together or OStim changes how equip-object geometry is materialized on remote player proxies.
