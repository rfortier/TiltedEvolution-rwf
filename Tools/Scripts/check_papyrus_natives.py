#!/usr/bin/env python3
"""Does every Papyrus native we declare actually get registered, under the same name?

A `native` function in a `.psc` is a declaration only: the game binds it to C++
at load time, matching the *script name* and the *function name* as strings. Both
sides of that match are written by hand, in different languages, in different
files, and neither compiler reads the other. A typo on either side is silent -
the script still compiles, the plugin still loads, and the call returns the
return type's default value (see the comment in SkyrimTogetherVerifyLaunchScript.psc).

That is the same shape as the overlay bridge and the message factories: two lists
that must agree and nothing comparing them.

Rules:
  R1  every first-party `native` declaration is registered, with the same class
      and function name;
  R2  every registration names a declaration that exists;
  R3  where the C++ side states a parameter count (the client's BindNativeMethod
      path does, in the NativeFunction base constructor), it equals the number of
      parameters the `.psc` declares. A mismatch makes the VM refuse the call;
  R4  the class name in a registration is a `Scriptname` that really exists - a
      misspelled class binds nothing at all.

What this deliberately does not check: the SKSE-side parameter *types*. SKSE
deduces arity from the C++ signature at compile time, so there is no number in
the source to compare, and the types are `RE::` engine types with no textual
counterpart in Papyrus. Names and the one count that is written down are what is
decidable here.

Exit status is 0 only when every pair agrees.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# First-party scripts only. Code/plugins/papyrus/stubs/ is a mirror of the
# vanilla API we do not own, plugins/OStimNG/ is a third-party mod, and
# snapshots/ holds archived copies of older plugin checkouts. A native declared
# in any of those is not ours to register.
SCRIPT_DIRS = [
    ROOT / "GameFiles" / "Skyrim" / "scripts" / "source",
    ROOT / "plugins" / "TradeTogether" / "Source" / "Scripts",
    ROOT / "plugins" / "OStimTogether" / "compat" / "OStimUIConsent" / "Data" / "Scripts" / "Source",
    ROOT / "plugins" / "OStimTogether" / "optional" / "OCumIntegration" / "Data" / "Scripts" / "Source",
]

# The client's three natives live in a directory that is never a submodule, so
# this floor holds on every checkout, submodules or not.
MIN_DECLARATIONS = 3

SCRIPTNAME = re.compile(r"^\s*ScriptName\s+(\w+)", re.IGNORECASE | re.MULTILINE)
# `bool Function IsPlayer(Actor actor) global native`
NATIVE = re.compile(
    r"^\s*[\w\[\]]+\s+Function\s+(\w+)\s*\(([^)]*)\)\s*(?:\w+\s+)*?native\b",
    re.IGNORECASE | re.MULTILINE,
)

# `new BSScript::IsRemotePlayerFunc("IsRemotePlayer", "SkyrimTogetherUtils", ...)`
BIND = re.compile(
    r"BindNativeMethod\(\s*new\s+BSScript::(\w+?)\s*\(\s*\"([^\"]+)\"\s*,\s*\"([^\"]+)\"\s*,",
    re.MULTILINE,
)
# `XFunc::XFunc(...) : NativeFunction(apFunctionName, apClassName, true, 1)`
CTOR = re.compile(r"(\w+)Func::\1Func\s*\(", re.MULTILINE)
NATIVE_CTOR = re.compile(r"NativeFunction\s*\(", re.MULTILINE)

# `a_vm->RegisterFunction("GetKey", kScriptName.data(), GetKey);`
REGISTER = re.compile(
    r"RegisterFunction\(\s*\"([^\"]+)\"\s*,\s*([^,]+?)\s*,",
    re.MULTILINE,
)
# `bool BSScript::IsPlayerFunc::MarshallAndDispatch(...)` followed by its body.
DISPATCH = re.compile(
    r"(\w+)Func::MarshallAndDispatch\s*\([^)]*\)\s*\n\{",
    re.MULTILINE,
)
# `constexpr std::string_view kScriptName = "TradeTogetherNative";`
CLASS_CONST = re.compile(
    r"(?:constexpr\s+)?(?:static\s+)?(?:const\s+)?std::string_view\s+(\w+)\s*=\s*\"([^\"]+)\"",
    re.MULTILINE,
)

CXX_GLOBS = ["Code/client/**/*.cpp", "plugins/*/src/*.cpp"]


def strip_comments(text: str) -> str:
    """Remove `;` comments without touching string literals.

    A `;` inside a literal is not a comment, and a literal inside a comment is
    not code. Both appear in these files.
    """
    out: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char == ";":
            while index < length and text[index] != "\n":
                index += 1
        elif char == '"':
            start = index
            index += 1
            while index < length and text[index] != '"':
                index += 1
            index += 1
            out.append(text[start:index])
        else:
            out.append(char)
            index += 1
    return "".join(out)


def cpp_sources() -> list[Path]:
    found: list[Path] = []
    for pattern in CXX_GLOBS:
        found.extend(ROOT.glob(pattern))
    return sorted(set(found))


def dispatch_bodies() -> tuple[list[tuple[str, str, Path, int]], list[str]]:
    """Every `MarshallAndDispatch` body: (class-name suffix, body, file, line).

    A dispatch that returns false without writing `apResult` reports a failure to
    the VM for a question that had an answer. The `.psc` side never checks the
    return, so the script sees whatever the VM substitutes for an unset result -
    and the shipped callers all cast an ObjectReference with `as Actor` first,
    which yields None on every ordinary activation.
    """
    found: list[tuple[str, str, Path, int]] = []
    notes: list[str] = []
    for source in cpp_sources():
        text = source.read_text(encoding="utf-8", errors="replace")
        if "MarshallAndDispatch" not in text:
            continue
        for match in DISPATCH.finditer(text):
            # Walk braces from the opening one so a nested block cannot end the
            # body early.
            start = text.index("{", match.end() - 1)
            depth = 0
            index = start
            while index < len(text):
                if text[index] == "{":
                    depth += 1
                elif text[index] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                index += 1
            body = text[start : index + 1]
            # Commented-out code is not code: `// return false;` must not read as
            # a live early exit, and a comment mentioning the result write must
            # not read as one either.
            body = re.sub(r"//[^\n]*", "", body)
            found.append(
                (match.group(1), body, source, text[: match.start()].count("\n") + 1)
            )
    return found, notes


def declarations() -> tuple[list[tuple[str, str, int, Path]], list[str]]:
    """Every first-party `native`: (class, function, parameter count, file)."""
    found: list[tuple[str, str, int, Path]] = []
    notes: list[str] = []
    for directory in SCRIPT_DIRS:
        if not directory.is_dir():
            notes.append(
                f"{directory.relative_to(ROOT)} is not checked out, so the scripts it holds "
                f"were not compared"
            )
            continue
        for psc in sorted(directory.glob("*.psc")):
            text = strip_comments(psc.read_text(encoding="utf-8", errors="replace"))
            script_names = SCRIPTNAME.findall(text)
            if not script_names:
                continue
            for name, params in NATIVE.findall(text):
                count = len([p for p in params.split(",") if p.strip()])
                found.append((script_names[0], name, count, psc))
    return found, notes


def native_ctor_arity() -> tuple[dict[str, int], list[str]]:
    """Parameter count per native-function class, from the `NativeFunction(...)` base call.

    The count is written in the constructor's initialiser list, which lives in a
    different file from the `BindNativeMethod` call that uses it - the hook that
    binds is Code/client/Games/Misc/BSScript.cpp and the functions it binds are
    declared in Code/client/Games/Skyrim/Misc/BSScript.cpp. Reading only the
    binding file would leave this rule with nothing to compare and no way to say so.
    """
    arity: dict[str, int] = {}
    notes: list[str] = []
    for source in cpp_sources():
        text = source.read_text(encoding="utf-8", errors="replace")
        if "NativeFunction(" not in text:
            continue
        for match in CTOR.finditer(text):
            # The argument list can span lines, so the search is bounded by the
            # next `;` rather than by a line.
            tail = text[match.end() : match.end() + 600]
            stop = tail.find(";")
            window = tail if stop < 0 else tail[:stop]
            call = NATIVE_CTOR.search(window)
            if not call:
                continue
            args = window[call.end() :]
            # `..., aIsStatic, aParameterCount)` - the count is the last argument.
            numbers = re.findall(r",\s*(\d+)\s*\)", args)
            if numbers:
                arity[match.group(1)] = int(numbers[-1])
            else:
                notes.append(
                    f"{source.relative_to(ROOT)}: {match.group(1)}Func passes a parameter count "
                    f"that is not a literal, so its arity could not be compared"
                )
    return arity, notes


def registrations(arity: dict[str, int]) -> tuple[list[tuple[str, str, int | None, Path, int]], list[str]]:
    """Every registration: (class, function, parameter count or None, file, line)."""
    found: list[tuple[str, str, int | None, Path, int]] = []
    notes: list[str] = []
    for source in cpp_sources():
        text = source.read_text(encoding="utf-8", errors="replace")
        if "BindNativeMethod" not in text and "RegisterFunction" not in text:
            continue

        for match in BIND.finditer(text):
            func_type, name, class_name = match.groups()
            # `BindNativeMethod(new BSScript::IsPlayerFunc(...))` names the type
            # with its suffix; the constructor map is keyed without it.
            key = func_type[:-4] if func_type.endswith("Func") else func_type
            if key not in arity:
                notes.append(
                    f"{source.relative_to(ROOT)}: {func_type} registers {class_name}::{name} but "
                    f"declares no NativeFunction base constructor, so its parameter count is unstated"
                )
            found.append(
                (class_name, name, arity.get(key), source, text[: match.start()].count("\n") + 1)
            )

        # The SKSE path names the class through a constant or a literal.
        class_names = {name: value for name, value in CLASS_CONST.findall(text)}
        for match in REGISTER.finditer(text):
            name, expr = match.groups()
            expr = expr.strip()
            literal = re.fullmatch(r"\"([^\"]+)\"", expr)
            if literal:
                class_name = literal.group(1)
            else:
                member = re.fullmatch(r"(\w+)(?:\.data\(\))?", expr)
                if member and member.group(1) in class_names:
                    class_name = class_names[member.group(1)]
                else:
                    notes.append(
                        f"{source.relative_to(ROOT)}: cannot resolve the class name for {name} "
                        f"from `{expr}`, so it was not compared"
                    )
                    continue
            found.append((class_name, name, None, source, text[: match.start()].count("\n") + 1))
    return found, notes


def main() -> int:
    declared, notes = declarations()
    arity, arity_notes = native_ctor_arity()
    registered, reg_notes = registrations(arity)
    notes.extend(arity_notes)
    notes.extend(reg_notes)

    problems: list[str] = []
    declared_keys = {(cls, name) for cls, name, _, _ in declared}
    registered_keys = {(cls, name) for cls, name, _, _, _ in registered}

    for cls, name, count, psc in declared:
        if (cls, name) not in registered_keys:
            problems.append(
                f"{psc.relative_to(ROOT)}: {cls}.{name} is declared native but no C++ code "
                f"registers it - the call returns the return type's default value forever"
            )

    for cls, name, count, source, line in registered:
        rel = source.relative_to(ROOT)
        if (cls, name) not in declared_keys:
            problems.append(
                f"{rel}:{line}: registers {cls}::{name}, but no first-party .psc declares it - "
                f"either the name is misspelled or the declaration was deleted"
            )
            continue
        if count is not None:
            declared_count = next(c for k, n, c, _ in declared if (k, n) == (cls, name))
            if count != declared_count:
                problems.append(
                    f"{rel}:{line}: registers {cls}::{name} with {count} parameter(s), but the "
                    f"script declares {declared_count} - the VM refuses a call whose argument "
                    f"count does not match"
                )

    # A class name in a registration must be a Scriptname that exists.
    known_classes = {cls for cls, _, _, _ in declared}
    for cls, name, _, source, line in registered:
        if cls not in known_classes:
            problems.append(
                f"{source.relative_to(ROOT)}:{line}: registers into script class '{cls}', which no "
                f"first-party .psc declares"
            )

    # A dispatch that returns false reports the call as failed, and the script
    # then receives the return type's default rather than an answer. The shipped
    # callers pass an unchecked `as Actor` cast, so None arrives on ordinary
    # activations - a boolean native must answer it, not fail it.
    bodies, _ = dispatch_bodies()
    for cls, body, source, line in bodies:
        if re.search(r"\breturn\s+false\s*;", body):
            problems.append(
                f"{source.relative_to(ROOT)}:{line}: {cls}Func::MarshallAndDispatch returns false - "
                f"that reports the call as failed and leaves the script with the return type's "
                f"default; a None argument is an answer the native should give"
            )

    print(f"note: {len(declared)} native declaration(s) found in {len(SCRIPT_DIRS)} first-party script dir(s)")
    print(f"note: {len(registered)} registration(s) found across {len(cpp_sources())} C++ source(s)")
    print(f"note: {len(arity)} native function class(es) state a parameter count: {', '.join(sorted(arity)) or 'none'}")
    print(f"note: {len(bodies)} MarshallAndDispatch body(ies) inspected for a result that is never written")
    for note in notes:
        print(f"note: {note}")

    if len(declared) < MIN_DECLARATIONS:
        problems.append(
            f"only {len(declared)} native declaration(s) found, expected at least {MIN_DECLARATIONS} - "
            f"a scan that finds nothing reports its own reach, not the contract's state"
        )

    if problems:
        print(f"\nPAPYRUS NATIVE CHECK FAILED ({len(problems)})")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print("PAPYRUS NATIVE CHECK OK / every declared native is registered under its own name")
    return 0


if __name__ == "__main__":
    sys.exit(main())
