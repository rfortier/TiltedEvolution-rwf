#!/usr/bin/env python3
"""Keep the overlay's three-layer bridge agreeing with itself.

A UI button reaches the game through three separately written lists:

  1. the typings (Code/skyrim_ui/src/typings.d.ts) declare what the overlay object
     has, and client.service.ts calls it as skyrimtogether.<name>(...);
  2. ProcessHandler::OnContextCreated (Code/tp_process/ProcessHandler.cpp)
     registers the JS function names; a name that is not registered never leaves
     the render process, so the call throws in the overlay and nothing else
     happens - no warning, no log, no partial action;
  3. OverlayClient (Code/client/Services/Generic/OverlayClient.cpp) dispatches
     those names to their implementations; a registered name with no dispatch
     arrives at the game and is silently dropped by the if-chain.

Nothing compared the three, and PITFALLS section 0 has the rule as a checklist
item ("a new JS to C++ call has to be registered in ProcessHandler"), which is
how the play guide shipped with its dispatch and its ShellExecuteW already
written, its button already wired, its strings already translated into fourteen
languages - and its registration missing, so the button threw. The reconnect
call went the other way: declared, called, mocked, and implemented in neither C++
layer.

    python Tools/Scripts/check_overlay_bridge.py          # the gate
    python Tools/Scripts/check_overlay_bridge.py --list   # show what is checked
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

TYPINGS = 'Code/skyrim_ui/src/typings.d.ts'
UI_SRC = ROOT / 'Code' / 'skyrim_ui' / 'src'
MOCK_DIR = UI_SRC / 'app' / 'mock'
PROCESS_HANDLER = 'Code/tp_process/ProcessHandler.cpp'
OVERLAY_CLIENT = 'Code/client/Services/Generic/OverlayClient.cpp'

# Methods the render process serves itself. `on`/`off` are the overlay framework's
# event plumbing (m_pEventsHandler), so they are registered but have no branch in
# the game-side dispatch - they never cross the process boundary.
RENDER_PROCESS_ONLY = {
    'on': 'the framework event handler subscribes inside the render process',
    'off': 'the framework event handler unsubscribes inside the render process',
}

REGISTRATION = re.compile(r'SetValue\(\s*"(\w+)"\s*,\s*CefV8Value::CreateFunction')
INTERFACE = re.compile(r'^interface\s+SkyrimTogether\s*\{', re.M)
TYPED_METHOD = re.compile(r'^\s{2}(\w+)\s*\(', re.M)
DISPATCH = re.compile(r'eventName\s*==\s*"(\w+)"')
UI_CALL = re.compile(r'skyrimtogether\.(\w+)\s*\(')
UI_LISTEN = re.compile(r"skyrimtogether\.on\(\s*'([\w-]+)'")
CPP_EMIT = re.compile(r'(?:ExecuteAsync|EmitEvent)\(\s*"([\w-]+)"')

# Events the UI subscribes to that no C++ code emits, with the reason each one is
# allowed to stay that way. The dangerous case is a *rename*: the game side stops
# emitting a name and the UI's handler simply never runs again - no error, no log,
# and (in the browser) the mock keeps emitting whatever it likes, so it looks fine.
#
# A listener whose state nothing can change is a claim that a behaviour exists.
# openingMenu was exactly that, and it was resolved in this round by deletion, not
# by exemption: root.component.html wrapped the overlay in
# `*ngIf="!(menuOpen$ | async)"`, so the overlay was meant to disappear while a game
# menu was open - and the subject started false and could never become true, because
# nothing emitted the event (not the client, not tp_process, not even the mock).
# SetUIVisible(false) is called from two places, neither of them a menu hook
# (OverlayClient's revealPlayers and deactivate handlers), while UI::GetMenuOpen
# exists and is used only by DebugService. So the guard and the whole dead channel
# went with it - the listener and subject in client.service.ts, the typings entry,
# and the exemption that used to sit in this dict. Hiding the overlay in menus is
# still a product decision and still unimplemented; docs/FOLLOW-UP.md records it.
# It is deliberately absent below: the last rule of the event direction now fails if
# an exemption covers a name no UI code subscribes to, so re-adding the listener
# without an emitter must fail rather than pass quietly.
NO_EMITTER: dict[str, str] = {
    'setName': 'deliberate: "not wanted, we dont sync name changes" at the listener',
    'debug': 'deliberate: "not needed anymore" at the listener; debugData replaced it',
    'dummyData': 'debug-only: onDummyData pokes the F2 debug refresh, its data loop is commented out',
    'protocolMismatch': 'superseded: a protocol mismatch arrives as triggerError with wrong_version '
                        '(TransportService.cpp builds that payload), which the error panel shows',
}


def read(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        return ''
    return path.read_text(encoding='utf-8', errors='ignore')


def typed_methods() -> set[str]:
    text = read(TYPINGS)
    start = INTERFACE.search(text)
    if not start:
        return set()
    end = text.find('\n}', start.end())
    body = text[start.end():end if end > 0 else len(text)]
    return set(TYPED_METHOD.findall(body))


def registered() -> set[str]:
    return set(REGISTRATION.findall(read(PROCESS_HANDLER)))


def dispatched() -> set[str]:
    return set(DISPATCH.findall(read(OVERLAY_CLIENT)))


def ui_calls() -> dict[str, str]:
    """name -> first file:line that calls it, excluding the mock."""
    found: dict[str, str] = {}
    for path in sorted(UI_SRC.rglob('*')):
        if path.suffix not in ('.ts', '.html') or MOCK_DIR in path.parents:
            continue
        text = path.read_text(encoding='utf-8', errors='ignore')
        for match in UI_CALL.finditer(text):
            found.setdefault(match.group(1), f'{path.relative_to(ROOT).as_posix()}:{text[:match.start()].count(chr(10)) + 1}')
    return found


def ui_listeners() -> dict[str, str]:
    """Every event the UI subscribes to, from the UI sources only (never the mock)."""
    found: dict[str, str] = {}
    for path in sorted(UI_SRC.rglob('*')):
        if path.suffix not in ('.ts', '.html') or MOCK_DIR in path.parents:
            continue
        text = path.read_text(encoding='utf-8', errors='ignore')
        for match in UI_LISTEN.finditer(text):
            found.setdefault(match.group(1),
                             f'{path.relative_to(ROOT).as_posix()}:{text[:match.start()].count(chr(10)) + 1}')
    return found


def cpp_emitters() -> set[str]:
    """Every event name the game side pushes to the overlay."""
    found: set[str] = set()
    for base in ('client', 'tp_process'):
        directory = ROOT / 'Code' / base
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob('*')):
            if path.suffix not in ('.cpp', '.h', '.hpp'):
                continue
            for match in CPP_EMIT.finditer(path.read_text(encoding='utf-8', errors='ignore')):
                found.add(match.group(1))
    return found


def check(failures: list[str], notes: list[str]) -> int:
    checks = 0

    handler = read(PROCESS_HANDLER)
    client = read(OVERLAY_CLIENT)
    if not handler or not client:
        failures.append(
            f'{PROCESS_HANDLER} or {OVERLAY_CLIENT} is missing; the bridge cannot be checked and an '
            f'empty scan is not a passing one')
        return 0

    registered_names = registered()
    dispatched_names = dispatched()
    if not registered_names:
        failures.append(f'{PROCESS_HANDLER}: no SetValue registration was found; the pattern moved')
        return 0
    if not dispatched_names:
        failures.append(f'{OVERLAY_CLIENT}: no eventName dispatch was found; the pattern moved')
        return 0

    # 1. Declared means registered: the typings describe the object the UI holds.
    typed = typed_methods()
    if not typed:
        failures.append(f'{TYPINGS}: the SkyrimTogether interface could not be read, so the declared half '
                        f'of this contract is not being checked')
    else:
        for name in sorted(typed - registered_names):
            failures.append(
                f'{TYPINGS} declares {name}() but {PROCESS_HANDLER} never registers it; the overlay '
                f'object does not have that method, so calling it throws inside the browser')

    # 2. Called means registered, wherever the call lives.
    for name, where in sorted(ui_calls().items()):
        if name not in registered_names:
            failures.append(
                f'{where} calls skyrimtogether.{name}(), which {PROCESS_HANDLER} never registers; the '
                f'call throws in the overlay and the feature does nothing')
        else:
            checks += 1

    # 3. A call the UI makes must be dispatched, or it crosses the process
    #    boundary and is dropped by the if-chain. A name nothing calls cannot be
    #    dropped silently - nothing sends it - so an unused channel is reported
    #    rather than failed, and deactivate is exactly that today.
    called = ui_calls()
    for name in sorted(set(called) & registered_names):
        if name in dispatched_names or name in RENDER_PROCESS_ONLY:
            checks += 1
            continue
        failures.append(
            f'{called[name]} calls skyrimtogether.{name}(), which {PROCESS_HANDLER} registers, but '
            f'{OVERLAY_CLIENT} has no "eventName == \\"{name}\\"" branch; the overlay forwards it and the '
            f'game side silently ignores it')

    # 4. The mirror of rule 2: a branch for a name the browser cannot send.
    for name in sorted(dispatched_names - registered_names):
        failures.append(
            f'{OVERLAY_CLIENT} dispatches "{name}", but {PROCESS_HANDLER} never registers it, so no UI '
            f'code can reach that branch')

    # Reported: channels that exist but are not used from the UI side.
    unused_registered = sorted(registered_names - set(called) - typed)
    if unused_registered:
        notes.append(f'{PROCESS_HANDLER} registers {unused_registered}, which neither the typings declare '
                     f'nor the UI calls')
    unused_declared = sorted((typed & registered_names) - set(called))
    if unused_declared:
        notes.append(f'{unused_declared} are declared and registered but nothing in the UI calls them; '
                     f'their game-side branch is not required until something does')

    # 5. The other direction of the bridge: events. A handler the UI subscribes to
    #    has to have an emitter, or the state it drives can never change.
    listeners = ui_listeners()
    emitters = cpp_emitters()
    if not listeners or not emitters:
        failures.append(
            f'the event direction could not be read ({len(listeners)} listener(s), {len(emitters)} '
            f'emitter(s)); the subscription or the emit helper changed shape')
    else:
        for name, where in sorted(listeners.items()):
            if name in emitters:
                checks += 1
                continue
            if name in NO_EMITTER:
                notes.append(f'{name} is listened for at {where} and never emitted - {NO_EMITTER[name]}')
                continue
            failures.append(
                f'{where} subscribes to "{name}", which no C++ code emits; the handler never runs and '
                f'the state it drives can never change. Either emit it or record why it is dead in '
                f'NO_EMITTER')
        for name in sorted(set(NO_EMITTER) - set(listeners)):
            failures.append(
                f'NO_EMITTER exempts "{name}", but no UI code subscribes to it; drop the exemption '
                f'rather than leaving it to cover nothing')

    return checks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    if args.list:
        print('checked:')
        print(f'  every method {TYPINGS} declares is registered in {PROCESS_HANDLER}')
        print(f'  every skyrimtogether.<name>() call in the UI is registered in {PROCESS_HANDLER}')
        print(f'  every registered name has a dispatch branch in {OVERLAY_CLIENT} (except {sorted(RENDER_PROCESS_ONLY)})')
        print(f'  registered names: {sorted(registered())}')
        print(f'  dispatched names: {sorted(dispatched())}')
        print(f'  declared names  : {sorted(typed_methods())}')
        return 0

    failures: list[str] = []
    notes: list[str] = []
    checks = check(failures, notes)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'OVERLAY BRIDGE CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('OVERLAY BRIDGE CHECK OK')
    print(f'  {checks} call(s) verified through all three layers: declared, registered, dispatched')
    return 0


if __name__ == '__main__':
    sys.exit(main())
