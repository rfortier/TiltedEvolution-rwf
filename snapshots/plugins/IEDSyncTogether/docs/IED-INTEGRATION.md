# IED slot-override integration

IEDSyncTogether deliberately does not modify Skyrim Together inventories. It
publishes a small C ABI that tells IED whether a given remote-proxy slot must
be empty or must use a specific form already present in that proxy inventory.

Released IED 1.7.4 exposes slot reads, but no per-actor slot override. The
companion source patch in `integration/ied-dev/ied-sync-together.patch` adds
that missing query at the selection boundary in `IEquipment::SelectSlotItem`.
IED keeps responsibility for model loading, nodes, transforms and candidate
lifetime; the synchronized owner selection intentionally takes precedence over
the remote proxy's NPC item filters.

## ABI

`IEDST_QuerySlotOverride(actorFormID, slotIndex, outFormID)` returns:

- `0`: the actor is not a synchronized remote proxy; use normal IED behavior.
- `1`: the authoritative remote slot is empty; select no item.
- `2`: select `outFormID` if it exists in IED's candidates, otherwise select
  no item.

The ABI is declared in `include/IEDSyncTogether/Interface.h`. It is intentionally
C-only at the DLL boundary, so the two plugins do not share C++ objects or
allocator ownership.

## Applying to ied-dev

From an `ied-dev` checkout:

```powershell
git apply C:\Users\chaos\Documents\Mods\IEDSyncTogether\integration\ied-dev\ied-sync-together.patch
```

The standalone `IEDSyncTogetherBridge.h` next to the patch is also provided for
review. A production release should use a build of IED containing this patch,
or an upstream equivalent. Without it, the synchronization DLL operates in
diagnostic mode; the INI fallback can hide all IED clones on remote proxies.

## STR messaging transport

IEDSyncTogether v0.3.1 defaults to `Transport=STR`. It dynamically loads
`STRPluginMessagingAPI.dll`, registers the channel
`chaos.ied_sync_together.slots.v1`, and sends the authoritative IED slot state
through the STR plugin-messaging layer.

In that default profile, IEDSyncTogether does not bind an Internet UDP port and
does not require router port forwarding. It also sets `RequireStrBridge=1`, so
the STRPM transport is accepted only when `STR_QueryPluginMessagingDiagnostics`
reports the `StrBridge` backend active. The previous IEDSyncTogether UDP
transport is still present as `Transport=UDP` or as an explicit fallback for
diagnostics and older test setups.

## Safety properties

- No item is added, removed, equipped or unequipped.
- A form is selected only when it already exists in IED's candidate set.
- Unknown actors keep normal IED behavior.
- An unresolved cross-client form identity produces an empty slot rather than
  falling back to an arbitrary NPC inventory item.
