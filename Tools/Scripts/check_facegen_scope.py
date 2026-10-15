#!/usr/bin/env python3
"""Keep the preprocessed-head flag forced only where a wire-built actor needs it.

bUseFaceGenPreprocessedHeads tells the engine to load an actor's head from the
baked FaceGenData mesh instead of generating one at runtime. It is on by default,
and upstream wants it on: tiltedphoques/TiltedEvolution#764 removed the code that
cleared it globally every frame, because leaving it on fixed neck seams, black
faces and mismatched heads for ordinary characters.

That revert is what makes a custom race's head disappear. A remote player's
appearance arrives as a serialised buffer and is rebuilt on this client, so the
face it describes was never generated here and no baked mesh matches it. With the
flag on, the engine looks for FaceGenData/FaceGeom/<plugin>/<formid>.nif, finds
nothing, and the head renders empty. A custom race hits this hardest because its
head only ever exists as the race's own .nif/.tri, never as FaceGenData, which is
what "custom races - heads are invisible" reports.

So this fork has to clear the flag for exactly the actors whose face came off the
wire, and leave it alone for everyone else. Seven things keep that true, and all
seven are cheap to break silently:

  1. The global clear stays gone. Re-adding it in TiltedOnlineApp::Update brings
     back the neck seams and black faces #764 fixed, and nothing else here would
     notice.
  2. The cleared value is restored from a saved copy, never assumed. Writing a
     literal back would overwrite a player's own ini choice, and this client does
     not own that setting.
  3. The window is derived from the waiting-for-3D actors every frame, so it
     closes by itself instead of relying on a matching close call that a
     disconnect or a failed spawn can skip.
  4. The window opens before the engine is handed the actor, because the body is
     built on a later tick.
  5. The engine is asked for the pass that reads the flag, while the window is
     still open. Clearing the flag on its own changes nothing: the value is read
     inside the engine's facegen pass, and nothing in this client ever requested
     one. That is the difference between "the flag says runtime" and "a runtime
     head exists".
  6. That window is evaluated every frame, and before the pass is requested. A
     window that is never closed leaves the flag cleared for the rest of the
     session, which is the client-wide force #764 removed wearing this function's
     name.
  7. The three offsets the setting is reached through are asserted where they are
     used, and the two lines that can settle them from a user log stay at a level
     a shipped build prints. Those offsets were measured on SkyrimSE.exe 1.5.97 and
     never on the 1.6.x/1.7.x runtimes, so the asserts only pin this repository's
     own layout - a user report is the only thing that can answer for the others,
     and a debug line never reaches one.

    python Tools/Scripts/check_facegen_scope.py          # the gate
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SETTING = 'bUseFaceGenPreprocessedHeads'
SERVICE = 'Code/client/Services/Generic/CharacterService.cpp'
HEADER = 'Code/client/Services/CharacterService.h'
APP = 'Code/client/TiltedOnlineApp.cpp'
TES = 'Code/client/Games/TES.h'

failures: list[str] = []


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding='utf-8', errors='ignore')


def strip_comments(text: str) -> str:
    """Drop // and /* */ comments, so a retired call site cannot satisfy a check.

    TiltedOnlineApp.cpp is the reason this exists: the file keeps a long comment
    explaining why the global clear was removed, and a check that read the raw
    text would find the setting named there and conclude the clear is still live.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def block_of(source: str, brace: int) -> str:
    """The brace-matched block that opens at brace."""
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[brace:i + 1]
    return ''


def body_of(source: str, signature: str) -> str:
    """The brace-matched body following the first occurrence of a signature."""
    start = source.find(signature)
    if start < 0:
        return ''
    brace = source.find('{', start)
    if brace < 0:
        return ''
    return block_of(source, brace)


def enclosing_brace(source: str, index: int) -> int:
    """The opening brace of the innermost block that contains index, or -1."""
    stack: list[int] = []
    for i in range(index):
        if source[i] == '{':
            stack.append(i)
        elif source[i] == '}' and stack:
            stack.pop()
    return stack[-1] if stack else -1


