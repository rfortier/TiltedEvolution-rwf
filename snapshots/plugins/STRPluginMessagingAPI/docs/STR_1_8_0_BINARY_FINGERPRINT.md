# Skyrim Together Reborn 1.8.0 binary fingerprint

This document records the exact public Nexus build used while developing the stock-client STRPM chat bridge.

## Nexus package

Archive:

```text
Skyrim Together Reborn-69993-1-8-0-1762551509.zip
```

Embedded client executable:

```text
SkyrimTogetherReborn/SkyrimTogether.exe
```

Executable size:

```text
7,058,432 bytes
```

SHA-256:

```text
77f23c9c82c412252b5c4491a09d7ab4349cbc6c77992c4766882f54798cb99d
```

The matching TiltedEvolution source tag is `v1.8.0`, commit:

```text
9c23efa422bbc1e5c06eef5522ca73971a513e35
```

## PE layout observation

The public executable is not laid out as a conventional unprotected PE containing normal `.text` and `.rdata` sections. Its section table contains the following notable sections:

```text
.game
.pdata
.xcode
CPADinfo
.fptable
.cld
.clr
.zdata
.rsrc
```

The source-level logging anchor used by `OverlayClient::ProcessChatMessage()`:

```text
Send chat message of type {}: '{}'
```

is not present verbatim in the file on disk. This means a static file-offset or ordinary `.text` signature derived from the source build is not safe for the public Nexus executable.

The executable still contains runtime/unwind metadata and identifiable STR type strings, including `NotifyChatMessageBroadcast`, but the client implementation must resolve the relevant code after the launcher has mapped/unpacked its runtime image.

## Bridge policy

For the 1.8.0 compatibility bridge:

1. Validate that the loaded `SkyrimTogether.exe` corresponds to the known public build where possible.
2. Do not use hard-coded absolute addresses.
3. Do not assume `.text` / `.rdata` exist in the on-disk image.
4. Resolve anchors and executable xrefs only after STR has initialized its mapped runtime code.
5. Use x64 unwind metadata (`RtlLookupFunctionEntry`) where available to recover exact function bounds.
6. Require multiple independent checks before enabling a call or hook.
7. On any mismatch, return `kNotConnected` instead of executing an unverified address.

This fingerprint is intentionally version-specific. New STR releases must be fingerprinted separately before the bridge enables version-dependent hooks.
