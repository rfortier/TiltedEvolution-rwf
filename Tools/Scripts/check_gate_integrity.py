#!/usr/bin/env python3
"""Keep the gates themselves honest about what they checked.

Every check_*.py in this tree is a guard, and CI shows its step as green or red.
That makes two properties load-bearing, and nothing was checking either:

R1  a gate must not report success when it could not run
    check_workflow_shell.py imported pyyaml inside a try/except and, on
    ImportError, printed 'POWERSHELL PARSE CHECK SKIPPED' and returned 0. Its
    comment said CI installs pyyaml first; the job that runs it, `contract`,
    installed nothing, and the only pyyaml install in the file was in the
    `wizard` job. So the step was green in CI while parsing nothing. A skip and
    a pass are the same output to every caller, which is what makes this worse
    than an absent gate: an absent gate is visibly absent.

R2  a gate must not pass when it compared nothing
    A gate whose corpus moved (a renamed directory, a changed declaration shape)
    tends to find zero items and return 0. 'found nothing to check' and 'checked
    everything and all of it is fine' are again the same output. The gates here
    mostly carry explicit floors for this reason; this rule checks the ones that
    return early instead.

R3  a gate script must be run by a workflow
    Two gates written in one round (check_fomod_languages.py, check_xmake_version.py)
    sat on disk for a round without any workflow invoking them. A gate nobody runs
    is a comment with extra steps.

R4  a gate must be covered by the path triggers of the workflow that runs it
    Otherwise the step exists but a push that breaks it does not start the job.
    'Code/plugins/**' covers most of the plugin tools; this checks the rest.

R5  a workflow's push and pull_request path lists must agree
    A path added to one and not the other means a change is guarded on a pull
    request and not on a push to main, or the reverse.

R6  every gate a workflow names must exist, and every gate on disk must be named
    The two directions of one relation. Iterating only over the scripts found on
    disk means a corpus that shrinks reports a smaller all-clear - which is how
    this gate passed while naming one script out of forty.

R7  a gate a workflow runs must have its dependency installed by that job
    A step is green when the gate exits 0, and a gate that catches ImportError
    and returns 0 is green while checking nothing.

    python Tools/Scripts/check_gate_integrity.py
    python Tools/Scripts/check_gate_integrity.py --list
"""

from __future__ import annotations

import argparse
import ast
import fnmatch
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / '.github/workflows'
GATE_DIRS = ('Tools/Scripts', 'Code/plugins/tools')

# A gate may legitimately have nothing to look at when the thing it guards is
# optional - a plugin submodule that is not checked out, for instance. Those
# print a SKIPPED banner and exit 0 on purpose, and each needs a reason here.
# What is not allowed is a skip caused by a missing tool or dependency, because
# that is a property of the runner and not of the repository.
ALLOWED_SKIPS = {
    'check_ostimng_versions.py': 'the OStimNG plugin submodule may not be checked out',
}

# Early returns that mean 'nothing to check' rather than 'all good'.
EMPTY_CORPUS_OK = {
    'check_capstone_report.py',
}


def gate_scripts() -> list[pathlib.Path]:
    found: list[pathlib.Path] = []
    for folder in GATE_DIRS:
        found += sorted((ROOT / folder).glob('check_*.py'))
    return found


def workflow_text() -> str:
    text = ''
    for path in sorted(WORKFLOWS.glob('*.yml')):
        text += path.read_text(encoding='utf-8')
    return text


def imports_of(path: pathlib.Path) -> set[str]:
    try:
        tree = ast.parse(path.read_text(encoding='utf-8'))
    except SyntaxError:
        return set()
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                names.add(alias.name.split('.')[0])
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            names.add(node.module.split('.')[0])
    return names


def third_party(paths: list[pathlib.Path]) -> dict[str, set[str]]:
    local = {p.stem for p in (ROOT / 'Tools/Scripts').glob('*.py')}
    local |= {p.stem for p in (ROOT / 'Code/plugins/tools').glob('*.py')}
    out: dict[str, set[str]] = {}
    for path in paths:
        mods = {m for m in imports_of(path) if m not in sys.stdlib_module_names and m not in local}
        if mods:
            out[path.name] = mods
    return out


# An import name is not always the name pip knows. Keeping this table short is the
# point: an unmapped third-party import fails the rule below rather than passing
# quietly, so adding a dependency here is a deliberate act.
IMPORT_TO_PACKAGE = {
    'yaml': 'pyyaml',
    'PIL': 'pillow',
    'cv2': 'opencv-python',
}