def conditional_at(source: str, index: int) -> bool:
    """True when the call at index only runs under an if.

    The first-write-only property is what keeps the info line to one entry per
    session, so an unconditional call has to fail here even though it names the
    setting just as well.
    """
    opener = enclosing_brace(source, index)
    if opener >= 0:
        boundary = max(source.rfind(';', 0, opener), source.rfind('{', 0, opener), source.rfind('}', 0, opener))
        head = source[boundary + 1:opener].strip()
        if re.fullmatch(r'(else\s+)?if\s*\(.*\)', head, re.S) or head == 'else':
            return True
    boundary = max(source.rfind(';', 0, index), source.rfind('{', 0, index), source.rfind('}', 0, index))
    return bool(re.fullmatch(r'if\s*\(.*\)', source[boundary + 1:index].strip(), re.S))


# 1. The global clear must not come back. #764 removed it for the characters that
#    do have baked assets, and those are the majority of a session.
app = strip_comments(read(APP))
if SETTING in app:
    failures.append(
        f'{APP} names {SETTING} in live code. Clearing it globally is what upstream '
        f'#764 reverted: it is the fix for neck seams and black faces on ordinary '
        f'characters. It may only be cleared for a wire-built actor, from '
        f'CharacterService::SetRemoteFaceGenForce.'
    )

# 2. No writer may leave the flag at 0. Clearing it is allowed only for as long as
#    the actor that needs it is being built, so every writer has to save the value
#    it found and put that value back. Actor::QueueUpdate is the precedent this
#    exists for: it is a correct save-and-restore, and it must stay one.
writers = sorted(
    str(p.relative_to(ROOT)).replace('\\', '/')
    for p in (ROOT / 'Code' / 'client').rglob('*')
    if p.suffix in ('.cpp', '.h', '.hpp') and SETTING in strip_comments(read(str(p.relative_to(ROOT))))
)
if SERVICE not in writers:
    failures.append(
        f'{SERVICE} no longer touches {SETTING}, so nothing clears it for a '
        f'wire-built actor and a custom race goes back to an empty head.'
    )

for rel in writers:
    if rel == SERVICE:
        continue  # checked against its own shape just below
    text = strip_comments(read(rel))
    saved = sorted(set(re.findall(r'(\w+)\s*=\s*pSetting->data', text)))
    if not saved:
        failures.append(
            f'{rel} clears {SETTING} without saving the value it found, so the '
            f"player's own setting is lost for the rest of the session."
        )
        continue
    for name in saved:
        if not re.search(r'pSetting->data\s*=\s*' + re.escape(name) + r'\b', text):
            failures.append(
                f'{rel} saves {SETTING} into {name} but never restores it, so the '
                f'flag stays cleared after this site runs.'
            )

# 3. The one writer must restore a saved copy rather than a literal, and must keep
#    the window derived from the waiting actors.
service = strip_comments(read(SERVICE))
force = body_of(service, 'void CharacterService::SetRemoteFaceGenForce')
if not force:
    failures.append(
        f'{SERVICE} no longer defines SetRemoteFaceGenForce, so nothing asks the '
        f'engine for a runtime head and a custom race goes back to an empty one.'
    )
else:
    if not re.search(r'm_faceGenPreprocessedBackup\s*=\s*pSetting->data', force):
        failures.append(
            'SetRemoteFaceGenForce clears the flag without first saving the value '
            'it found, so it cannot restore the player\'s own setting.'
        )
    if not re.search(r'pSetting->data\s*=\s*aForce\s*\?\s*0\s*:\s*m_faceGenPreprocessedBackup', force):
        failures.append(
            'SetRemoteFaceGenForce no longer restores m_faceGenPreprocessedBackup '
            'when the window closes, which leaves the flag cleared for the rest of '
            'the session.'
        )
    if not re.search(r'if\s*\(!\s*pSetting\s*\)', force):
        failures.append(
            'SetRemoteFaceGenForce dereferences the ini setting without checking '
            'the lookup first; GetSetting answers null for a setting the game does '
            'not define.'
        )

window = body_of(service, 'void CharacterService::UpdateRemoteFaceGenWindow')
if not window:
    failures.append(
        f'{SERVICE} no longer defines UpdateRemoteFaceGenWindow, so the window is '
        f'never closed and the flag stays cleared.'
    )
