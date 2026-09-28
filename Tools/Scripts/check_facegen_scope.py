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
wire, and leave it alone for everyone else. Four things keep that true, and all
four are cheap to break silently:

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


def body_of(source: str, signature: str) -> str:
    """The brace-matched body of the first definition of a function."""
    start = source.find(signature)
    if start < 0:
        return ''
    brace = source.find('{', start)
    if brace < 0:
        return ''
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[brace:i + 1]
    return ''


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

# 5. The window has to run every frame, or it never closes.
update = body_of(service, 'void CharacterService::OnUpdate')
if 'UpdateRemoteFaceGenWindow()' not in update:
    failures.append(
        'CharacterService::OnUpdate does not call UpdateRemoteFaceGenWindow(), so '
        'the window is opened once and never closed.'
    )

header = strip_comments(read(HEADER))
for declaration in ('void UpdateRemoteFaceGenWindow() noexcept;', 'void SetRemoteFaceGenForce(bool aForce) const noexcept;'):
    if declaration not in header:
        failures.append(f'{HEADER} does not declare {declaration}')

print(f'facegen scope: {len(writers)} writer(s) of {SETTING} (all restoring), 2 spawn path(s), 1 frame hook')

if failures:
    print()
    print(f'FACEGEN SCOPE CHECK FAILED ({len(failures)})')
    for failure in failures:
        print('  -', failure)
    sys.exit(1)

print()
print('FACEGEN SCOPE CHECK OK')
print('  the preprocessed-head flag is cleared only for wire-built actors, and restored')
