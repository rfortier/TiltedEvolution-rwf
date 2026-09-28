# STRPM FOMOD installation

STRPluginMessagingAPI v0.8.1 ships as a single Vortex/FOMOD archive containing both the SKSE client and the official STR server resource.

## Installation types

### Client + Server — recommended for the host

Installs:

```text
Data/SKSE/Plugins/STRPluginMessagingAPI.dll
Data/SKSE/Plugins/STRPluginMessagingBridge.dll
Data/SKSE/Plugins/STRPluginMessagingAPI.ini
Data/SkyrimTogetherReborn/resources/strpm-chat-relay/main.lua
Data/SkyrimTogetherReborn/resources/strpm-chat-relay/strpm-chat-relay.manifest
```

The `-test` package additionally installs `STRPluginMessagingDiagnostic.dll` with the client payload.

### Client Only

Installs only the SKSE client files. Use this on players that connect to a STR server hosted elsewhere.

### Server Files Only

Installs only:

```text
Data/SkyrimTogetherReborn/resources/strpm-chat-relay/
```

Use this when updating or repairing the server resource without reinstalling the SKSE client components.

## Why this path is correct

STR 1.8.0's server resource loader uses a `resources` directory relative to the server process working directory. The standard STR archive places the server under `Data/SkyrimTogetherReborn`, therefore the FOMOD destination is:

```text
Data/SkyrimTogetherReborn/resources/strpm-chat-relay
```

The server resource contains both its Lua entry point and its `.manifest`, so no manual copy step is required when using the standard STR layout.

After starting the server, verify:

```text
[STRPM] Chat relay v3 loaded (ProxyResolver identity metadata enabled)
```
