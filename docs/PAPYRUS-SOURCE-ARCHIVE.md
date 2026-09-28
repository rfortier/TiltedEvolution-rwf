# Papyrus: where the compiler comes from, and the copy kept here

This project compiles Papyrus scripts during its build. Nothing about that is
obvious from the outside - there is no Papyrus toolchain in the repository, no
game is installed on a CI runner, and the `.pex` files that ship are committed
rather than produced. This document records how those three facts fit together.

## The compiler is a pinned download, not a vendored tree

`Code/plugins/papyrus/Resolve-PapyrusCompiler.ps1` holds the only definition of
the pin:

| | |
| --- | --- |
| project | [russo-2025/papyrus-compiler](https://github.com/russo-2025/papyrus-compiler) (MIT) |
| release | `2026.03.15` |
| asset | `papyrus-compiler-windows.zip` |
| SHA-256 | `67b44c77d00a5cda986bec6af5c228a56abe6ec1fadcb4cfea5c858e6941140e` |

Two scripts dot-source it and call `Resolve-PapyrusCompiler`:

| script | compiles |
| --- | --- |
| `Compile-OStimConsentScripts.ps1` | OStimTogether's two consent scripts, into the plugin build tree |
| `Compile-ShippingScripts.ps1` | the ten scripts under `GameFiles/Skyrim/scripts/source/` |

The pin lives in one file on purpose. While each script carried its own copy of
the tag and the hash, bumping the compiler meant editing both, and the one that
got missed would keep building with a compiler nobody had chosen.

The unpacked compiler lands in `.papyrus-tool/`, which is gitignored: it is a
fetched build input, not repository content.

Both compile scripts change to the repository root before invoking the compiler
and restore the caller's directory afterwards. The compiler creates its own cache
directory - `.papyrus` - relative to the *process working directory*, not to any
path it is given, so running a script from elsewhere either writes that directory
somewhere unexpected or fails outright with `failed to make dir` when the
directory is not writable. `/.papyrus/` is gitignored for the same reason. A compiler update is a reviewed
change - bump the tag and the hash together, run both scripts, commit the
resulting `.pex` files.

## Why a source archive exists

`snapshots/tools/papyrus-compiler/` is a durability copy of the upstream source,
refreshed by `Tools/Scripts/papyrus_archive.py`. It is not a build input.

It exists because of a specific, verified problem. **The pinned release cannot
compile one of this project's own scripts.** The scanner in `2026.03.15` has no
escape handling at all: a string literal containing a backslash-n produces the
two characters `\` and `n` where the shipped `.pex` holds a real newline, and
the compile reports success. The fix exists only in commits newer than that tag,
and upstream has published no release since.

So this project depends on a compiler that is known-broken for its own input,
whose fix is unreleased, and which is reachable only through one GitHub release
asset. If that repository or that release disappears, the ability to build a
compiler disappears with it. The archive is what keeps that from being terminal.

```pwsh
python Tools/Scripts/papyrus_archive.py snapshot --source ../papyrus-compiler
python Tools/Scripts/papyrus_archive.py verify
```

### What the archive excludes, and the consequence

| excluded | why |
| --- | --- |
| `bin/` | upstream commits Bethesda's `PapyrusCompiler.exe`, `PapyrusAssembler.exe` and `PCompiler.dll` there. Redistributing them from a GPL-3 repository is not this project's call to make. |
| `docs/` | a 4.7 MB README screenshot and compiler benchmark tables. |
| `test-files/` | compiled test output; upstream's own `AGENTS.md` calls it "not source code". |

**The archive cannot produce `papyrus.exe`.** Building it needs the V toolchain
plus `modules/tests/sources`, which is a submodule and is not in the archive
either. What is preserved is the *source*, so a fix stays possible. Anyone
treating the archive as a build input will be disappointed, and that is the
honest description rather than an oversight.

### The upstream documentation has drifted, and the archive keeps it that way

Upstream's `AGENTS.md` is archived verbatim, drift included. It contradicts
itself about where the test header stubs live:

| line | claim |
| --- | --- |
| 35 | `modules/tests/psc_deps/` - "83 Skyrim base class header stubs (Actor.psc, Form.psc, etc.) used as dependencies for tests" |
| 152 | `modules/tests/sources/psc_deps/` - "Skyrim base script headers (dependencies for all tests)" |

Line 35 names a path that does not exist: there is no `modules/tests/psc_deps/`.
The real location is `modules/tests/sources/psc_deps/`, and `modules/tests/sources`
is a submodule pointing at `russo-2025/papyrus-compiler-test-sources`. The tests
agree with line 152, not line 35:

```text
projects_test.v:28                  modules/tests/sources/psc_deps
selective_headers_loading_test.v:18 modules/tests/sources/psc_deps
```

Whether that directory really holds 83 headers cannot be checked from here: the
submodule is not checked out in this repository, and it is not in the archive
either. What is checkable is that the path on line 35 is wrong and that the file
disagrees with itself.

The archive does not fix this. Editing it would stop the archive from being a copy
of commit `b9dc9951`, which is the only thing that makes it worth keeping - and
the digest would then have to be recomputed to match, which is exactly how a
"backup" quietly becomes a different document. The drift is recorded here instead,
where it can be read without pretending to be upstream.

This is also the concrete reason `verify` recomputes from the archive rather than
trusting the record: a file that is added, removed or edited fails the gate, and
so does a hand-edited `contentSha256`. What it cannot tell you is whether the
archive still matches upstream - that needs the upstream checkout. It catches the
realistic failure instead, and this repository has a precedent for it: an ignore
rule once silently excluded 22 files from the plugin snapshots.

## The escape hazard, and the one script it changed

`Compile-ShippingScripts.ps1` refuses to compile a shipping script that uses a
backslash escape, and fails if a script it has listed as unbuildable stops using
one. Both directions matter: the first stops a silent wrong `.pex`, the second
stops the exception list from rotting into a place where broken files are
forgotten.

That check fired on real code. `SkyrimTogetherVerifyLaunchScript.psc` built its
error dialog as four concatenated literals joined by line continuations, with
`\n` for the line breaks:

```papyrus
Debug.MessageBox("Skyrim Together Error\n\n" \
               + "Skyrim Together is not running!\n" \
               ...
```

The dialog was rewritten as one multi-line literal. The text is unchanged, and
that was checked rather than assumed: the shipped `.pex` stores the message as
four separate string-table entries that the VM concatenates at run time, and
those four, concatenated, are byte-identical to the single string the rewritten
source produces.

```text
shipped fragments (4):
    'Skyrim Together Error\n\n'
    'Skyrim Together is not running!\n'
    'To play Skyrim Together and access multiplayer features, '
    "launch SkyrimTogether.exe located in 'Skyrim Special Edition\\Data\\SkyrimTogetherReborn'"

rewritten (1): the four above, concatenated, byte for byte
```

Two things to know if this file is ever edited again. A line continuation
followed by a newline inside a multi-line literal **crashes the pinned
compiler** (`INTERNAL COMPILER ERROR`, `trim_slash_line_break`), so the rewrite
does not use one. And a genuine backslash in a path - `Skyrim Special
Edition\Data\...` - is left as a single backslash, which is what both compilers
store.

## Headers

The compiler takes `-h` per directory and does not recurse, so every directory
that declares something the scripts use is passed explicitly.
`Code/plugins/papyrus/stubs/` is that directory, and this repository owns it.

The stubs are compile-only headers: declarations of the types and functions the
scripts call, with no bodies. **They are never packaged.** The real scripts ship
with the game, and a stub in a player's `Data/Scripts/` would shadow the real
one.

They are not exhaustive and are not meant to be. The game's own script sources
are the reference; these exist so CI, which has no game installed, can type-check
and generate bytecode. When a script starts calling something new, add the
declaration here.

Two things learned the hard way, both of which cost a compile cycle:

- **Every declaration needs a body or `native`.** A stub function with neither
  fails with `Parser error: unexpected end of file` at the *end of the file*, not
  at the missing `EndFunction` - which points at the wrong place entirely.
- **A derived stub's parent must resolve, and the source directory must be a
  header directory.** `ReferenceAlias` extends `Alias`; if `Alias` is missing,
  the error names the derived type. Two of the shipping scripts extend each
  other, which is why `GameFiles/Skyrim/scripts/source` is passed as a header
  directory as well as an input.
