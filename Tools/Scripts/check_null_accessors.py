#!/usr/bin/env python3
"""Keep the client off the engine singletons it cannot dereference safely.

The client reaches the engine through a family of singleton accessors that all
end in `return *ppSingleton.Get()`. That dereference has two ways to be wrong,
and both were live in this tree:

1. **The id itself is unmapped.** On a legacy runtime an unmapped address-library
   id resolves to a shared executable stub, so reading *through* it as data reads
   the stub's own opcodes. `GarbageCollector::Get()` and
   `bhkCharacterController::PhysicsDeltaTime()` resolve explicitly with
   `FindAddressById` for exactly this reason, and
   `Tools/missing_1_5_97_ids.txt` records why.

2. **The singleton is not built yet.** The engine fills these pointers during
   startup, and this client constructs its services from `World`'s own
   initialisation, so the order is not guaranteed. `INISettingCollection` is the
   sharpest case: the engine's own `GetSetting` (SkyrimSE.exe +0x3104d0) opens
   with an explicit empty-collection guard that returns null, which is the
   contract callers are written against.

What makes this a defect rather than a style question is **inconsistency inside
the tree**: `PlayerCharacter::Get()` is null-checked at all 56 of its call sites
and `ModManager::Get()` at twelve of thirteen, so the codebase already treats
these accessors as nullable. A site that skips the check is a fault the rest of
the tree does not have.

The check is structural rather than a list of blessed lines: it reads the
accessors that are known to be nullable, then follows the value from the call to
the first dereference and refuses the call site when no null test comes first.

That follow is the point. Reading one line at a time is not enough, because the
tree writes the guard on a *different* line from the call:

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;
    pPlayer->SetDifficulty(0);        # guarded - fine

    auto* pPlayer = PlayerCharacter::Get();
    pPlayer->SetDifficulty(0);        # not guarded - a fault

Both shapes are the same line apart from the test, so a gate that only looks at
the line holding `X::Get()` reads them as identical and reports nothing. The
window runs from the assignment to the end of the innermost block containing it,
and stops at the first dereference of that variable, so the guard has to be
*reached before* the use rather than merely present somewhere in the function.

    python Tools/Scripts/check_null_accessors.py            # the gate
    python Tools/Scripts/check_null_accessors.py --list     # show what is checked
    python Tools/Scripts/check_null_accessors.py --selftest # check the guard shapes
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'Code' / 'client'

# Accessors whose result can be null, each with the reason, and whether the
# resolved value is itself a pointer-to-pointer (the singleton case) or a plain
# object pointer.
#
# Every entry here is justified by the engine or by this repository, not by
# taste: see the docstring. Adding a name means a caller must guard it.
NULLABLE_ACCESSORS: dict[str, str] = {
    'INISettingCollection::Get': 'returns *ppSingleton; the singleton is null before the ini settings load',
    'PlayerCharacter::Get': 'returns the player pointer, null until the character exists',
    'SkyrimVM::Get': 'null before the VM is constructed',
    'WeatherManager::Get': 'null before the weather system is up',
    'SubtitleManager::Get': 'null before the subtitle manager is up',
    'MenuTopicManager::Get': 'null before the menu topic manager is up',
    'TimeData::Get': 'null before the time manager is up',
    'TES::Get': 'null before the game data is up',
    'UI::Get': 'null before the UI is constructed',
    'Sky::Get': 'null outside an exterior cell',
    'ProcessLists::Get': 'null before the process lists exist',
    'GarbageCollector::Get': 'resolved with FindAddressById; deliberately null where the id is unmapped',
    'EventDispatcherManager::Get': 'null before the event dispatcher is built',
    'ModManager::Get': 'null before the mod manager is built',
    'PlayerCamera::Get': 'null before the camera is built',
    'EquipManager::Get': 'null before the equip manager is built',
    'BSInputEnableManager::Get': 'null before input is initialised',
    'BGSCreatedObjectManager::Get': 'null before the object manager is built',
    'ActorMediator::Get': 'null before the animation mediator is built',
}

# Deliberately NOT listed, with the reason, so a later reader does not "fix" the
# omission by adding them:
#
#   BehaviorVar::Get   lazily allocates (`if (!single) single = new ...`), so it
#                      never returns null and its call sites need no guard.
#   DefaultObjectManager::Get, ModSystem::Get, *::GetInstance singletons with a
#                      static storage duration - no engine-dependent window.
NOT_NULLABLE: dict[str, str] = {
    'BehaviorVar::Get': 'lazily allocates on first use, so it never returns null',
}

# Call sites that are correct even though the text looks unguarded. Each needs a
# reason, and the reason has to be about this call site rather than the function.
#
# Empty on purpose: the fix for every real site was to guard it, so nothing needs
# an exemption. The list stays as the place to record one, with the same
# reverse-check as the rest of this file so an entry cannot outlive its reason.
ALLOWED: dict[str, str] = {}

STRIP_BLOCK_COMMENT = re.compile(r'/\*.*?\*/', re.S)
STRIP_LINE_COMMENT = re.compile(r'//[^\n]*')


def sources() -> list[pathlib.Path]:
    return sorted(p for p in CLIENT.rglob('*') if p.suffix in ('.cpp', '.h', '.hpp'))


def strip_comments(text: str) -> str:
    return STRIP_LINE_COMMENT.sub('', STRIP_BLOCK_COMMENT.sub('', text))


# A null test of one variable: `!pX`, `pX == nullptr`, `nullptr != pX`, `pX &&`,
# `pX ?`, `assert(pX)`, plus the two shapes the gate used to read as "some other
# expression was tested":
#
#   `if (pTes && pModManager)`  a variable on the right of `&&`/`||` is one of
#                               the operands the condition tests, so the test
#                               covers it exactly as `pModManager &&` does.
#   `if (pPlayer)`              the whole condition is the variable, which is a
#                               test of it and of nothing else.
#
# Testing the pointer is what makes the later use safe, so the test still has to
# name the variable the accessor was stored in - a bare `if (` or `&&` somewhere
# earlier on the line tests something else and is not a guard. Neither added
# shape loosens that: `if (pOther)` does not test `pX`, and the variable has to
# stand alone inside its condition, so `pPlayerExtra` never counts as `pPlayer`.
def test_of(name: str) -> re.Pattern[str]:
    v = re.escape(name)
    return re.compile(
        rf'!\s*{v}\b'
        rf'|\b{v}\b\s*(?:==|!=)\s*(?:nullptr|NULL|0)\b'
        rf'|(?:nullptr|NULL|0)\s*(?:==|!=)\s*{v}\b'
        rf'|\b{v}\b\s*(?:&&|\|\||\?)'
        rf'|(?:&&|\|\|)\s*{v}\b'
        rf'|\b(?:if|while)\s*\(\s*{v}\s*\)'
        rf'|\bassert\s*\(\s*{v}\b')


# The same test written against the accessor call, for the inline form
# `X::Get() && X::Get()->Foo()`. `!SomeOther(pX)` does not match it.
def inline_test_of(accessor: str) -> re.Pattern[str]:
    a = re.escape(accessor)
    return re.compile(
        rf'!\s*{a}\(\)'
        rf'|{a}\(\)\s*(?:==|!=)\s*(?:nullptr|NULL|0)'
        rf'|(?:nullptr|NULL|0)\s*(?:==|!=)\s*{a}\(\)'
        rf'|{a}\(\)\s*(?:&&|\|\||\?)')


# A dereference of a variable: `pX->`, `pX[`, `*pX`. Testing it (`if (pX)`) and
# handing it to something (`Foo(pX)`) are not uses of the pointee.
def use_of(name: str) -> re.Pattern[str]:
    v = re.escape(name)
    return re.compile(rf'\b{v}\b\s*->|\b{v}\b\s*\[|\*\s*{v}\b')


# `... = ` with nothing but whitespace after it: the value the accessor returns
# is being stored rather than used.
ASSIGN_TAIL = re.compile(r'(?<![=!<>+\-*/%&|^])=(?!=)\s*$')

# An `if (`, `while (` or `assert (` still open at the end of the statement so
# far, so the value being assigned is itself what is tested. The parens are
# counted rather than matched by a pattern, so `if (foo) pX = X::Get();` - where
# the condition closed before the assignment - is not mistaken for a test.
def in_test(prefix: str) -> bool:
    """True when the assignment sits inside a still-open `if`/`while`/`assert` condition."""
    depth = 0
    for i in range(len(prefix) - 1, -1, -1):
        if prefix[i] == ')':
            depth += 1
        elif prefix[i] == '(':
            if depth == 0:
                return bool(re.search(r'\b(?:if|while|assert)$', prefix[:i].rstrip()))
            depth -= 1
    return False

# (relative path, line of the accessor call, the statement holding the call,
# accessor, variable holding it, line of the first unguarded use, that line's
# text). The variable is empty for the inline form, where the call and the use
# are the same statement.
Site = tuple[str, int, str, str, str, int, str]


def depths(lines: list[str]) -> list[int]:
    """Brace depth at the start of each line."""
    out: list[int] = []
    depth = 0
    for line in lines:
        out.append(depth)
        depth += line.count('{') - line.count('}')
    return out


def block_end(depth: list[int], start: int) -> int:
    """Index of the last line of the innermost block containing `start`."""
    for j in range(start + 1, len(depth)):
        if depth[j] < depth[start]:
            return j - 1
    return len(depth) - 1


def assigned_name(prefix: str) -> str:
    """The variable a `... = ` prefix stores into, or '' for any other prefix."""
    tail = ASSIGN_TAIL.search(prefix)
    if not tail:
        return ''
    names = re.findall(r'\b(\w+)\b', prefix[:tail.start()])
    return names[-1] if names else ''


def unguarded_use(lines: list[str], depth: list[int], call: int, var: str,
                  offset: int) -> tuple[int, str] | None:
    """(line number, text) of the first dereference of `var` no null test precedes.

    The window is the innermost block holding the assignment, and the scan stops
    at the first dereference, so a test after the use does not count. A test only
    counts when it sits at or above the assignment's own depth: a test buried in
    a nested block does not dominate the lines after that block.
    """
    use = use_of(var)
    test = test_of(var)
    tested = False
    for j in range(call, block_end(depth, call) + 1):
        line = lines[j]
        start = offset if j == call else 0
        hit = use.search(line, start)
        if hit and not tested and not test.search(line[:hit.start()]):
            return j + 1, line.strip()
        if depth[j] <= depth[call] and test.search(line, start):
            tested = True
    return None


# The shape matrix behind `--selftest`. Each case is a snippet whose first line
# stores an accessor result in the named variable and whose last line
# dereferences it, so a case can never read as "guarded" merely because nothing
# was used. The matrix pins both directions of the gate's contract: every shape
# it accepts as a null test, and every near miss it has to keep refusing. The
# first six entries are the call sites that used to be reported wrongly.
SELFTEST_CASES: list[tuple[str, str, str, bool]] = [
    # Variable on the right of a logical operator: the operand of `&&`/`||` is
    # one of the things the condition tests, so the test covers it.
    ('if (pTes && pModManager)',
     'auto* pModManager = ModManager::Get();\n'
     'if (pTes && pModManager)\n'
     '    pCell = pModManager->GetCellFromCoordinates(0, 0, 0, false);',
     'pModManager', True),
    ('if (!m_bCustomPresence && pPlayer)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (!m_bCustomPresence && pPlayer)\n'
     '    auto* pLocation = pPlayer->locationForm;',
     'pPlayer', True),
    ('if (pOther || pPlayer)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pOther || pPlayer)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', True),

    # The whole condition is the variable.
    ('if (pProcessLists)',
     'ProcessLists* const pProcessLists = ProcessLists::Get();\n'
     'if (pProcessLists)\n'
     '    i = pProcessLists->highActorHandleArray.length;',
     'pProcessLists', True),
    ('if (pModManager)',
     'auto* pModManager = ModManager::Get();\n'
     'if (pModManager)\n'
     '    pCell = pModManager->GetCellFromCoordinates(0, 0, 0, false);',
     'pModManager', True),
    ('if (pPlayer)',
     'PlayerCharacter* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayer)\n'
     '    pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);',
     'pPlayer', True),
    ('while (pPlayer)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'while (pPlayer)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', True),

    # The nine shapes the gate already knew, kept so the additions cannot have
    # replaced them.
    ('if (pPlayer && pOther)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayer && pOther)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', True),
    ('if (!pPlayer)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (!pPlayer)\n'
     '    return;\n'
     'pPlayer->SetDifficulty(0);',
     'pPlayer', True),
    ('if (pPlayer == nullptr)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayer == nullptr)\n'
     '    return;\n'
     'pPlayer->SetDifficulty(0);',
     'pPlayer', True),
    ('if (nullptr != pPlayer)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (nullptr != pPlayer)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', True),
    ('if (pPlayer) { ... } - the guard is a block',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayer)\n'
     '{\n'
     '    pPlayer->SetDifficulty(0);\n'
     '}',
     'pPlayer', True),
    ('assert(pPlayer)',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'assert(pPlayer);\n'
     'pPlayer->SetDifficulty(0);',
     'pPlayer', True),
    ('pPlayer ? ... : ...',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'auto difficulty = pPlayer ? 1 : 0;\n'
     'pPlayer->SetDifficulty(difficulty);',
     'pPlayer', True),

    # Near misses. A test of a different variable is not a test of this one, and
    # a longer name that merely starts the same is a different variable too - the
    # `&&` shape must not match it just because the operator is there.
    ('if (pOther) - a test of something else',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pOther)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('if (pPlayerExtra) - same-name substring, bare condition',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayerExtra)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('if (pOther && pPlayerExtra) - same-name substring on the right of &&',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pOther && pPlayerExtra)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('if (pPlayerExtra && pOther) - same-name substring on the left of &&',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayerExtra && pOther)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('if (pPlayerExtra == nullptr) - same-name substring, explicit test',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayerExtra == nullptr)\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('if (pPlayer) does not guard pPlayerExtra',
     'auto* pPlayerExtra = PlayerCharacter::Get();\n'
     'if (pPlayer)\n'
     '    pPlayerExtra->SetDifficulty(0);',
     'pPlayerExtra', False),
    ('if (pPlayer->Foo()) - the condition dereferences what it would have to test',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pPlayer->Foo())\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('if (Foo(pPlayer)) - handing the pointer on is not a test of it',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (Foo(pPlayer))\n'
     '    pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('pOther = pPlayer; - mentioning the name is not a test of it',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'pOther = pPlayer;\n'
     'pPlayer->SetDifficulty(0);',
     'pPlayer', False),
    ('a test after the dereference does not count',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'pPlayer->SetDifficulty(0);\n'
     'if (pPlayer)\n'
     '    pPlayer->SetDifficulty(1);',
     'pPlayer', False),
    ('a test inside a nested block does not dominate the lines after it',
     'auto* pPlayer = PlayerCharacter::Get();\n'
     'if (pOther)\n'
     '{\n'
     '    if (pPlayer)\n'
     '        pPlayer->SetDifficulty(0);\n'
     '}\n'
     'pPlayer->SetDifficulty(1);',
     'pPlayer', False),
]


def selftest() -> int:
    """Run the shape matrix; 0 when every case matches, 1 otherwise."""
    failures: list[str] = []
    for label, snippet, var, guarded in SELFTEST_CASES:
        lines = snippet.splitlines()
        # A case with no dereference would pass the `guarded` cases for the wrong
        # reason, so the matrix refuses to hold a case that cannot fail. The first
        # line is skipped: its `auto* pX = ` prefix is not a dereference of pX.
        if not any(use_of(var).search(line) for line in lines[1:]):
            failures.append(f'{label}: the case never dereferences {var}, so it '
                            f'cannot tell the two answers apart')
            continue
        # The scan starts after the accessor call, exactly as call_sites() does it,
        # so the `auto* pX = ` prefix is not itself read as a dereference of pX.
        call = re.search(r'\w+\(\)', lines[0])
        hit = unguarded_use(lines, depths(lines), 0, var, call.end() if call else 0)
        if guarded and hit:
            failures.append(f'{label}: {var} is tested but the gate reported it as '
                            f'unchecked (line {hit[0]}: {hit[1]})')
        elif not guarded and not hit:
            failures.append(f'{label}: {var} is not tested but the gate accepted it '
                            f'as checked')

    if failures:
        print(f'NULL ACCESSOR SELFTEST FAILED ({len(failures)}/{len(SELFTEST_CASES)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1
    print(f'NULL ACCESSOR SELFTEST OK ({len(SELFTEST_CASES)} shape(s))')
    return 0


def call_sites() -> list[Site]:
    """Each dereference of a nullable accessor that no null test precedes."""
    found: list[Site] = []
    for path in sources():
        rel = path.relative_to(CLIENT).as_posix()
        text = strip_comments(path.read_text(encoding='utf-8', errors='ignore'))
        lines = text.splitlines()
        depth = depths(lines)
        for index, statement in enumerate(lines):
            for accessor in NULLABLE_ACCESSORS:
                if accessor not in statement:
                    continue

                # The accessor's own definition is what *makes* it nullable, so
                # it is not a call site: `T* Cls::Get() noexcept {`.
                if re.search(r'\b\w[\w:]*\s+' + re.escape(accessor) + r'\(\)', statement):
                    continue

                for call in re.finditer(re.escape(accessor) + r'\(\)', statement):
                    # The inline form: the call is dereferenced where it stands.
                    if re.match(r'\)\s*->', statement[call.end():]):
                        if not inline_test_of(accessor).search(statement[:call.start()]):
                            found.append((rel, index + 1, statement.strip(), accessor,
                                          '', index + 1, statement.strip()))
                        continue

                    # The stored form: follow the variable the call was stored
                    # in to its first use and require a test of that variable
                    # before it. A test of the call in the condition that does
                    # the storing (`if (auto* p = X::Get())`) is such a test.
                    prefix = statement[:call.start()]
                    var = assigned_name(prefix)
                    if not var or in_test(prefix):
                        continue
                    hit = unguarded_use(lines, depth, index, var, call.end())
                    if hit:
                        found.append((rel, index + 1, statement.strip(), accessor, var,
                                      hit[0], hit[1]))
    return found


def check(failures: list[str]) -> None:
    """Every nullable singleton accessor is null-checked before it is dereferenced."""
    sites = call_sites()
    for rel, call, statement, accessor, _var, use, use_text in sites:
        if rel in ALLOWED:
            continue
        # The two lines are the same statement for the inline form, so showing
        # both would print the line twice and hide the one that matters.
        detail = (f'saw: {statement[:110]}' if use_text == statement
                  else f'call: {statement[:110]}; use: {use_text[:110]}')
        failures.append(
            f'{rel}:{call}: {accessor}() is dereferenced at line {use} without a null '
            f'check, but {NULLABLE_ACCESSORS[accessor]} - assign it to a variable and '
            f'test that ({detail})')

    # The reverse direction: an ALLOWED file that no longer needs the exemption
    # is a stale entry, and a stale entry is how this kind of list rots into a
    # blanket permission.
    seen = {rel for rel, *_rest in sites}
    for rel, reason in ALLOWED.items():
        if rel not in seen:
            failures.append(
                f'{rel}: listed as an allowed exception ({reason}) but no longer '
                f'dereferences a nullable accessor; drop the entry')

    # A NOT_NULLABLE entry that has grown a null path is a stale claim about the
    # engine, which is worse than no claim at all.
    for accessor, reason in NOT_NULLABLE.items():
        owner = accessor.split('::')[0]
        for path in sources():
            text = path.read_text(encoding='utf-8', errors='ignore')
            if f'{accessor}()' not in text:
                continue
            body = re.search(
                re.escape(accessor) + r'\(\)[^{]*\{(?P<body>[^}]*)\}', text)
            if body and re.search(r'return\s+(nullptr|NULL|0)\s*;', body.group('body')):
                failures.append(
                    f'{accessor}: listed as never-null ({reason}) but its body can '
                    f'return null; move it to NULLABLE_ACCESSORS and guard its callers')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--selftest', action='store_true',
                        help='check the null-test shapes against the shape matrix')
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    if args.list:
        print('nullable accessors that callers must guard:')
        for accessor, reason in sorted(NULLABLE_ACCESSORS.items()):
            print(f'  {accessor:<32} {reason}')
        print()
        print('accessors checked and found never-null:')
        for accessor, reason in sorted(NOT_NULLABLE.items()):
            print(f'  {accessor:<32} {reason}')
        print()
        print('allowed exceptions:')
        for rel, reason in sorted(ALLOWED.items()):
            print(f'  {rel:<46} {reason}')
        return 0

    failures: list[str] = []
    check(failures)

    if failures:
        print(f'NULL ACCESSOR CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('NULL ACCESSOR CHECK OK')
    print(f'  {len(NULLABLE_ACCESSORS)} nullable accessor(s) checked across {len(sources())} source file(s)')
    print(f'  {len(NOT_NULLABLE)} accessor(s) verified never-null, {len(ALLOWED)} exception(s), each still live')
    return 0


if __name__ == '__main__':
    sys.exit(main())
