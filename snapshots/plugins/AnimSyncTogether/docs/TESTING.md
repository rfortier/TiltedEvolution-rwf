# v0.9.0 test procedure

This milestone diagnoses why Helmet Toggle 2 remains visually absent on the remote STR proxy even though `OffsetGPMA` is successfully replayed and the proxy emits the expected GPMA graph outputs.

## Setup

1. Install STRPluginMessagingAPI v0.8.2 or newer on both clients.
2. Build with `build_release.bat`.
3. Install `dist/AnimSyncTogether-v0.9.0.zip` with Vortex on both clients.
4. Connect both players to the same STR server and place them in the same cell.

## Controlled test

Keep Player2 idle. On Player1 / Kahel:

1. wait a few seconds after the STR proxy appears;
2. trigger the helmet-removal animation once;
3. wait five seconds;
4. stop testing and collect both logs.

Do not repeat other animations during the 1.5 second clip-capture window.

## Expected Player1 log

The local player should arm clip capture before the input reaches OAR:

```text
ClipProbeArm actor=00000014 reason='local OffsetGPMA' windowMs=1500
GPMAState actor=00000014 localPlayer=true variable='iGPMAOffsetType' present=true value=<n>
```

One or more clip activations should then be logged:

```text
ClipSelection actor=00000014 ... beforeIndex=<n> selectedIndex=<n> replaced=<true|false> file='<HKX path>' duration=<seconds> reason='local OffsetGPMA'
```

## Expected Player2 log

Before remote replay:

```text
ClipProbeArm actor=FF...... reason='remote OffsetGPMA' windowMs=1500
GPMAState actor=FF...... localPlayer=false variable='iGPMAOffsetType' present=true value=<n>
AnimRxInput ... event='OffsetGPMA' replay=true result=true
```

The remote proxy should also emit `ClipSelection` lines. Compare the final `selectedIndex`, `file` and `duration` against Player1.

The diagnostic question is simple:

- if Player1 selects a Helmet Toggle replacement HKX but Player2 remains on `GPMAOffsetAnimation.hkx`, OAR conditions differ between local player and STR proxy;
- if both select the same HKX, the remaining problem is later in animation playback/pose application rather than replacement selection;
- if `iGPMAOffsetType` differs, that graph-variable state may also need synchronization.

Collect `AnimSyncTogether.log` from both machines after one controlled helmet-removal test.
