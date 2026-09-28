<#
.SYNOPSIS
Stages the optional companion plugins into the Skyrim Together Next mod package.

.DESCRIPTION
Reads Code/plugins/plugins.json and materialises every plugin payload and build
artifact into the archive staging tree, under <stageRoot>/<id>/ for the plugin
itself and <stageRoot>/<id>__<subId>/ for each sub-option. Those two folder
shapes are exactly what the generated FOMOD wizard points at, so the wizard and
the archive are driven by one manifest and cannot disagree.

Repo content (ini files, esp/pex that ship in the repository) is copied from the
plugin submodule. Compiled output (dlls) is copied from an artifacts root laid
out as <artifactsRoot>/<pluginId>/ by the build jobs.

A missing payload source or a missing artifact fails the build. A plugin that
built successfully but produced no DLL must not silently ship a wizard entry
that installs nothing.

.EXAMPLE
# after the framework build has filled the stage
pwsh Tools/Packaging/Add-PluginPayload.ps1 -Stage mod-pack -ArtifactsRoot plugin-artifacts

.EXAMPLE
# verify an already assembled tree without touching it
pwsh Tools/Packaging/Add-PluginPayload.ps1 -Stage mod-pack -VerifyOnly
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Stage,

    # Root holding <pluginId>/ folders of compiled output. Optional when
    # every artifact already exists in the stage (for example a rebuild).
    [string]$ArtifactsRoot,

    # Verify the stage is complete and exit non-zero if it is not; copies nothing.
    [switch]$VerifyOnly,

    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ManifestPath = Join-Path $RepoRoot 'Code/plugins/plugins.json'
if (-not (Test-Path -LiteralPath $ManifestPath)) {
    throw "plugin manifest not found: $ManifestPath"
}

$Manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$StageRoot = $Manifest.stageRoot
$PluginsRoot = Join-Path $RepoRoot 'plugins'

# Strict mode makes a missing property a hard error, but payload, artifacts and
# subOptions are all legitimately optional in the manifest. Treat an absent key
# as an empty list rather than a typo.
function Get-ListProperty {
    param([object]$Object, [string]$Name)

    if ($null -eq $Object.PSObject.Properties[$Name]) {
        return @()
    }
    return @($Object.PSObject.Properties[$Name].Value)
}

function Resolve-PluginStagingRoot {
    param([string]$PluginId, [string]$SubId)

    $leaf = if ($SubId) { $PluginId + '__' + $SubId } else { $PluginId }
    return (Join-Path $Stage (Join-Path $StageRoot $leaf))
}

function Copy-PayloadItem {
    param(
        [string]$PluginRoot,
        [string]$StagingRoot,
        [string]$Source,
        [string]$Dest
    )

    # A source beginning with ../ is repo-relative rather than submodule-relative:
    # this repository owns a few packaged files (see payloadNote in the manifest)
    # because the plugin repositories are pinned and are not pushed to from here.
    $sourcePath = if ($Source.StartsWith('..')) {
        [System.IO.Path]::GetFullPath((Join-Path $PluginRoot $Source))
    }
    else {
        Join-Path $PluginRoot $Source
    }

    if (-not (Test-Path -LiteralPath $sourcePath)) {
        throw "payload source is missing: $Source (resolved to $sourcePath)"
    }

    New-Item -ItemType Directory -Force -Path $StagingRoot | Out-Null

    if ($Dest -eq '.' -or [string]::IsNullOrWhiteSpace($Dest)) {
        if ((Get-Item -LiteralPath $sourcePath).PSIsContainer) {
            Copy-Item -Path (Join-Path $sourcePath '*') -Destination $StagingRoot -Recurse -Force
        }
        else {
            Copy-Item -LiteralPath $sourcePath -Destination $StagingRoot -Force
        }
        return
    }

    $destPath = Join-Path $StagingRoot $Dest
    $destDir = Split-Path -Parent $destPath
    if ($destDir) {
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    }

    if ((Get-Item -LiteralPath $sourcePath).PSIsContainer) {
        New-Item -ItemType Directory -Force -Path $destPath | Out-Null
        Copy-Item -Path (Join-Path $sourcePath '*') -Destination $destPath -Recurse -Force
    }
    else {
        Copy-Item -LiteralPath $sourcePath -Destination $destPath -Force
    }
}

