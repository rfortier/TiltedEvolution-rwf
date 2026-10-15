#!/usr/bin/env python3
"""Does the .pex we ship still say what the .psc beside it says?

The ten scripts under GameFiles/Skyrim/scripts/source are staged into the
release package as .pex. The .pex is the artifact the game loads; the .psc is
the only record of what it was built from, and the two are separate committed
files.

Code/plugins/papyrus/Compile-ShippingScripts.ps1 rebuilds all ten in CI - into
a scratch directory, deliberately, so a build cannot overwrite the committed
artifact. That proves the sources still compile; it says nothing about the
files that ship, because nothing compares the two. A .psc edited without
regenerating its .pex passes CI and ships the old behaviour.

A .pex is not opaque. It holds a string table naming every function and event
the script declares and every literal it uses, so the pair can be compared
without a compiler and without the game. This gate reads both files and
requires the .pex to name what the .psc declares.

What this deliberately does not check: control flow, arithmetic, or any edit
that changes neither a name nor a literal (a comparison operator, a branch
body). Catching those needs the pinned compiler to reproduce the committed
bytes, and it does not - the shipped .pex came from the original compiler, and
SkyrimTogetherVerifyLaunchScript.psc was rewritten to match the bytes its .pex
already held rather than the other way round. Names and literals are the part
that is decidable here, and they are the part a script fix usually touches.

Exit status is 0 only when every shipped script agrees.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "GameFiles" / "Skyrim" / "scripts"
SOURCE = SCRIPTS / "source"

# A declaration: an optional type, the keyword, the name, then the argument
# list. `bool Function IsPlayer(Actor actor) global native`.
FUNCTION = re.compile(r"\bFunction\s+(\w+)\s*\(", re.IGNORECASE)
EVENT = re.compile(r"\bEvent\s+(\w+)\s*\(", re.IGNORECASE)
SCRIPT_NAME = re.compile(r"^\s*ScriptName\s+(\w+)", re.IGNORECASE | re.MULTILINE)

# Floors. A scan that finds almost nothing reports its own reach, not the
# repository's state, and must not read as a pass.
MIN_SCRIPTS = 8
MIN_LITERALS = 30

# Literals are compared byte for byte, so only ASCII ones can be looked up in
# the table. Non-ASCII literals are counted and reported rather than silently
# skipped.
ASCII_MAX = 126


def strip_comments_and_literals(text: str) -> tuple[str, list[str]]:
    """Return the code with comments removed, plus every string literal.

    A `;` outside a literal starts a comment, so the scan is character by
    character: literals are consumed whole (a `;` inside one is not a comment)
    and may span lines, which is how the launch dialog is written.
    """
    code: list[str] = []
    literals: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char == ";":
            while index < length and text[index] != "\n":
                index += 1
        elif char == '"':
            index += 1
            start = index
            while index < length and text[index] != '"':
                index += 1
            literals.append(text[start:index])
            index += 1
        else:
            code.append(char)
            index += 1
    return "".join(code), literals


def string_table(data: bytes, minimum: int = 3) -> list[str]:
    """Every run of printable ASCII bytes in the file.

    Searching per run rather than over one joined blob keeps a match from
    straddling two unrelated entries in the table.
    """
    runs: list[str] = []
    run = bytearray()
    for byte in data:
        if 32 <= byte < 127:
            run.append(byte)
        else:
            if len(run) >= minimum:
                runs.append(run.decode("ascii"))
            run.clear()
    if len(run) >= minimum:
        runs.append(run.decode("ascii"))
    return runs


def shipped_pairs() -> tuple[list[tuple[Path, Path]], list[str]]:
    """Pair each source script with the .pex shipped beside it.

    The lookup is case-insensitive because the two are not spelled the same:
    PetFramework_PetQuest.psc ships as petframework_petquest.pex. On Windows
    the filesystem hides that; on a case-sensitive checkout it would pair with
    nothing.
    """
    by_lower = {entry.name.lower(): entry for entry in SCRIPTS.iterdir() if entry.is_file()}
    pairs: list[tuple[Path, Path]] = []
    problems: list[str] = []
    for psc in sorted(SOURCE.glob("*.psc")):
        pex = by_lower.get((psc.stem + ".pex").lower())
        if pex is None:
            problems.append(f"{psc.name} has no .pex beside it, so the package ships nothing for it")
            continue
        pairs.append((psc, pex))
    return pairs, problems


def main() -> int:
    if not SOURCE.is_dir():
        print(f"SHIPPED PEX CHECK FAILED\n  {SOURCE.relative_to(ROOT)} is missing")
        return 1

    pairs, problems = shipped_pairs()

    # The reverse direction: a .pex with no source is a script nobody can
    # rebuild, and it is the file that ships.
    sources = {psc.stem.lower() for psc, _ in pairs}
    for pex in sorted(SCRIPTS.glob("*.pex")):
        if pex.stem.lower() not in sources:
            problems.append(f"{pex.name} ships but no .psc under source/ builds it")

    checked_literals = 0
    skipped_literals = 0
    checked_names = 0
    for psc, pex in pairs:
        code, literals = strip_comments_and_literals(psc.read_text(encoding="utf-8", errors="replace"))
        runs = string_table(pex.read_bytes())
        rel = psc.relative_to(ROOT)

        names = [psc.stem] + SCRIPT_NAME.findall(code) + FUNCTION.findall(code) + EVENT.findall(code)
        checked_names += len(names)
        for name in names:
            if not any(name in run for run in runs):
                problems.append(
                    f"{rel}: declares {name}, but {pex.name} does not name it - "
                    f"the shipped .pex was built from an older source"
                )

        for literal in literals:
            if not literal.strip():
                continue
            if any(ord(char) > ASCII_MAX for char in literal):
                skipped_literals += 1
                continue
            checked_literals += 1
            # A literal with real newlines is stored as one string, and the
            # table scan breaks at each newline, so each line is looked up on
            # its own. A literal holding a backslash escape has no real
            # newline, and none of its text is in the table - which is exactly
            # the bug this check exists for.
            for line in literal.split("\n"):
                if line.strip() and not any(line in run for run in runs):
                    shown = line if len(line) <= 60 else line[:57] + "..."
                    problems.append(
                        f"{rel}: literal {shown!r} is not in {pex.name} - "
                        f"the shipped .pex holds different text"
                    )

    if len(pairs) < MIN_SCRIPTS:
        problems.append(f"only {len(pairs)} script(s) paired, expected at least {MIN_SCRIPTS}")
    if checked_literals < MIN_LITERALS:
        problems.append(f"only {checked_literals} literal(s) compared, expected at least {MIN_LITERALS}")

    print(f"note: {len(pairs)} shipped script(s) paired with their source")
    print(f"note: {checked_names} name(s) and {checked_literals} literal(s) looked up in the string table")
    if skipped_literals:
        print(f"note: {skipped_literals} non-ASCII literal(s) cannot be looked up and were not compared")

    if problems:
        print(f"\nSHIPPED PEX CHECK FAILED ({len(problems)})")
        for problem in problems:
            print(f"  - {problem}")
        print("\n  A .pex that no longer matches its .psc ships the old script: the game")
        print("  loads the .pex, so the source edit is invisible in game. Rebuild with")
        print("  pwsh Code/plugins/papyrus/Compile-ShippingScripts.ps1 and commit both.")
        return 1

    print("SHIPPED PEX CHECK OK / every .pex shipped names what its .psc declares")
    return 0


if __name__ == "__main__":
    sys.exit(main())
