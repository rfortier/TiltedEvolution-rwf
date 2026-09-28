# STRPM consumer contract

The STR Plugin Messaging API (STRPM) is the C ABI that lets an independent
SKSE plugin move bytes over the Skyrim Together Next session. The plugin
interfaces are consumed by six separate repositories, each pinned here as a
git submodule:

| submodule | role |
| --- | --- |
| `plugins/STRPluginMessagingAPI` | the API facade DLL and the binpatch bridge |
| `plugins/OStimTogether` | scene synchronisation consumer |
| `plugins/MorphSyncTogether` | morph/overlay synchronisation consumer |
| `plugins/IEDSyncTogether` | equipment display synchronisation consumer |
| `plugins/AnimSyncTogether` | animation graph variable/event consumer |
| `plugins/DAVSyncTogether` | Dynamic Armor Variants appearance consumer |
| `plugins/TradeTogether` | item and gold trading consumer |

## Layout

```text
Code/plugins/plugins.json                 packaging manifest (wizard + archive)
Code/plugins/STRPM/include/...            the authoritative interface header
Code/plugins/STRPM/contract.json          the ABI pin and the reviewed values
Code/plugins/tools/strpm_contract.py      the ABI gate
Code/plugins/tools/merge_fomod.py         the installer wizard generator
Code/plugins/tools/ModConfig5.0.xsd       the FOMOD 5.0 schema the wizard is checked against
```

## Why this directory exists

A consumer is built against a fixed ABI and vendors its own copy of the
interface header, so it can keep building on its own. That vendoring is also
the only place this project can break silently: a consumer shipping an older
copies of the header registers a channel whose layout no longer matches the
DLL that loads it, and the failure surfaces at runtime as garbage rather than
at build time.

`include/STRPluginMessagingAPI/STRPluginMessagingAPI.h` is the **single
authoritative copy**. The copies under `plugins/*` are vendored and are
expected to differ in whitespace, in the declaration/definition split of
helper functions, and in symbols a given consumer does not use.

## Checks

```text
python Code/plugins/tools/strpm_contract.py check
```

| id | check | catches |
| --- | --- | --- |
| C1 | authoritative header hash equals the pin in `contract.json` | an ABI edit that was never reviewed |
| C2 | every STRPM-owned symbol a consumer references is declared by the header it compiles against | a consumer using an interface its vendored header lacks |
| C3 | exported entry point names agree with the contract | a renamed `STR_Query...` export |
| C4 | constants shared between a vendored copy and the authoritative header carry identical values | a consumer pinned to an older `kInterfaceVersion` or stale limit |

C2 deliberately only considers symbols the STRPM namespace owns. A consumer
has its own `kSomething` constants and its own types, and those are none of
this contract's business.

## Commands

```text
python Code/plugins/tools/strpm_contract.py dump    # regenerate contract.json
python Code/plugins/tools/strpm_contract.py check   # verify (default; the CI gate)
python Code/plugins/tools/strpm_contract.py sync    # copy the header into consumers
```

`sync` is intentionally not automated into a commit. A consumer whose file
defines helpers the authoritative header only declares must keep those
definitions, so the resulting patch needs a human pass. It never runs in CI.

## Changing the ABI

1. Edit `Code/plugins/STRPM/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h`.
2. Run `dump` and commit the new pin together with the header edit.
3. Run `sync` and get the vendored copies updated in their repositories.

Step 2 is the whole point of the pin: the diff that changes the ABI and the
diff that re-records it are the same commit, so a silent ABI change cannot
survive review.

## Ownership note

The consumer repositories are read-only from this project: they are
cloned here, pinned, built, and packaged, but their remotes are not pushed to.
Any header change that must reach them is produced as a patch by `sync` and
applied upstream by whoever owns those repositories.