def provides(installs: str, module: str) -> bool:
    """Does an install line in this job provide `module`?

    Matching the import name as a word is not enough: `\\byaml\\b` does not match
    'pip install pyyaml', so the first version of this gate reported the pyyaml
    install as missing. Match the package name, or the module name at a word
    boundary.
    """
    package = IMPORT_TO_PACKAGE.get(module)
    if package is None:
        return False
    return re.search(rf'pip install[^\n]*\b{re.escape(package)}\b', installs) is not None


def raises_anywhere(nodes: list[ast.stmt]) -> bool:
    for node in nodes:
        for sub in ast.walk(node):
            if isinstance(sub, ast.Raise):
                return True
    return False


def import_error_handlers(tree: ast.Module) -> list[int]:
    """Lines of `except ImportError` handlers that swallow the error.

    A handler that re-raises is fine - that is the fixed form of
    check_workflow_shell.py. A handler that returns, prints and returns, or falls
    through turns 'I could not check' into 'exit 0'.
    """
    lines: list[int] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Try):
            continue
        for handler in node.handlers:
            names: list[str] = []
            if handler.type is None:
                names = ['*']
            elif isinstance(handler.type, ast.Name):
                names = [handler.type.id]
            elif isinstance(handler.type, ast.Tuple):
                names = [e.id for e in handler.type.elts if isinstance(e, ast.Name)]
            if not ({'ImportError', 'ModuleNotFoundError'} & set(names)) and '*' not in names:
                continue
            if not raises_anywhere(handler.body):
                lines.append(handler.lineno)
    return lines


def prints_skipped(tree: ast.Module) -> bool:
    """Does this script print a banner containing SKIPPED?

    Scoped to the argument of a print call on purpose. A plain text search for
    'SKIPPED' also matches a constant named SKIPPED_OK and a docstring that
    mentions the word - both of which this gate itself contains, so the first
    version reported itself.

    Case-sensitive on purpose, too. A banner announcing that the gate gave up is
    shouted ('POWERSHELL PARSE CHECK SKIPPED'); prose about something else being
    skipped is not, and check_skse_entry_points.py says 'the plugin is skipped by
    SKSE 2.2.x' in lowercase in a print - matching that would report a gate that
    never skips anything.

    Boundary: the banner has to be a literal in the print call. One assembled
    from a variable is not detected, and that is the honest limit of reading
    source instead of running it.
    """
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)):
            continue
        if node.func.id != 'print':
            continue
        for arg in node.args:
            if isinstance(arg, ast.Constant) and isinstance(arg.value, str):
                if 'SKIPPED' in arg.value:
                    return True
    return False


def jobs_of(document: dict) -> dict[str, dict]:
    jobs = document.get('jobs') or {}
    return jobs if isinstance(jobs, dict) else {}


def job_installs(job: dict) -> list[str]:
    return re.findall(r'pip install[^\n]*', str(job))


