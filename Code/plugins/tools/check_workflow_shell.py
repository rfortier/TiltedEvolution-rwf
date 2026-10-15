#!/usr/bin/env python3
"""Keep every PowerShell block in .github/workflows/ parseable.

A workflow's `run:` block is a script that only ever executes on a runner, so a
syntax error in one is invisible until the job it belongs to starts - and a job
that cannot start reports nothing useful. That is not hypothetical here: the
OStimNG prerequisite step shipped with an unterminated string literal
(`Write-Host "COMMONLIB_SSE_FOLDER=$env:COMMONLIB_SSE_FOLDER` with no closing
quote), so PowerShell failed to parse the whole block, the step exited 1 before
running a single command, and three separate workflows failed with no CMake
output to explain why. Every local gate passed, because none of them looked at
the workflow files.

This runs the same parser PowerShell itself uses, over every pwsh block, and
fails on the first syntax error in each. GitHub substitutes ${{ ... }} before
PowerShell sees a block, so those expressions are replaced with a literal first;
they are not valid PowerShell and would otherwise be reported as errors.
"""

from __future__ import annotations

import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
WORKFLOWS = ROOT / '.github' / 'workflows'
EXPRESSION = re.compile(r'\$\{\{.*?\}\}', re.S)

# The shells whose syntax is PowerShell. `pwsh` is PowerShell 7 and `powershell`
# is 5.1; a block with no shell on a Windows runner is also PowerShell, but it is
# skipped rather than guessed at, because the same block on a Linux runner would
# be bash and a false failure is worse than a missed one.
POWERSHELL_SHELLS = {'pwsh', 'powershell'}


def blocks() -> list[dict]:
    """Every run: block with its shell, via the YAML the runner reads."""
    # pyyaml is required, not optional. This gate used to print
    # 'POWERSHELL PARSE CHECK SKIPPED' and exit 0 when the import failed, with a
    # comment saying CI installs it first - and the job that runs this gate,
    # `contract`, installs nothing. A gate that cannot check must not report
    # success: 'skipped' and 'everything is fine' are the same output to every
    # caller, so the check would sit in CI looking like a guard while checking
    # nothing. Failing loudly is the only honest outcome, and it is also what
    # makes the missing dependency visible instead of mysterious.
    try:
        import yaml  # noqa: PLC0415
    except ImportError as exc:
        raise SystemExit(
            'WORKFLOW SHELL CHECK CANNOT RUN\n'
            f'  {exc}\n'
            '  This gate parses the workflows with the same YAML the runner reads, so\n'
            '  pyyaml is a requirement and not an optimisation. Install it in the job\n'
            '  that runs this gate (`python -m pip install pyyaml`); do not downgrade\n'
            '  this to a skip, because a skip is indistinguishable from a pass.'
        ) from exc

    found: list[dict] = []
    for path in sorted(WORKFLOWS.glob('*.yml')):
        document = yaml.safe_load(path.read_text(encoding='utf-8'))
        for job_name, job in (document.get('jobs') or {}).items():
            for index, step in enumerate(job.get('steps') or []):
                run = step.get('run')
                if not isinstance(run, str):
                    continue
                shell = (step.get('shell') or '').strip()
                if shell not in POWERSHELL_SHELLS:
                    continue
                found.append({
                    'file': path.relative_to(ROOT).as_posix(),
                    'job': job_name,
                    'step': step.get('name') or f'step {index}',
                    'text': EXPRESSION.sub('0', run),
                })
    return found


def parse_all(scripts: list[dict]) -> list[str]:
    """One PowerShell invocation for every block, so the gate stays fast.

    ParseInput is used rather than ParseFile because the blocks live in YAML; the
    script is handed over as JSON so no quoting layer can mangle it.
    """
    payload = json.dumps([{'id': i, 'text': s['text']} for i, s in enumerate(scripts)])
    program = (
        "$items = ConvertFrom-Json $input",
        "$out = @()",
        "foreach ($item in $items) {",
        "  $errors = $null; $tokens = $null",
        "  $null = [System.Management.Automation.Language.Parser]::ParseInput($item.text, [ref]$tokens, [ref]$errors)",
        "  foreach ($e in $errors) {",
        "    $out += [pscustomobject]@{ id = $item.id; line = $e.Extent.StartLineNumber; message = $e.Message }",
        "  }",
        "}",
        "$out | ConvertTo-Json -Compress -Depth 4",
    )
    result = subprocess.run(
        ['powershell', '-NoProfile', '-NonInteractive', '-Command', '\n'.join(program)],
        input=payload, capture_output=True, text=True, encoding='utf-8', errors='replace',
    )
    if result.returncode != 0:
        raise SystemExit(f'PowerShell could not run the parse check:\n{result.stderr.strip()[:800]}')

    raw = (result.stdout or '').strip()
    if not raw:
        return []
    parsed = json.loads(raw)
    if isinstance(parsed, dict):
        parsed = [parsed]

    failures = []
    for entry in parsed:
        script = scripts[int(entry['id'])]
        failures.append(
            f"{script['file']} ({script['job']} / {script['step']}) line "
            f"{entry['line']}: {entry['message']}"
        )
    return failures


def main() -> int:
    scripts = blocks()
    if not scripts:
        print('WORKFLOW SHELL CHECK FAILED')
        print('  no PowerShell block was found in any workflow')
        print('  Either the workflows moved, or the shell keys stopped being read;')
        print('  an empty result means this gate compared nothing.')
        return 1
    failures = parse_all(scripts)
    print(f'  {len(scripts)} PowerShell block(s) across the workflows')
    if failures:
        print()
        print(f'WORKFLOW SHELL CHECK FAILED ({len(failures)})')
        for failure in failures:
            print(f'  - {failure}')
        print()
        print('  A block that does not parse never runs a command, so the step fails')
        print('  with no output and every workflow that includes it fails too.')
        return 1
    print()
    print('WORKFLOW SHELL CHECK OK')
    print('  every PowerShell block in .github/workflows/ parses')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