else:
    missing = [name for name in ('WaitingFor3D', 'RemoteComponent')
               if name not in window]
    # The comparison itself, not just the tokens: an inverted or dropped test on the
    # FormId is how the window silently widens to every waiting actor, which is the
    # client-wide force upstream #764 removed, wearing this function's name.
    if not re.search(r'SpawnRequest\.FormId\s*==\s*GameId\s*\{\s*\}', window):
        missing.append('SpawnRequest.FormId == GameId{}')
    if missing:
        failures.append(
            'UpdateRemoteFaceGenWindow does not scope the window to the actors whose '
            'appearance arrived over the wire; ' + ', '.join(missing) +
            ' is missing or not the comparison it has to be. WaitingFor3D is the body '
            'wait, an empty FormId is the wire-built base, and RemoteComponent is what '
            'a disconnect clears - without all three the window is either always open '
            'or stays open past the session.'
        )

# 4. The window opens before the engine builds the body, in both spawn paths.
for signature in (
    'void CharacterService::OnCharacterSpawn',
    'Actor* CharacterService::CreateCharacterForEntity',
):
    definition = body_of(service, signature)
    if not definition:
        failures.append(f'{SERVICE} no longer defines {signature.split("::")[1]}().')
        continue
    opened = definition.find('SetRemoteFaceGenForce(true)')
    built = definition.find('Actor::Create(')
    if opened < 0:
        fail = ('never opens the facegen window, so the actor it creates is '
                'built with the preprocessed head the engine cannot find')
    elif built < 0:
        fail = 'no longer creates the actor, so this check cannot place the window'
    elif opened > built:
        fail = ('opens the facegen window after Actor::Create, but the engine '
                'builds the body on a later tick and reads the flag then')
    else:
        continue
    failures.append(f'{signature.split("::")[1]}() {fail}.')

# 5. Clearing the flag is not enough by itself. The value is read inside the
#    engine's own facegen pass, so the pass has to be asked for - and asked for
#    while the window is still open. RunRemoteUpdates is the one place both spawn
#    paths funnel through: the body has just appeared, and WaitingFor3D has not
#    been dropped yet, so the window derived from it is still open on this tick.
run_remote = body_of(service, 'void CharacterService::RunRemoteUpdates')
if not run_remote:
    failures.append(f'{SERVICE} no longer defines RunRemoteUpdates(), so the '
                    f'facegen pass is never requested for a wire-built actor.')
else:
    asked = run_remote.find('QueueUpdate()')
    if asked < 0:
        failures.append(
            'RunRemoteUpdates no longer asks the engine for a facegen pass. The '
            'flag only changes what that pass decides; with nothing requesting it '
            'the head is built on the engine\'s own schedule, which is the empty '
            'head this fix exists for.'
        )
    else:
        # Same scoping as the window: a base resolved from the local load order
        # has a baked head and must keep it.
        if not re.search(r'SpawnRequest\.FormId\s*==\s*GameId\s*\{\s*\}', run_remote[:asked]):
            failures.append(
                'RunRemoteUpdates requests a facegen pass without scoping it to the '
                'actors whose appearance arrived over the wire, so every actor it '
                'finishes waiting on is put back on runtime generation.'
            )
        dropped = run_remote.find('remove<WaitingFor3D>')
        if dropped >= 0 and asked > dropped:
            failures.append(
                'RunRemoteUpdates requests the facegen pass after dropping '
                'WaitingFor3D, but that component is what keeps the window open - '
                'the pass then runs with the flag already restored.'
            )

# 6. The window has to run every frame, or it never closes.
update = body_of(service, 'void CharacterService::OnUpdate')
if 'UpdateRemoteFaceGenWindow()' not in update:
    failures.append(
        'CharacterService::OnUpdate does not call UpdateRemoteFaceGenWindow(), so '
        'the window is opened once and never closed.'
    )
# ...and it has to run before the pass is requested, or the request goes out with
# the flag already restored from the previous tick.
if 'UpdateRemoteFaceGenWindow()' in update and 'RunRemoteUpdates()' in update:
    if update.find('UpdateRemoteFaceGenWindow()') > update.find('RunRemoteUpdates()'):
        failures.append(
            'CharacterService::OnUpdate requests the facegen pass before it evaluates '
            'the window, so the pass runs with the flag from the previous tick.'
        )