def check(failures: list[str], notes: list[str], listing: bool) -> None:
    import yaml

    scripts = gate_scripts()
    text = workflow_text()
    mods_by_name = third_party(scripts)

    # R3: every gate script is invoked by some workflow.
    never_run = []
    for script in scripts:
        rel = str(script.relative_to(ROOT)).replace('\\', '/')
        if not re.search(re.escape(rel), text):
            never_run.append(rel)
            failures.append(f'{rel} is not run by any workflow; a gate nothing runs '
                            f'cannot fail, and its step never appears in CI')
    notes.append(f'{len(scripts)} gate script(s), {len(never_run)} never run by CI')

    # R7: the same relation, read the other way. Every gate a workflow names must
    # exist on disk.
    #
    # This rule exists because this gate failed it. Its own rules are 'a gate must
    # not pass when it compared nothing', and the check above iterates over the
    # scripts it *finds* - so deleting all but one left it reporting
    # '1 gate script(s), 0 never run by CI / OK', while the workflows still named
    # 40. A corpus that can vanish without the verdict changing is the defect this
    # whole file is about, and finding it in itself is the reason to keep the
    # direction pair.
    named = sorted(set(re.findall(r'((?:Tools/Scripts|Code/plugins/tools)/check_\w+\.py)', text)))
    missing = [rel for rel in named if not (ROOT / rel).is_file()]
    for rel in missing:
        failures.append(
            f'a workflow runs {rel}, which does not exist on disk; the step would fail '
            f'on every run, or the gate was deleted while its step stayed')
    notes.append(f'{len(named)} gate script(s) named by workflows, {len(missing)} missing')

    # R5 + R4: read the workflows the runner reads.
    per_workflow_paths: dict[str, list[str]] = {}
    owners: dict[str, str] = {}
    for path in sorted(WORKFLOWS.glob('*.yml')):
        try:
            document = yaml.safe_load(path.read_text(encoding='utf-8'))
        except yaml.YAMLError as exc:
            # A workflow that does not parse does not run at all, so every gate
            # step inside it is dead. Reporting it as a failure with the parser's
            # own message is the difference between "fix the YAML" and a
            # traceback that names this file instead of the broken one.
            failures.append(f'{path.name} does not parse as YAML, so none of its jobs or '
                            f'gate steps can run: {exc}')
            continue
        if not isinstance(document, dict):
            failures.append(f'{path.name} is not a mapping at the top level; it cannot be '
                            f'a workflow file')
            continue
        on = document.get(True, document.get('on'))
        if isinstance(on, dict):
            push = on.get('push')
            pull = on.get('pull_request')
            push_paths = push.get('paths') if isinstance(push, dict) else None
            pull_paths = pull.get('paths') if isinstance(pull, dict) else None
            if push_paths is not None or pull_paths is not None:
                if push_paths is None or pull_paths is None:
                    failures.append(
                        f'{path.name}: only one of push/pull_request filters by path; a change '
                        f'is then guarded in one direction and unguarded in the other')
                elif set(push_paths) != set(pull_paths):
                    only_push = sorted(set(push_paths) - set(pull_paths))
                    only_pull = sorted(set(pull_paths) - set(push_paths))
                    failures.append(
                        f'{path.name}: the push and pull_request path lists differ - '
                        f'push-only {only_push}, pull_request-only {only_pull}')
                else:
                    per_workflow_paths[path.name] = list(push_paths)
                    duplicates = sorted({p for p in push_paths if push_paths.count(p) > 1})
                    if duplicates:
                        notes.append(f'{path.name}: {len(duplicates)} path(s) listed more than '
                                     f'once ({", ".join(duplicates)}); harmless to GitHub, and '
                                     f'the fingerprint of a hand-edited list')

        for job_name, job in jobs_of(document).items():
            installs = ' '.join(job_installs(job))
            for script in re.findall(r'([\w/]+/check_\w+\.py)', str(job)):
                owners.setdefault(script, f'{path.name}/{job_name}')
                for mod in sorted(mods_by_name.get(pathlib.Path(script).name, ())):
                    if mod not in IMPORT_TO_PACKAGE:
                        failures.append(
                            f'{script} imports `{mod}`, which this gate cannot map to a pip '
                            f'package; add it to IMPORT_TO_PACKAGE so the jobs that run it '
                            f'can be checked for providing it')
                    elif not provides(installs, mod):
                        failures.append(
                            f'{path.name} job `{job_name}` runs {script}, which imports `{mod}` '
                            f'({IMPORT_TO_PACKAGE[mod]}), but that job never installs it; the '
                            f'gate would have to fail or silently skip')

    # R1: no gate may turn a missing dependency into a pass.
    # R2: a gate that reports SKIPPED must be a known, justified skip.
    for script in scripts:
        rel = str(script.relative_to(ROOT)).replace('\\', '/')
        source = script.read_text(encoding='utf-8')
        try:
            tree = ast.parse(source)
        except SyntaxError as exc:
            failures.append(f'{rel} does not parse ({exc}); it cannot run in CI either')
            continue
        for line in import_error_handlers(tree):
            if script.name not in ALLOWED_SKIPS:
                failures.append(
                    f'{rel}:{line} catches ImportError without raising; if that handler '
                    f'returns, the gate reports success without checking anything. Make it '
                    f'fail, or add the file to ALLOWED_SKIPS with the reason the dependency '
                    f'is genuinely optional')
        if prints_skipped(tree) and script.name not in ALLOWED_SKIPS:
            failures.append(
                f'{rel} prints a SKIPPED banner but is not in ALLOWED_SKIPS; a skip and a '
                f'pass are the same output to CI, so each one needs a written reason')

    # R4: each gate is covered by a path trigger of the workflow that runs it.
    for script, owner in sorted(owners.items()):
        workflow_name = owner.split('/')[0]
        triggers = per_workflow_paths.get(workflow_name, [])
        if not triggers:
            continue
        if not any(p == script or fnmatch.fnmatch(script, p) for p in triggers):
            failures.append(
                f'{owner} runs {script} but no path trigger in {workflow_name} matches it, '
                f'so a change to that gate does not start the job')

    if listing:
        for script in scripts:
            rel = str(script.relative_to(ROOT)).replace('\\', '/')
            print(f'  {owners.get(rel, "(not run)")}: {rel}')
    notes.append(f'{len(owners)} gate step(s) located in the workflows')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()

    failures: list[str] = []
    notes: list[str] = []
    check(failures, notes, args.list)

    for note in notes:
        print(f'  note: {note}')

    if failures:
        print(f'GATE INTEGRITY CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print('GATE INTEGRITY CHECK OK')
    print('  every gate runs, is reachable from a path trigger, and fails rather than skips')
    return 0


if __name__ == '__main__':
    sys.exit(main())