# Returns $null (and records the problem) when the artifact cannot be found,
# so one build run reports every missing piece instead of only the first.
function Copy-Artifact {
    param(
        [string]$PluginId,
        [string]$StagingRoot,
        [string]$From,
        [string]$Dest
    )

    if (-not $ArtifactsRoot) {
        $script:problems += "artifact '$From' for $PluginId was not supplied; pass -ArtifactsRoot"
        return
    }

    $pluginArtifacts = Join-Path $ArtifactsRoot $PluginId
    if (-not (Test-Path -LiteralPath $pluginArtifacts)) {
        $script:problems += "no build output for $PluginId under $pluginArtifacts"
        return
    }

    $matches = @(Get-ChildItem -LiteralPath $pluginArtifacts -Recurse -File -Filter $From)
    if ($matches.Count -eq 0) {
        $script:problems += "$PluginId produced no '$From' under $pluginArtifacts"
        return
    }
    if ($matches.Count -gt 1) {
        $shortest = $matches | Sort-Object { $_.FullName.Length } | Select-Object -First 1
        Write-Warning "$PluginId produced $($matches.Count) files named '$From'; using $($shortest.FullName)"
        $chosen = $shortest
    }
    else {
        $chosen = $matches[0]
    }

    $destPath = Join-Path $StagingRoot $Dest
    $destDir = Split-Path -Parent $destPath
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Copy-Item -LiteralPath $chosen.FullName -Destination $destPath -Force

    Write-Host ("    artifact {0,-34} -> {1}" -f $From, $Dest)
}

$staged = 0
$artifacts = 0
$problems = @()

# Copy-Artifact records into the script scope so it can keep going.
$script:problems = $problems

foreach ($plugin in $Manifest.plugins) {
    $pluginRoot = Join-Path $PluginsRoot $plugin.id
    if (-not (Test-Path -LiteralPath $pluginRoot)) {
        $problems += "submodule not checked out: plugins/$($plugin.id)"
        continue
    }

    $pluginStage = Resolve-PluginStagingRoot -PluginId $plugin.id

    $payloadItems = Get-ListProperty -Object $plugin -Name 'payload'
    $artifactItems = Get-ListProperty -Object $plugin -Name 'artifacts'

    if (-not $VerifyOnly) {
        Write-Host "  $($plugin.id)"
        foreach ($item in $payloadItems) {
            Copy-PayloadItem -PluginRoot $pluginRoot -StagingRoot $pluginStage -Source $item.source -Dest $item.dest
            $staged++
        }
        foreach ($item in $artifactItems) {
            Copy-Artifact -PluginId $plugin.id -StagingRoot $pluginStage -From $item.from -Dest $item.dest
            $artifacts++
        }
    }

    foreach ($item in $artifactItems) {
        $expected = Join-Path $pluginStage $item.dest
        if (-not (Test-Path -LiteralPath $expected -PathType Leaf)) {
            $problems += "missing staged artifact: ($StageRoot)/$($plugin.id)/$($item.dest)"
        }
    }

    foreach ($sub in (Get-ListProperty -Object $plugin -Name 'subOptions')) {
        $subStage = Resolve-PluginStagingRoot -PluginId $plugin.id -SubId $sub.id

        # Sub-options may declare artifacts too, not just payload. The OCum
        # Ascended option needs it: its quest esp names a Papyrus script in its
        # VMAD, and that script is compiled rather than committed, so there is
        # nothing for a payload rule to copy.
        #
        # Artifacts rather than payload on purpose. A payload rule copies
        # whatever happens to be in the source tree, so a compile that never ran
        # stages a directory that is merely smaller and says nothing; an artifact
        # rule is checked against the build output and reports the missing file.
        $subArtifactItems = Get-ListProperty -Object $sub -Name 'artifacts'

        if (-not $VerifyOnly) {
            foreach ($item in (Get-ListProperty -Object $sub -Name 'payload')) {
                Copy-PayloadItem -PluginRoot $pluginRoot -StagingRoot $subStage -Source $item.source -Dest $item.dest
                $staged++
            }
            foreach ($item in $subArtifactItems) {
                Copy-Artifact -PluginId $plugin.id -StagingRoot $subStage -From $item.from -Dest $item.dest
                $artifacts++
            }
        }

        foreach ($item in $subArtifactItems) {
            $expected = Join-Path $subStage $item.dest
            if (-not (Test-Path -LiteralPath $expected -PathType Leaf)) {
                $problems += "missing staged artifact: ($StageRoot)/$($plugin.id)__$($sub.id)/$($item.dest)"
            }
        }

        $payloadFiles = @(Get-ChildItem -LiteralPath $subStage -Recurse -File -ErrorAction SilentlyContinue)
        if ($payloadFiles.Count -eq 0) {
            $problems += "staged sub-option is empty: ($StageRoot)/$($plugin.id)__$($sub.id)"
        }
    }

    $pluginFiles = @(Get-ChildItem -LiteralPath $pluginStage -Recurse -File -ErrorAction SilentlyContinue)
    if ($pluginFiles.Count -eq 0) {
        $problems += "staged plugin is empty: ($StageRoot)/$($plugin.id)"
    }
}

$problems = @($script:problems)

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host "PLUGIN STAGING FAILED ($($problems.Count))" -ForegroundColor Red
    foreach ($problem in $problems) {
        Write-Host "  - $problem"
    }
    exit 1
}

$mode = if ($VerifyOnly) { 'verified' } else { 'staged' }
Write-Host ''
Write-Host "PLUGIN STAGING OK ($mode)"
Write-Host "  stage            $Stage"
Write-Host "  payload items    $staged"
Write-Host "  build artifacts  $artifacts"
Write-Host "  plugins          $($Manifest.plugins.Count)"
exit 0
