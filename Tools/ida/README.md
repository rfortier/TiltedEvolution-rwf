# 1.5.97 address-recovery kit

## Current tool: `match_capstone.py` (no IDA required)

Recovers the remaining 1.5.97 RVAs for AE address-library ids entirely in
pure Python (capstone + numpy) -- no IDA, no license locks, no giant IDB:

* **function targets** -- normalized instruction-token Dice similarity (address
  operands become placeholders), candidate pool = union of four bracket
  interpolations (full/code anchors x id-order/rva-order), plus mapped-callee
  and shared-string bonuses. Every accepted result was additionally verified
  by side-by-side disassembly of the 1.6.1170 and 1.5.97 versions.
* **full-.text scan** (`/tmp/scan_fulltext.py` pattern) -- for targets with no
  reliable bracket, scans all ~190k 1.5.97 function starts against the target
  with a size prefilter and a shared token cache (~2 min for all targets).
* **data targets** -- rip-relative xref ordering through already-mapped
  containing functions (used where possible; several data statics remain
  unmapped by design: no stable cross-version signal).

Run (from the repo root):

    python3 Tools/ida/match_capstone.py \
        --ae-exe "<1.6.1170 SkyrimSE.exe>" \
        --se-exe "<1.5.97 SkyrimSE.exe>" \
        --ae-bin  /tmp/addrlib/SKSE/Plugins/versionlib-1-6-1170-0.bin \
        --se-bin  /tmp/addrlib/SKSE/Plugins/version-1-5-97-0.bin \
        --map     GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map \
        --missing Tools/missing_1_5_97_ids.txt \
        --out-overrides Tools/ida/st_overrides.txt \
        --out-report    Tools/ida/st_capstone_report.json

### Regenerating the shipped maps

The ten `versionlib-ae-to-se-1-5-*.map` files are produced from three override
files, and all three are needed - `st_overrides_graph.txt` alone carries the
ten data statics and ten functions recovered by `recover_1597.py`, so leaving
it out silently drops twenty ids:

    python3 Tools/Scripts/gen_se_map_from_history.py \
        --se-bin       GameFiles/Skyrim/SKSE/Plugins/version-1-5-97-0.bin \
        --overrides    Tools/ida/st_overrides.txt \
        --overrides    Tools/ida/st_overrides_graph.txt \
        --overrides    Tools/ida/st_overrides_all.tsv \
        --se-bins-dir  GameFiles/Skyrim/SKSE/Plugins \
        --out          GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map

That reproduces the shipped 1.5.97 map exactly except for one id:

    14774   BSRandom::GetGenerator. st_overrides_graph.txt carries it with a
            comment saying it is "not yet folded into the map", and that is
            still true - it is not in any shipped map. Do not "fix" this by
            dropping the id from the override file; the file is the evidence
            log, the map is what ships.

The generator now refuses to write a map that assigns one offset to two ids, or
whose entries are badly out of address order. Both are real defects: the two
address libraries are injective (778674 ids -> 778674 distinct offsets, and
428461 -> 428461), so a correct id map must be injective too. See
docs/PITFALLS.md section 11 for what a duplicate cost when it shipped.

Feed the result into the map generator:

    python3 Tools/Scripts/gen_se_map_from_history.py \
        --se-bin  /tmp/addrlib/SKSE/Plugins/version-1-5-97-0.bin \
        --overrides Tools/ida/st_overrides.txt \
        --se-bins-dir /tmp/addrlib/SKSE/Plugins \
        --out GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map

Status at the time of that run: 3033/3058 codebase ids mapped (99.2%), and all
10 1.5.x maps regenerated and validated (3667/3667). Superseded - see the
current status at the end of this file.

## Bracket-free recovery: `recover_1597.py` (no IDA required)

`match_capstone.py` interpolates between mapped neighbours, so it gives up
where the neighbourhood is empty ("no bracket") and on data statics whose
referencing functions are all unmapped. `recover_1597.py` drops the bracket and
identifies an individual 1.6.1170 function directly:

* **string literals** it references - the bytes are identical in both builds,
  so the same literal in 1.5.97 `.rdata` leads back to the same function;
