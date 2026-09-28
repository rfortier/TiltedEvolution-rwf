<#
.SYNOPSIS
Compiles the scripts this project ships into GameFiles/Skyrim/scripts/.

.DESCRIPTION
Ten .psc files under GameFiles/Skyrim/scripts/source are staged into the release
package as .pex. Before this script existed nothing in the repository compiled
them: the .pex files were committed, and the .psc beside them were the only
record of what they were built from. A fix to one of those scripts meant editing
the source, hand-running a compiler that only exists inside an installed Skyrim,
and hoping the result matched. Three of the ten are this project's own
(SkyrimTogetherUtils and the two launch/alias scripts); the rest are carried
from other mods.

This script makes that path reproducible with the same open-source compiler the
OStim consent scripts use, so no game and no Creation Kit are required.

Header resolution is the whole difficulty. The compiler takes -h per directory
and does not recurse, so each directory is passed explicitly. Here there is only
one: Code/plugins/papyrus/stubs, which this repository owns. Every type and
function the ten scripts reference is declared there. Nothing from an installed
game is read, which is the point - CI has no game.

All ten scripts rebuild. One of them needed a source change to get there:
SkyrimTogetherVerifyLaunchScript.psc held its error dialog together with
backslash-n escapes, and the pinned compiler has no escape handling in its string
scanner - it would emit a literal backslash and an n where the shipped .pex has a
real newline, and report success. The dialog is now one multi-line literal
holding the same bytes. Read docs/PAPYRUS-SOURCE-ARCHIVE.md.

The escape check below is a ratchet. A shipping script that uses a backslash
escape fails the build unless it is listed in $Unbuildable with a reason, and a
listed script that stops using one fails too, so the table cannot rot into a
place where broken files go to be forgotten.

.EXAMPLE
pwsh Code/plugins/papyrus/Compile-ShippingScripts.ps1
#>
[CmdletBinding()]
param(
    # Where the pinned compiler release is unpacked. Cached between runs.
    [string]$ToolRoot,

    # Skip the download and use a papyrus.exe already unpacked here.
    [string]$CompilerPath,

    # Where the .pex files are written. Defaults to the directory the release
    # package stages from, so a clean run regenerates the shipped files.
    [string]$OutputDir,

    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Resolve-PapyrusCompiler.ps1')

# Scripts the pinned compiler cannot rebuild faithfully, and why.
#
# Empty, and that is the point: every shipping script builds. It is kept because
# the escape check below needs somewhere to name an exception, and because a
# future script that needs one should have to write its reason here rather than
# quietly skip the compile. SkyrimTogetherVerifyLaunchScript was the one entry
# this table ever held; its dialog was rewritten as a multi-line literal so the
# pinned compiler can build it.
$Unbuildable = @{}

$SourceDir = Join-Path $RepoRoot "GameFiles/Skyrim/scripts/source"
$BaseStubDir = Join-Path $RepoRoot "Code/plugins/papyrus/stubs"
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot "GameFiles/Skyrim/scripts" }

foreach ($required in @($SourceDir, $BaseStubDir)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "header or source directory is missing: $required"
    }
}

# A backslash escape is the one construct the pinned compiler gets wrong, and it
# gets it wrong silently. Commented-out lines are ignored: they are not compiled
# and these files carry a lot of disabled debug tracing.
$escaped = @{}
foreach ($file in Get-ChildItem -LiteralPath $SourceDir -File -Filter *.psc) {
    $hit = Select-String -LiteralPath $file.FullName -Pattern '\\[ntr"\\]' -AllMatches |
        Where-Object { $_.Line.TrimStart() -notmatch '^;' }
    if ($hit) { $escaped[$file.BaseName] = @($hit).Count }
}

foreach ($name in $escaped.Keys) {
    if (-not $Unbuildable.ContainsKey($name)) {
        Write-Host "::error file=GameFiles/Skyrim/scripts/source/$name.psc::uses a backslash escape the pinned compiler does not implement"
        throw "$name.psc uses a backslash escape; the pinned compiler would emit it literally. Fix the script, or list it as unbuildable with a reason."
    }
}
foreach ($name in $Unbuildable.Keys) {
    if (-not $escaped.ContainsKey($name)) {
        Write-Host "::error::$name is listed unbuildable but no longer uses an escape"
        throw "$name.psc no longer needs its unbuildable entry; remove it so the list stays honest."
    }
}

$buildable = Get-ChildItem -LiteralPath $SourceDir -File -Filter *.psc |
    Where-Object { -not $Unbuildable.ContainsKey($_.BaseName) } |
    Sort-Object Name

if ($Unbuildable.Count -gt 0) {
    Write-Host "not rebuilt by this script:" -ForegroundColor Yellow
    foreach ($name in ($Unbuildable.Keys | Sort-Object)) {
        Write-Host "  $name - $($Unbuildable[$name])"
    }
    Write-Host ""
}

$Compiler = Resolve-PapyrusCompiler -RepoRoot $RepoRoot -ToolRoot $ToolRoot -CompilerPath $CompilerPath

Write-Host "compiler: $Compiler"
& $Compiler version 2>&1 | Select-Object -First 1 | ForEach-Object { Write-Host "  $_" }

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Order matters. The compiler does not recurse, so every directory that declares
# something the scripts use has to be listed. The source directory comes first
# because two scripts here extend one another.
$headers = @($SourceDir, $BaseStubDir)

$arguments = @("compile", "-nocache")
foreach ($header in $headers) { $arguments += @("-h", $header) }
foreach ($script in $buildable) { $arguments += @("-i", $script.FullName) }
$arguments += @("-o", $OutputDir)

# The compiler creates its cache directory - ".papyrus" - relative to the
# process working directory, not to any path it is given. Run from the repository
# root, and put the working directory back afterwards: this is dot-sourced
# context in a CI step, and silently moving the caller's location is the kind of
# thing that breaks a later command in the same step.
$previousLocation = Get-Location
try {
    Set-Location -LiteralPath $RepoRoot
    & $Compiler @arguments
    $compilerExit = $LASTEXITCODE
}
finally {
    Set-Location -LiteralPath $previousLocation
}
if ($compilerExit -ne 0) {
    throw "Papyrus compilation failed for the shipping scripts (exit $compilerExit)."
}

foreach ($script in $buildable) {
    $name = $script.BaseName + ".pex"
    $path = Join-Path $OutputDir $name
    if (-not (Test-Path -LiteralPath $path)) { throw "PEX not produced: $path" }
    $length = (Get-Item -LiteralPath $path).Length
    if ($length -le 0) { throw "PEX is empty: $path" }
    Write-Host ("  {0,-40} {1,7} bytes" -f $name, $length)
}

Write-Host ""
Write-Host "Shipping scripts compiled." -ForegroundColor Green
Write-Host "  $($buildable.Count) of $($buildable.Count + $Unbuildable.Count) scripts -> $OutputDir"
if ($Unbuildable.Count -gt 0) {
    Write-Host "  $($Unbuildable.Count) not rebuilt; see docs/PAPYRUS-SOURCE-ARCHIVE.md" -ForegroundColor Yellow
}
