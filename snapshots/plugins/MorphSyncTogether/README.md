# MorphSyncTogether v0.4.2 - STRPM transport

MorphSyncTogether keeps remote Skyrim Together player appearance authoritative across clients.

## v0.4.2

v0.4.2 keeps the optional TNG genital-size synchronization and adds automatic installer detection for The New Gentleman.

The final archive is:

```text
dist/MorphSyncTogether-v0.4.2.zip
```

It does not include `FOMOD` or `Vortex` in the filename.

### Optional TNG support with automatic detection

The New Gentleman integration remains optional. The installer now checks for:

```text
TheNewGentleman.esp
```

When that plugin is active, **The New Gentleman (TNG) support** is marked `Recommended`, matching the behavior used for supported BodyHairSliders overlay packs. If TNG is not detected, the option remains `Optional`.

Selecting the option installs:

```text
Data/SKSE/Plugins/MorphSyncTogether/Providers/TNG.enabled
```

Without that marker, the TNG synchronization module remains disabled.

TNG does not represent genital size as a normal RaceMenu BodyMorph. Its current implementation scales the live skeleton node `NPC GenitalsBase [GenBase]` and compensates `NPC GenitalsScrotum [GenScrot]` with `1 / sqrt(scale)`. MorphSyncTogether captures the player's effective live TNG scale, sends it through STRPluginMessagingAPI, and applies the same scale to the corresponding remote STR proxy.

This preserves custom TNG size settings and race multipliers instead of transmitting only the XS/S/M/L/XL category.

### TNG synchronization behavior

- optional installer integration
- automatically marked `Recommended` when `TheNewGentleman.esp` is active
- enabled by `Data/SKSE/Plugins/MorphSyncTogether/Providers/TNG.enabled`
- captures the local player's effective `GenBase` scale on Skyrim's game thread
- sends `TNGSIZE` state through the existing `morphsync.together.v1` STRPM channel
- resolves the remote actor through STRPM ProxyResolver
- applies `GenBase = scale`
- applies `GenScrot = 1 / sqrt(scale)`, matching TNG's own scaling behavior
- checks once per second for remote drift
- resends the authoritative scale every 5 seconds
- skips node writes when the proxy already matches the authoritative scale
- retries automatically when the STR proxy or TNG skeleton nodes are not yet loaded

The v0.3.4 morph resend optimization remains unchanged: repeated STRPM morph snapshots are compared against the live proxy and expensive RaceMenu rebuilds are skipped when the proxy already matches.

## Features

- RaceMenu BodyMorph synchronization
- OBody drift correction/reapplication
- FaceGen makeup preservation/rebind
- BodyHairSliders / OPubes semantic overlay synchronization
- optional The New Gentleman genital-size synchronization
- automatic installer detection for supported overlay providers and TNG
- periodic authoritative resend and remote reapply safety net

## STRPM transport

- channel: `morphsync.together.v1`
- target: all connected STR players
- flags: reliable + ordered
- sender identity comes from authenticated STRPM message metadata
- callbacks are queued onto the SKSE game thread before MorphSync processes them
- STRPM ProxyResolver is used for remote-player proxy resolution
- MorphSyncTogether does not open or discover LAN UDP peers

## Requirements

- Skyrim Together Reborn 1.8.0-compatible setup
- SKSE64
- RaceMenu
- Address Library for SKSE Plugins
- **STRPluginMessagingAPI v0.8.2 or newer compatible API/ProxyResolver**
- the STRPM server relay required by your STRPluginMessagingAPI installation
- the same MorphSyncTogether version on all clients

Optional integrations:

- OBody / OBody NG
- OStim
- BodyHairSliders
- OPubes
- **The New Gentleman (TNG)**

## BodyHairSliders providers

The installer mirrors the provider list used by BodyHairSliders:

- **Nordic Warmaiden Body Hair** — `Nordic Warmaiden Body Hair.esp`
- **HIMBO V3 Bodyhair Overlays for Racemenu** — `HIMBOBodyhairOverlay.esp`
- **Pubes Forever Female / Pubic Hairstyles All In One CBBE** — `AK_RM_PubicStyles_All_In_One.esp`
- **Pubes Forever Male** — `AK_RM_PubicStyles_All_In_One_M.esp`
- **OPubes NG compatibility** — `OPubes.esp`
- **More Pubes for SlaveTats** — detected through `textures/actors/character/slavetats/ZckeZckTPubicHair/ZckeZcktPubic00Heart.dds`
- **Natural Pubic Hairstyles** — `NaturalPubicHairstyles.esp`
- **Natural Pubic Hairstyles - UBE** — `UBENaturalPubicHairstyles.esp`

Detected providers are marked `Recommended` in the installer.

Selected providers install markers under:

```text
Data/SKSE/Plugins/MorphSyncTogether/Providers/
```

Tattoos, unrelated generic Body Paints and temporary OCum overlays remain excluded.

The installer intentionally has **no module artwork**.

## Expected log

Successful startup with TNG support selected should include:

```text
MorphSyncTogether v0.4.2 STRPM loading
MST STRPM transport READY channel=morphsync.together.v1 ...
Morph sync started ...
MST TNG sync started interval=1000ms resend=5000ms
```

Local TNG state:

```text
MST TNG TX player=... name="..." scale=... changed=... resend=...
```

Remote TNG state:

```text
MST TNG RX sender="..." scale=... repeated=...
MST TNG DRIFT ... action=restore
MST TNG APPLY ... verified=1
```

When the remote TNG scale already matches:

```text
MST TNG APPLY SKIP ... reason=already-authoritative
```

Existing morph diagnostics remain available:

```text
MST MORPH DRIFT ... action=restore
MST MORPH APPLY ... verified=1
MST MORPH APPLY SKIP ... reason=already-authoritative
```

## Build

Run:

```powershell
.\build-vortex.ps1
```

Output:

```text
dist/MorphSyncTogether-v0.4.2.zip
```

The build script validates the core package, all BodyHair provider packages, the optional TNG integration marker, both installer XML files and the final archive contents. It also explicitly rejects an accidentally reintroduced `fomod/ModuleImage.png`.
