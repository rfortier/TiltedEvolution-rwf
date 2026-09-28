# Optional OCum Ascended Integration

This component enables the supported OCum Ascended appearance synchronization for OStim Together.

## Supported in v0.37.5

OStim Together mirrors the true local PlayerCharacter's RaceMenu `CumOverlays` to the corresponding remote Skyrim Together player proxy. The local player remains fully owned by OCum/RaceMenu; OStim Together reads and transports the resulting overlay state without taking over local rendering.

Supported appearance data includes OCum RaceMenu texture/decal overlays on the body and other RaceMenu overlay areas handled by the native overlay bridge.

## Known limitation: OCum 3D meshes

OCum's OStim equip-object meshes:

- `ocumvagmesh`
- `ocumanmesh`

are **not synchronized to remote Skyrim Together player proxies**.

The v0.37.4 diagnostic established that the complete network and OStim state path succeeds on the receiving client: the remote proxy resolves correctly, `OActor.EquipObject()` returns `true`, and `OActor.IsObjectEquipped()` remains `true` at both 250 ms and 1.25 s. Despite that, the 3D mesh is not rendered on the STR proxy.

Because further retries cannot fix a renderer/proxy materialization failure, v0.37.5 intentionally treats these meshes as local-only and suppresses their synchronization. This limitation does **not** disable OCum RaceMenu texture overlays.

## Build

v0.37.5 changes `OStimTogetherOCum.psc`, so recompile it before building the FOMOD:

```powershell
.\optional\OCumIntegration\compile-ocum-integration.ps1 `
  -SkyrimDir "C:\Games\Steam\steamapps\common\Skyrim Special Edition"
```

Then build normally:

```powershell
$env:VCPKG_ROOT="C:\dev\vcpkg"
.\build-fomod.ps1
```

The FOMOD build intentionally refuses to package a PEX older than the current source.
