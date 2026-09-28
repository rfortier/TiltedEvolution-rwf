<#
.SYNOPSIS
Compiles the OStimTogether Add Actor consent scripts with an open-source compiler.

.DESCRIPTION
OSKSE.pex and OStimTogetherNative.pex cannot be built by CMake: they are Papyrus,
not C++, and the plugin's own compat/OStimUIConsent/compile-ui-consent.ps1 needs
PapyrusCompiler.exe from an installed Skyrim, which no CI runner has.

This script uses russo-2025/papyrus-compiler instead: open source, no game and no
Creation Kit required, published as a Windows release. The release tag and its
SHA-256 live in Resolve-PapyrusCompiler.ps1, which this script dot-sources, so
the pin is defined once for every compile script in this directory. It compiles
the two scripts into compat/OStimUIConsent/package/Data/Scripts/ so the plugin's
own build scripts and the packaging manifest find them where they already expect
them.

Header resolution is the whole difficulty. The compiler takes -h per directory and
does not recurse, so each directory is passed explicitly and each one only
supplies what the previous ones do not declare:

  Data/Scripts/Source          the two scripts being compiled
  Dependencies/Source          the plugin's own OStim/UIExtensions stubs
  Code/plugins/papyrus/stubs   base types (Actor, Form, ModEvent, NiOverride...)

The last directory is owned by this repository rather than by the plugin
submodule, because the submodule cannot be pushed to and its Dependencies/Source
is missing every base type. NiOverride is the one that matters most: OSKSE.psc
calls five NiOverride functions and without a header the compile dies on an
unknown type.

.EXAMPLE
pwsh Code/plugins/papyrus/Compile-OStimConsentScripts.ps1
#>
[CmdletBinding()]
param(
    # Where the pinned compiler release is unpacked. Cached between runs.
    [string]$ToolRoot,

    # Skip the download and use a papyrus.exe already unpacked here.
    [string]$CompilerPath,

    # Where the two .pex files are written. Defaults to the directory the
    # plugin's own build scripts expect; CI overrides it to the plugin build
    # tree so the existing artifact collection picks them up.
    [string]$OutputDir,

    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Resolve-PapyrusCompiler.ps1')

$PluginRoot = Join-Path $RepoRoot "plugins/OStimTogether/compat/OStimUIConsent"
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

if (Test-Path -LiteralPath $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Order matters. The compiler does not recurse, so every directory that declares
# something the scripts use has to be listed. Dependencies/Source deliberately
# shadows OStim's own scripts so the compile does not pull in OStim's UI tree;
# the base stubs come last because nothing before them declares a base type.
$headers = @($StubDir, $BaseStubDir)

$arguments = @("compile", "-nocache")
foreach ($header in $headers) { $arguments += @("-h", $header) }
$arguments += @("-i", $SourceDir, "-o", $OutputDir)

# The compiler creates its cache directory - ".papyrus" - relative to the
# process working directory, not to any path it is given. Run from the repository
# root, and restore the caller's location afterwards.
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
    throw "Papyrus compilation failed for the Add Actor consent scripts (exit $compilerExit)."
}

foreach ($name in @("OSKSE.pex", "OStimTogetherNative.pex")) {
    $path = Join-Path $OutputDir $name
    if (-not (Test-Path -LiteralPath $path)) { throw "PEX not produced: $path" }
    $length = (Get-Item -LiteralPath $path).Length
    if ($length -le 0) { throw "PEX is empty: $path" }
    Write-Host ("  {0,-26} {1,6} bytes" -f $name, $length)
}

Write-Host ""
Write-Host "Papyrus consent scripts compiled." -ForegroundColor Green
Write-Host "  output $OutputDir"
Write-Host "  OSKSE.pex must win its file conflict against OStim for the Add Actor gate to apply." -ForegroundColor Yellow
