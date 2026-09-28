#!/usr/bin/env python3
"""Keep the OStimNG prerequisite buildable in CI.

OStimNG's CMakeLists asserts COMMONLIB_SSE_FOLDER and then add_subdirectory()s
it, so building OStimNG needs a CommonLibSSE-NG SOURCE checkout. The pinned
OStimNG used to carry that checkout as its own skse/extern/CommonLibSSE-NG
gitlink, and the CI step read it from there. OStimNG commit f20db82 ("updated
compiler") deleted the gitlink when OStimNG moved to vcpkg, so the path stopped
existing in the pinned tree and the step threw before configuring - which is
why three workflows failed with no CMake output at all.

The revision therefore lives in Code/plugins/plugins.json, beside the OStimNG
pin it has to match, and the workflow reads it from there. Two claims have to
stay true for that to keep working, and neither is visible from a local build:

  1. the manifest declares a repository and a full commit sha for it, and
  2. the workflow consumes that field instead of a hard-coded path.

The second is the one that rots. A step that went back to asserting on
plugins/OStimNG/skse/extern/CommonLibSSE-NG would pass review and fail only in
CI, which is exactly how this broke.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MANIFEST = 'Code/plugins/plugins.json'
WORKFLOW = '.github/workflows/windows.yml'

# The path OStimNG's CMakeLists reads through COMMONLIB_SSE_FOLDER. It is still
# where the checkout has to land - that part is OStimNG's contract - but nothing
# may assume the submodule checkout already put it there.
CHECKOUT = 'plugins/OStimNG/skse/extern/CommonLibSSE-NG'
FIELD = 'commonLibSseNg'
SHA = re.compile(r'^[0-9a-f]{40}$')

# The paths whose pinned vcpkg baselines the setup step collects. Kept as the
# literal pattern rather than a paraphrase, so this gate fails when the workflow
# narrows it and not when someone rewords a comment.
BASELINE_GLOBS = ("plugins/**/vcpkg.json", "plugins/**/vcpkg-configuration.json")
GLOB_LINE = re.compile(r"hashFiles\(([^)]*)\)")


def workflow_globs(workflow: str) -> list[str]:
    """Every hashFiles() pattern in the workflow's cache keys."""
    patterns: list[str] = []
    for match in GLOB_LINE.finditer(workflow):
        patterns.extend(re.findall(r"'([^']+)'", match.group(1)))
    return patterns


def glob_matches(path: str) -> bool:
    """Whether a hashFiles()-style '**' pattern covers a repo-relative path.

    GitHub's hashFiles supports '**' as any number of directories, which
    pathlib's glob does not do the same way, so the one form the workflow uses
    is translated rather than delegated.
    """
    for pattern in workflow_globs((ROOT / WORKFLOW).read_text(encoding='utf-8')):
        regex = re.escape(pattern).replace(r'\*\*', '.*').replace(r'\*', '[^/]*')
        if re.fullmatch(regex, path):
            return True
    return False


def main() -> int:
    failures: list[str] = []

    manifest = json.loads((ROOT / MANIFEST).read_text(encoding='utf-8'))
    prerequisites = manifest.get('prerequisites') or []
    ng = next((p for p in prerequisites if p.get('id') == 'OStimNG'), None)
    if ng is None:
        failures.append(f'{MANIFEST}: no OStimNG entry under prerequisites')
        pin = None
    else:
        pin = ng.get(FIELD)
        if not pin:
            failures.append(
                f'{MANIFEST}: the OStimNG prerequisite declares no {FIELD}; the CI '
                f'build has no revision to check out and cannot configure'
            )
        else:
            repository = (pin.get('repository') or '').strip()
            commit = (pin.get('commit') or '').strip()
            if not repository:
                failures.append(f'{MANIFEST}: {FIELD}.repository is empty')
            if not SHA.match(commit):
                failures.append(
                    f'{MANIFEST}: {FIELD}.commit is {commit!r}, which is not a full '
                    f'40-character commit sha; a branch or tag would drift silently'
                )

    workflow = (ROOT / WORKFLOW).read_text(encoding='utf-8')

    # The step that builds OStimNG, from its name to the next step at the same
    # indentation - so the assertions below are about that step and not the file.
    lines = workflow.splitlines()
    start = next((i for i, l in enumerate(lines)
                  if l.strip() == '- name: Build the OStimNG prerequisite'), None)
    if start is None:
        failures.append(
            f'{WORKFLOW}: no "Build the OStimNG prerequisite" step; the prerequisite '
            f'is not compiled anywhere, so a patch to it would reach no player'
        )
        step = ''
    else:
        end = start + 1
        while end < len(lines) and not re.match(r'^      - ', lines[end]):
            end += 1
        step = '\n'.join(lines[start:end])

    if step:
        if FIELD not in step:
            failures.append(
                f'{WORKFLOW}: the OStimNG step never reads {FIELD} from {MANIFEST}; '
                f'the revision it builds against has to come from the pin, not from a '
                f'hard-coded path or a branch tip'
            )
        if CHECKOUT not in step:
            failures.append(
                f'{WORKFLOW}: the OStimNG step no longer mentions {CHECKOUT}, which is '
                f'the path OStimNG reads through COMMONLIB_SSE_FOLDER'
            )
        # The original defect: asserting the checkout is already there, which was
        # only ever true while OStimNG still carried the gitlink.
        for bad in (
            'the recursive submodule checkout did not populate it',
            "OStimNG's pinned CommonLibSSE-NG is missing",
        ):
            if bad in step:
                failures.append(
                    f'{WORKFLOW}: the OStimNG step still requires the submodule '
                    f'checkout to supply {CHECKOUT} ({bad!r}); OStimNG f20db82 deleted '
                    f'that gitlink, so this fails before configuring'
                )

    # OStimNG's vcpkg manifest is at plugins/OStimNG/skse/vcpkg.json, not one
    # level down like the plugins'. The setup step derives the pinned baselines
    # to fetch from the plugin manifests, and vcpkg resolves builtin-baseline
    # with a bare `git show` that has no fetch fallback - so a manifest the
    # glob misses is a baseline that is never fetched, and the configure dies
    # with 'failed to `git show` versions/baseline.json'. The glob was
    # 'plugins/*/vcpkg.json' and missed exactly this one.
    for plugin in prerequisites:
        for manifest in sorted((ROOT / plugin['submodule']).rglob('vcpkg.json')):
            relative = manifest.relative_to(ROOT).as_posix()
            try:
                pinned_baseline = json.loads(manifest.read_text(encoding='utf-8')).get('builtin-baseline')
            except (OSError, ValueError) as error:
                failures.append(f'{relative} could not be read as a vcpkg manifest: {error}')
                continue
            # Only a manifest that pins a baseline needs the setup step to fetch
            # it; an overlay port declares no baseline and needs nothing.
            if pinned_baseline and not glob_matches(relative):
                failures.append(
                    f'{relative} pins builtin-baseline {str(pinned_baseline)[:12]}, but '
                    f'{WORKFLOW} collects those with a glob that does not match it; vcpkg '
                    f'resolves a baseline with a bare \'git show\' and no fetch fallback, '
                    f'so it would fail on a depth-1 checkout'
                )

    if failures:
        print(f'OSTIMNG BUILD CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('OSTIMNG BUILD CHECK OK')
    print(f'  {FIELD} pinned at {pin["commit"][:12]} and read by the CI step')
    return 0


if __name__ == '__main__':
    sys.exit(main())