* its **position in the call graph** relative to already-mapped functions;
* the **globals it touches** that are already mapped - the anchor grid is
  mostly RTTI and data, so this is often the only signal a literal-free
  function has;
* normalized token similarity as the tie breaker.

With a function located, a data static referenced from it follows: the ordered
rip-relative references of both bodies are aligned with each other and the slot
holding the target on the 1.6 side names its 1.5.97 counterpart. Feeding
CommonLibSSE-NG's `RELOCATION_ID`/`VariantID` SE-id pairs in as extra anchors
tripled the grid (3679 -> 11966) and agreed with the history-derived map on
698 of the 701 ids they share.

    python3 Tools/ida/recover_1597.py \
        --ae-exe "<1.6.1170 SkyrimSE.exe>" --se-exe "<1.5.97 SkyrimSE.exe>" \
        --ae-bin GameFiles/Skyrim/SKSE/Plugins/versionlib-1-6-1170-0.bin \
        --se-bin GameFiles/Skyrim/SKSE/Plugins/version-1-5-97-0.bin \
        --map    GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map \
        --missing Tools/missing_1_5_97_ids.txt \
        --out-overrides Tools/ida/st_overrides_graph.txt \
        --out-report    Tools/ida/st_graph_report.json

## Patch offsets: `patch_offsets_1597.py`

Mapping an id only names a function start. The client also patches *inside*
those functions at offsets measured on 1.6.1170, and those do not survive a
recompile. This aligns the two disassemblies instruction by instruction and
reports where each site moved to, refusing any result whose instruction
mnemonic or length disagrees. Output: `st_patch_offsets_1597.tsv`, consumed by
hand into the `GamePatch::Site` entries in the client.

Status: 3069/3082 codebase ids mapped (99.6%), 3698/3698 offsets validated
against version-1-5-97-0.bin, all 10 1.5.x maps regenerated. The 13 that remain
and what they cost are listed in Tools/missing_1_5_97_ids.txt. The totals are
checked against this file by Tools/Scripts/check_docs.py, so they cannot drift
here without the gate failing. Counted by
`gen_ae_to_se_map.py::collect_codebase_ids`; see that file for the two blind
spots (a `*.[ch]pp` glob that skipped every `.h`, and commented-out calls
counted as live) that were fixed when these numbers were last recomputed.

Consumed by the client: the rows of `st_patch_offsets_1597.tsv` that report an
aligned offset are wired into the `GamePatch::Site` entries in the client as
`{modern, legacy, "1.5.97"}`. `Site::legacyMeasuredOn` records the build the
offset was measured on, and `GamePatch::At()` refuses to apply it to any other
1.5.x version - ten different 1.5.x libraries ship and their code is not
interchangeable. Sites with no aligned row stay at `kUnknown` and are skipped
and logged at runtime.

## Legacy IDA kit (superseded)

`1_apply_st_map.py` / `2_apply_ae_labels.py` / `3_export_targets.py` were the
IDA-Pro flow used before `match_capstone.py`; keep them only as reference for
manually resolving the last ids (near-twin siblings, tiny bodies, data
statics listed in Tools/missing_1_5_97_ids.txt).

The IDA-side workbench built on top of that flow, in pipeline order:

| script | role |
|---|---|
| `export_target_meta_1170.py` | dumps size/decompile/callees/strings per target from the 1.6.1170 IDB -> `st_target_meta_1170.json` |
| `extract_candidates_1597.py` | enumerates 1.5.97 candidates per bracket with their matching signals |
| `match_targets_1597.py` | scores candidates inside the IDB -> `st_match_report.json` |
| `match_offline.py` | same scoring without IDA in memory, merges vtable-derived overrides |
| `probe_1597.py` | interactive xref / byte-pattern / decompile probe used for the last hand-resolved ids |
| `find_comctl32_345.py` | one-off: locates who referenced the COMCTL32 ordinal-345 import slot |
| `post_analysis_idalib.py` | labels + target workbench pass over a freshly analysed IDB |

All of them need a local IDB and are not part of any build.