# 7. The three offsets the walk depends on were measured on SkyrimSE.exe 1.5.97 and
#    never on the 1.6.x/1.7.x runtimes. Nothing here can verify them - the machine
#    that builds this has no other executable - so what this section keeps alive is
#    the record and the two lines that let a user settle it. A wrong offset does not
#    crash: GetSetting answers null and the flag is simply never cleared, so without
#    the info line "not fixed on AE" and "wrong offsets on AE" look the same.
tes = strip_comments(read(TES))
for struct, fields in (
    ('Setting', ('data', 'name')),
    ('INISettingCollection', ('head',)),
):
    definition = body_of(tes, f'struct {struct}')
    if not definition:
        failures.append(
            f'{TES} no longer defines struct {struct}, so the ini walk has no layout '
            f'to assert and the offsets below it are unchecked.'
        )
        continue
    for field in fields:
        if not re.search(r'\b' + field + r'\s*(\[[^\]]*\])?\s*;', definition):
            failures.append(
                f'{TES} struct {struct} no longer declares {field}, so the offset this '
                f'fork walks the engine setting through is gone.'
            )

for offset, field in ((r'0x8', 'Setting::data'), (r'0x10', 'Setting::name'), (r'0x118', 'INISettingCollection::head')):
    owner, member = field.split('::')
    if not re.search(r'static_assert\s*\(\s*offsetof\s*\(\s*' + owner + r'\s*,\s*' + member +
                     r'\s*\)\s*==\s*' + offset + r'\s*\)', tes):
        failures.append(
            f'{TES} does not assert offsetof({field}) == {offset}. That number was '
            f'measured on SkyrimSE.exe 1.5.97 only and is unverified on 1.6.x/1.7.x; '
            f'without the assert the next reader cannot tell it is an assumption at all.'
        )

if force:
    info = force.find('spdlog::info(')
    if info < 0 or SETTING not in force[info:info + 400]:
        failures.append(
            'SetRemoteFaceGenForce does not log the setting it forces at info level. '
            'Debug is invisible in a shipped build (TiltedOnlineApp leaves '
            'set_level(debug) commented out), so the one line that can tell a user '
            'whether the 1.5.97-only offsets resolved on their runtime never appears.'
        )
    elif not conditional_at(force, info):
        failures.append(
            'SetRemoteFaceGenForce logs the forced setting on every call instead of '
            'only the first write, so the line that should appear once per session '
            'floods the log and stops being readable.'
        )
    debug = force.find('spdlog::debug(')
    if debug < 0 or SETTING not in force[debug:debug + 400]:
        failures.append(
            'SetRemoteFaceGenForce no longer keeps the per-call debug line for the '
            'setting, so the value each individual write saw is no longer recorded.'
        )
    lookup = re.search(r'if\s*\(\s*!\s*pSetting\s*\)', force)
    if lookup:
        block = block_of(force, force.find('{', lookup.end()))
        if 'spdlog::warn(' not in block:
            failures.append(
                'SetRemoteFaceGenForce returns without warning when the setting is not '
                'found. GetSetting answers null both for a setting the game does not '
                'define and for a runtime whose INISettingCollection is not the layout '
                'TES.h asserts, so a silent return leaves a wrong offset looking like '
                'a missing setting.'
            )
        if not re.search(r'\breturn\s*;', block):
            failures.append(
                'The not-found path in SetRemoteFaceGenForce no longer returns, so it '
                'falls through and writes through a null pSetting.'
            )

header = strip_comments(read(HEADER))
for declaration in ('void UpdateRemoteFaceGenWindow() noexcept;', 'void SetRemoteFaceGenForce(bool aForce) const noexcept;'):
    if declaration not in header:
        failures.append(f'{HEADER} does not declare {declaration}')

print(f'facegen scope: {len(writers)} writer(s) of {SETTING} (all restoring), 2 spawn path(s), '
      f'1 frame hook, 1 pass request, 3 asserted offset(s), 1 info first-write log')

if failures:
    print()
    print(f'FACEGEN SCOPE CHECK FAILED ({len(failures)})')
    for failure in failures:
        print('  -', failure)
    sys.exit(1)

print()
print('FACEGEN SCOPE CHECK OK')
print('  the preprocessed-head flag is cleared only for wire-built actors, restored')
print('  from a saved copy, and the facegen pass is requested while it is still down')
