<#
.SYNOPSIS
Assembles the Skyrim Together Next client mod package (the single all-in-one zip).

.DESCRIPTION
Builds the archive staging tree that the shipped FOMOD wizard installs into: the
framework core from build output plus GameFiles/Skyrim, the optional launcher and
launch-check folders, and the optional companion plugins staged by
Add-PluginPayload.ps1.

This lives in a script rather than inline in the release workflow so the same
code path can be dry-run locally and in CI. Packaging bugs that only appear in a
tag build are expensive; this one is exercised on every run.

The wizard is verified against the assembled tree before the zip is written, so
a wizard entry pointing at a folder the build never staged fails the build
instead of shipping an option that installs nothing.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Stage,
    [Parameter(Mandatory = $true)][string]$Dist,

    # Directory holding the framework build output (SkyrimTogetherSKSE.dll,
    # runtime payload, launcher and server binaries).
    [Parameter(Mandatory = $true)][string]$BuildDir,

    [string]$Version = '',
    [string]$ArtifactsRoot = '',
    [string]$GameFiles = 'GameFiles/Skyrim',
    [switch]$SkipZip,

    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Text)
    Write-Host ''
    Write-Host "== $Text" -ForegroundColor Cyan
}

$stagePath = Join-Path $RepoRoot $Stage
$distPath = Join-Path $RepoRoot $Dist
$gameFilesPath = Join-Path $RepoRoot $GameFiles
$buildPath = Join-Path $RepoRoot $BuildDir

foreach ($required in @($gameFilesPath, $buildPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "required input not found: $required"
    }
}

