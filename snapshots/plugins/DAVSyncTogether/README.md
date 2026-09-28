# DAVSync Together

DAVSync Together is an SKSE/CommonLibSSE-NG plugin for **Skyrim Special Edition / Anniversary Edition** that synchronizes the visual result of **Dynamic Armor Variants (DAV)** between players in **Skyrim Together Reborn**.

## Scope

DAVSync Together handles only DAV-controlled equipment visuals. RaceMenu appearance remains MorphSync Together's responsibility and IED display objects remain IEDSync Together's responsibility.

## Current status

**v0.6.3 — post-reequip Helmet Toggle rebound guard**

v0.5.2 established the head-safe DAV-native path for hidden helmets. v0.6.0 added visible replacement (`REPLACED`) synchronization. v0.6.1 fixed actor-specific matching when an ARMO exposes several sex/race Armor Addons but the actor renders only one. v0.6.2 added proxy-safe selection for lowered-hood variants.

Testing OStim Together scene teardown exposed a separate Helmet Toggle 2 lifecycle edge case. OStim can restore a previously stripped helmet as a normal `VISIBLE` armor item, then Helmet Toggle 2 can reapply `HT_HiddenHelmetWorkaround` several seconds later. If DAVSync mirrors that automatic rebound immediately, an STR proxy can be left with the worn helmet hidden even when the corresponding alternate display has not been restored yet.

v0.6.3 adds a narrow post-reequip guard for this sequence. When a DAV-tracked armor reappears after being absent, DAVSync arms a short 15-second guard. If the first DAV transition during that window is `HIDDEN` specifically through `HT_HiddenHelmetWorkaround`, that one rebound is suppressed instead of being transmitted. The guard is then consumed. Normal later Helmet Toggle actions continue to synchronize as before.

Expected diagnostics:

```text
DAVST POST_REEQUIP_GUARD armoStable="..." state=VISIBLE action=armed windowMs=15000
DAVST POST_REEQUIP_GUARD armoStable="..." variant="HT_HiddenHelmetWorkaround" ageMs=... action=suppress-first-hidden-rebound
```

If the same workaround transition happens after the guard window, it is treated as a normal user-visible DAV state and is sent:

```text
DAVST POST_REEQUIP_GUARD armoStable="..." variant="HT_HiddenHelmetWorkaround" ageMs=... action=expired-send
DAVST VARIANT_MATCH ... state=HIDDEN variant="HT_HiddenHelmetWorkaround" ... action=send-selected
```

### REPLACED variant selection

When DAV renders one or more Armor Addons that differ from the equipped ARMO's base Armor Addons, DAVSync classifies the state as `REPLACED`.

For `REPLACED`, DAVSync now:

- collects replacement ARMA reachable from each base ARMA through `replaceByForm` or the first matching `replaceBySlot` rule;
- accepts the actor's actually rendered ARMA as a subset of those possible replacements;
- allows unchanged base ARMA to coexist with replacements for multi-addon armor;
- requires at least one observed replacement ARMA before considering the variant a match;
- strongly de-prioritizes variants whose name or `linkTo` is player-scoped when choosing a variant for an STR proxy;
- prefers the generic/NPC DAV variant when it renders the same replacement ARMA;
- remains conservative if several equally suitable proxy-safe variants remain.

A selected replacement is sent through STRPM and applied by DAV itself:

```text
DynamicArmor.ApplyVariant(proxy, variant)
DynamicArmor.ResetVariant(proxy, armor)
```

### REPLACED diagnostics

A real replacement produces:

```text
DAVST ARMOR_STATE ... state=REPLACED baseARMA=[...] activeARMA=[...]
DAVST REPLACED_MATCH ... candidates=N names=[...]
```

For the Common Mage Hood + Dynamic Lowered Hoods test, DAVSync should select the generic NPC-safe variant:

```text
DAVST VARIANT_MATCH ... state=REPLACED variant="LoweredHoods" candidates=3 action=send-selected
DAVST STRPM TX ... state=REPLACED variant="LoweredHoods" result=ok
```

If candidates remain indistinguishable after proxy-scope ranking:

```text
DAVST VARIANT_MATCH ... state=REPLACED candidates=N action=ambiguous-not-sent
```

### Head-safe hidden helmets

The v0.5.2 behavior remains unchanged:

- ambiguous hidden helmet variants are ranked using DAV's `overrideHead` semantics;
- `showAll` / `showHead` are preferred over head-hiding variants;
- Head/Hair ARMO records never use the direct `CullNode()` fallback;
- DAV itself handles head/hair visibility and 3D refresh.

## Multiplayer architecture

