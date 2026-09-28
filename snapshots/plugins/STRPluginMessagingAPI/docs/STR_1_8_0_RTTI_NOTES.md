# STR 1.8.0 RTTI notes for the stock-client bridge

Static analysis of the exact Nexus 1.8.0 `SkyrimTogether.exe` shows that MSVC RTTI information is retained even though the public executable uses a nonstandard section layout.

## Incoming chat type anchors

The executable contains multiple RTTI/type-name references for:

```text
NotifyChatMessageBroadcast
```

including names associated with:

```text
entt::dispatcher_handler<NotifyChatMessageBroadcast, ...>
TransportService
ServerMessageFactoryInit
```

This is more useful than the source-level `spdlog` string for the public build because the latter is absent from the on-disk executable.

One `entt::dispatcher_handler<NotifyChatMessageBroadcast,...>` MSVC `TypeDescriptor` can be recovered from the retained RTTI data. Its corresponding x64 `RTTICompleteObjectLocator` references lead to a concrete vftable in the public binary. This gives the bridge a structural route to the type-specific dispatcher implementation without depending on a source log string.

## Why this matters

The receive-side bridge needs to observe only `NotifyChatMessageBroadcast` instances and consume envelopes that begin with `STRPM|v2|`. Hooking a type-specific dispatcher path is preferable to a broad network receive hook because:

- unrelated STR packets remain untouched;
- normal chat can be forwarded unchanged;
- the hook can validate the RTTI identity before activation;
- the bridge does not need to reproduce the full STR packet decoder.

## Safety requirements

The eventual receive hook must verify all of the following before activation:

1. the public executable fingerprint matches the known 1.8.0 Nexus build;
2. the RTTI `TypeDescriptor` name matches `NotifyChatMessageBroadcast` exactly;
3. the associated `RTTICompleteObjectLocator` is internally consistent;
4. the vftable lies inside the mapped STR image;
5. the selected function pointer lies in executable memory;
6. the candidate function has valid x64 unwind metadata where applicable;
7. failure of any check leaves the bridge disabled (`kNotConnected`).

No absolute virtual address from this analysis should be treated as a public API. All addresses must be rediscovered from the mapped image at runtime.
