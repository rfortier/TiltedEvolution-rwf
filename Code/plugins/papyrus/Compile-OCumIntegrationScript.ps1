<#
.SYNOPSIS
Compiles the optional OStimTogether OCum Ascended integration script.

.DESCRIPTION
OStimTogetherOCum.pex is the quest script the optional OCum Ascended option needs.
The plugin ships the quest esp (OStimTogether_OCum.esp) as package content, and that
esp names this script in its VMAD, so without the .pex the quest has no code: the
OnInit that calls RegisterIntegration never runs, and the native side that dispatches
"OStimTogetherOCum"/"RegisterIntegration" by name has nothing to call.

It was missing. The plugin's own optional/OCumIntegration/compile-ocum-integration.ps1
builds it with PapyrusCompiler.exe from an installed Skyrim, and its build-fomod.ps1
throws when the .pex is absent - but this repository stages package/Data directly and
never runs those scripts, so the esp shipped without its script and nothing said so.

This script uses the same pinned open-source compiler as the other compile scripts in
this directory, via Resolve-PapyrusCompiler.ps1. Header resolution is the same shape as
Compile-OStimConsentScripts.ps1:

  Data/Scripts/Source          the script being compiled
  Dependencies/Source          the plugin's own OActor stub
  Code/plugins/papyrus/stubs   base types (Quest, Form, Game, Debug...)

The base stubs needed four additions for this script to build at all - Form's
RegisterForModEvent / UnregisterForModEvent / RegisterForUpdate / UnregisterForUpdate
and Game's IsPluginInstalled - all copied verbatim from the game's own sources.

.EXAMPLE
pwsh Code/plugins/papyrus/Compile-OCumIntegrationScript.ps1
#>
[CmdletBinding()]
param(
    # Where the pinned compiler release is unpacked. Cached between runs.
    [string]$ToolRoot,

    # Skip the download and use a papyrus.exe already unpacked here.
    [string]$CompilerPath,

    # Where the .pex is written. Defaults to the directory the plugin's own
    # build scripts expect; CI overrides it to the plugin build tree so the
    # existing artifact collection picks it up.
    [string]$OutputDir,

    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Resolve-PapyrusCompiler.ps1')

$PluginRoot = Join-Path $RepoRoot "plugins/OStimTogether/optional/OCumIntegration"
$SourceDir = Join-Path $PluginRoot "Data/Scripts/Source"
$StubDir = Join-Path $PluginRoot "Dependencies/Source"
$BaseStubDir = Join-Path $RepoRoot "Code/plugins/papyrus/stubs"
if (-not $OutputDir) { $OutputDir = Join-Path $PluginRoot "package/Data/Scripts" }

foreach ($required in @($SourceDir, $StubDir, $BaseStubDir)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "header or source directory is missing: $required"
    }
}

$Compiler = Resolve-PapyrusCompiler -RepoRoot $RepoRoot -ToolRoot $ToolRoot -CompilerPath $CompilerPath

Write-Host "compiler: $Compiler"
& $Compiler version 2>&1 | Select-Object -First 1 | ForEach-Object { Write-Host "  $_" }

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Dependencies/Source carries only the OActor stub the script needs; the base
# stubs come last because nothing before them declares a base type.
$headers = @($StubDir, $BaseStubDir)

$arguments = @("compile", "-nocache")
foreach ($header in $headers) { $arguments += @("-h", $header) }
$arguments += @("-i", $SourceDir, "-o", $OutputDir)

# The compiler creates its cache directory relative to the process working
# directory, so run from the repository root and restore the caller afterwards.
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
    throw "Papyrus compilation failed for the OCum integration script (exit $compilerExit)."
}

$name = "OStimTogetherOCum.pex"
$path = Join-Path $OutputDir $name
if (-not (Test-Path -LiteralPath $path)) { throw "PEX not produced: $path" }
$length = (Get-Item -LiteralPath $path).Length
if ($length -le 0) { throw "PEX is empty: $path" }
Write-Host ("  {0,-30} {1,6} bytes" -f $name, $length)

Write-Host ""
Write-Host "OCum integration script compiled." -ForegroundColor Green
Write-Host "  output $OutputDir"