```text
local DAV rendered state
    -> classify VISIBLE / HIDDEN / REPLACED
    -> infer/rank DAV variant from DAV JSON config
    -> guard post-reequip HT_HiddenHelmetWorkaround rebound
    -> prefer proxy-safe generic/NPC variant over local-player-only variant
    -> load-order-safe ARMO/ARMA identity
    -> STRPM channel DAVSyncTogether.State.v2
    -> STRPM Sender.connectionID
    -> STRPM ProxyResolver
    -> remote proxy Actor
    -> DynamicArmor.ApplyVariant / ResetVariant
```

DAVSync creates **no custom UDP transport**. Messaging and proxy identity are provided exclusively by STRPluginMessagingAPI.

## Stable form identity

ARMO and ARMA forms are represented as:

```text
plugin filename + local FormID
```

rather than client-specific runtime FormIDs.

## DAV state model

- `VISIBLE` — rendered ARMA belongs to the original ARMO.
- `HIDDEN` — ARMO remains worn but no matching biped ARMA is rendered.
- `REPLACED` — rendered ARMA differs from the ARMO's original Armor Addons.
- `UNEQUIPPED` — a previously tracked DAV ARMO is no longer worn.

## Requirements

Runtime:

- Skyrim Special Edition / Anniversary Edition
- SKSE
- Skyrim Together Reborn
- Dynamic Armor Variants
- STRPluginMessagingAPI (STRPM)

Build:

- Visual Studio with C++ workload
- CMake 3.24+
- vcpkg
- CommonLibSSE-NG
- nlohmann-json

## Building

```bat
build_release.bat
```

The Vortex-ready archive is generated under:

```text
dist/DAVSyncTogether-<version>.zip
```

## Test procedure for v0.6.3

Install the same build and DAV configuration on Player1 and Player2 and connect both players before testing.

### Regression A — normal Helmet Toggle synchronization

1. start outside OStim with the helmet visible;
2. hide it normally through Helmet Toggle 2;
3. confirm the remote helmet hides and head/hair remain correct;
4. show it again;
5. confirm the remote helmet returns.

This normal toggle path must still produce `action=send-selected` for `HT_HiddenHelmetWorkaround`.

### Regression B — OStim scene teardown rebound

1. start an OStim scene that strips the helmet;
2. end the scene and let OStim restore the helmet;
3. confirm DAVSync logs `POST_REEQUIP_GUARD ... action=armed` when the armor reappears;
4. wait for Helmet Toggle 2 to reapply `HT_HiddenHelmetWorkaround`;
5. confirm DAVSync logs `action=suppress-first-hidden-rebound` and does **not** send a new `HIDDEN` packet for that rebound;
6. confirm the remote proxy does not get stuck with the helmet missing;
7. perform a new explicit Helmet Toggle action afterwards and confirm synchronization resumes normally.

### Regression C — visible replacement variants

Recommended regression item: **Common Mage Hood** with **Dynamic Lowered Hoods**.

1. equip the Common Mage Hood in its raised state;
2. trigger the lowered-hood DAV variant;
3. confirm the local log reports `state=REPLACED` with an active ARMA from `DynamicLoweredHoods.esp`;
4. confirm `REPLACED_MATCH` reports the three known candidates;
5. confirm DAVSync chooses `LoweredHoods`, not either `HT_*Player` variant;
6. confirm the remote proxy shows the lowered hood;
7. switch back to the raised hood and confirm `ResetVariant` restores the remote model;
8. repeat in the opposite player direction.

Expected sender log:

```text
DAVST ARMOR_STATE ... state=REPLACED baseARMA=[...] activeARMA=["DynamicLoweredHoods.esp|..."]
DAVST REPLACED_MATCH ... candidates=3 names=["HT_LoweredHoodsHairOnlyPlayer","HT_LoweredHoodsPlayer","LoweredHoods"]
DAVST VARIANT_MATCH ... state=REPLACED variant="LoweredHoods" candidates=3 action=send-selected
DAVST STRPM TX ... state=REPLACED variant="LoweredHoods" result=ok
```

Expected receiver log:

```text
DAVST DAV_API ApplyVariant ... variant="LoweredHoods" dispatched=1
DAVST STRPM RX_STATE ... state=REPLACED variant="LoweredHoods" davDispatch=1 fallbackNodes=0 apply=1
```

Returning to the base model should produce `VISIBLE` and `ResetVariant ... dispatched=1`.

## Versioning

- small fixes increment PATCH;
- larger feature changes increment MINOR and reset PATCH to zero;
- README and deployment ZIP version follow the project version.

## License

MIT