Write-Step 'Resetting staging tree'
if (Test-Path -LiteralPath $stagePath) {
    Remove-Item -LiteralPath $stagePath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagePath, $distPath | Out-Null

Write-Step 'Staging the framework core'
New-Item -ItemType Directory -Force -Path (Join-Path $stagePath 'fomod'),
    (Join-Path $stagePath 'launcher'),
    (Join-Path $stagePath 'VerifyScript/scripts') | Out-Null

Copy-Item -Path (Join-Path $gameFilesPath 'fomod/*') -Destination (Join-Path $stagePath 'fomod') -Recurse -Force

# The wizard reads author and version from fomod/info.xml. Stamp the version so
# it can never drift from the archive it ships inside; drop the leading "v"
# because the field shows a version, not a tag.
$versionFile = Join-Path $stagePath 'fomod/info.xml'
if ((Test-Path -LiteralPath $versionFile) -and $Version) {
    $stamped = $Version -replace '^v', ''
    # Read and write as UTF-8 explicitly: info.xml carries Chinese text and
    # Windows PowerShell 5.1 would otherwise round-trip it through the ANSI code
    # page, turning the wizard description into mojibake inside the zip.
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $xml = [System.IO.File]::ReadAllText($versionFile, $utf8)
    $xml = $xml -replace '<Version>[^<]*</Version>', "<Version>$stamped</Version>"
    [System.IO.File]::WriteAllText($versionFile, $xml, $utf8)

    if (-not (Select-String -LiteralPath $versionFile -Pattern "<Version>$stamped</Version>" -Quiet)) {
        throw "fomod/info.xml was not stamped with version $stamped"
    }
    Write-Host "  info.xml version -> $stamped"
}

Get-ChildItem -LiteralPath $gameFilesPath -Force |
    Where-Object { $_.Name -ne 'fomod' } |
    ForEach-Object { Copy-Item -Recurse -Force $_.FullName $stagePath }

# SKSE bootstrap plugin: on load it deploys the runtime payload from
# Data/SkyrimTogetherRuntime/ into the game root.
# Moved, not copied: anything left in the build directory below is swept into
# the runtime payload, and a second copy of the bootstrap plugin has no business
# in the game root.
New-Item -ItemType Directory -Force -Path (Join-Path $stagePath 'SKSE/Plugins') | Out-Null
Move-Item -LiteralPath (Join-Path $buildPath 'SkyrimTogetherSKSE.dll') -Destination (Join-Path $stagePath 'SKSE/Plugins/SkyrimTogetherSKSE.dll') -Force

# The launch verification script is a wizard option: it relies on a papyrus
# native that degrades on 1.5.x (it would always nag that Skyrim Together is not
# running), so 1.5.x players uncheck it and it must not sit in the
# always-installed scripts folder.
$verifyScript = Join-Path $stagePath 'scripts/SkyrimTogetherVerifyLaunchScript.pex'
if (Test-Path -LiteralPath $verifyScript) {
    Move-Item -LiteralPath $verifyScript -Destination (Join-Path $stagePath 'VerifyScript/scripts/') -Force
}
else {
    Write-Warning 'SkyrimTogetherVerifyLaunchScript.pex not found in the staged scripts folder'
}

# Self-deploying runtime payload: everything the client needs at the game root.
New-Item -ItemType Directory -Force -Path (Join-Path $stagePath 'SkyrimTogetherRuntime') | Out-Null

foreach ($binary in 'SkyrimTogether.exe', 'EarlyLoad.dll') {
    $candidate = Join-Path $buildPath $binary
    if (Test-Path -LiteralPath $candidate) {
        Move-Item -LiteralPath $candidate -Destination (Join-Path $stagePath 'launcher/') -Force
    }
}

# Build byproducts and the dedicated-server binaries stay in the build directory:
# the server package step copies them from there, and none of them belong in the
# client runtime payload. Filtering is done by name on purpose - Get-ChildItem
# silently ignores -Exclude when it is combined with -LiteralPath, which is
# exactly how a test executable ends up shipped to players.
$holdBack = @(
    'SkyrimTogetherServer.exe',
    'STServer.dll',
    'SkyrimTogetherSKSE.dll'
)
# Directories are moved as well as files: the payload is not only dlls. cef
# needs locales/ and its .pak resources, and the overlay needs UI/, none of
# which match a file filter. An earlier version of this filtered with -File
# and silently produced a client that started and never opened the
# multiplayer menu.
Get-ChildItem -Path (Join-Path $buildPath '*') |
    Where-Object {
        $_.PSIsContainer -or (
            $_.Name -notmatch '\.(pdb|lib|exp|ilk|sym)$' -and
            $_.Name -notmatch 'Tests?\.exe$' -and
            $holdBack -notcontains $_.Name
        )
    } |
    Move-Item -Destination (Join-Path $stagePath 'SkyrimTogetherRuntime/') -Force

New-Item -ItemType File -Path (Join-Path $stagePath 'SkyrimTogetherRuntime/.str_version') | Out-Null

# Both per-version runtimes have to be in the payload: the SKSE bootstrap picks
# one by game version, and a missing one means the game starts with no Skyrim
# Together at all.
foreach ($dll in 'SkyrimTogetherRuntime.dll', 'SkyrimTogetherRuntime_1_5.dll') {
    if (-not (Test-Path -LiteralPath (Join-Path $stagePath "SkyrimTogetherRuntime/$dll"))) {
        throw "mod package is missing $dll (build the legacy 1.5.x client too)"
    }
}

# The same set the playable-build workflow asserts, for the same reason: the
# bootstrap deploys this directory into the game root, so anything missing
# here is missing at runtime and the symptom is a game that starts and never
# opens the multiplayer menu - which reads like a code fault, not a packaging
# one.
foreach ($item in 'libcef.dll', 'TPProcess.exe', 'icudtl.dat', 'resources.pak', 'locales', 'UI') {
    if (-not (Test-Path -LiteralPath (Join-Path $stagePath "SkyrimTogetherRuntime/$item"))) {
        throw "runtime payload is missing $item"
    }
}

Write-Step 'Staging the companion plugins'
$pluginArgs = @{ Stage = $Stage; RepoRoot = $RepoRoot }
if ($ArtifactsRoot) { $pluginArgs['ArtifactsRoot'] = $ArtifactsRoot }
# The script is invoked with &, so a failure surfaces as $? rather than as
# $LASTEXITCODE, which would still hold the exit code of the last native command.
& (Join-Path $RepoRoot 'Tools/Packaging/Add-PluginPayload.ps1') @pluginArgs
if (-not $?) {
    throw 'companion plugin staging failed; see the output above'
}

Write-Step 'Verifying the wizard against the assembled tree'
$merge = Join-Path $RepoRoot 'Code/plugins/tools/merge_fomod.py'
$stageForPython = (Resolve-Path -LiteralPath $stagePath).Path
# python is a native command, so $LASTEXITCODE is correct here.
& python $merge check --stage $stageForPython
if ($LASTEXITCODE -ne 0) {
    throw "FOMOD wizard does not match the assembled tree (exit $LASTEXITCODE)"
}

if ($SkipZip) {
    Write-Host ''
    Write-Host 'PACKAGE STAGED (no zip requested)'
    Write-Host "  $stageForPython"
    exit 0
}

Write-Step 'Writing the archive'
$archiveName = if ($Version) { "SkyrimTogetherNextMod-$Version.zip" } else { 'SkyrimTogetherNextMod.zip' }
$archivePath = Join-Path $distPath $archiveName
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

Compress-Archive -Path (Join-Path $stagePath '*') -DestinationPath $archivePath -Force

$sizeMb = [math]::Round((Get-Item -LiteralPath $archivePath).Length / 1MB, 1)
Write-Host ''
Write-Host 'MOD PACKAGE OK'
Write-Host "  archive  $archivePath ($sizeMb MB)"
Write-Host "  stage    $stageForPython"
exit 0
