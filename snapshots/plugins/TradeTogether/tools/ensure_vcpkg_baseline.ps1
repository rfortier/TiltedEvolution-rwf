param(
    [Parameter(Mandatory=$true)]
    [string]$VcpkgRoot
)

$ErrorActionPreference = 'Stop'

$manifestPath = Join-Path $PSScriptRoot '..\vcpkg.json'
$manifestPath = [System.IO.Path]::GetFullPath($manifestPath)

if (-not (Test-Path $manifestPath)) {
    throw "vcpkg.json introuvable: $manifestPath"
}

if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
    throw "Le dossier vcpkg n'est pas un clone Git: $VcpkgRoot"
}

$gitCommand = Get-Command git -ErrorAction SilentlyContinue
if (-not $gitCommand) {
    throw "Git est introuvable dans PATH. TradeTogether a besoin de lire le commit HEAD de ton vcpkg."
}

$baseline = (& git -C $VcpkgRoot rev-parse HEAD 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or $baseline -notmatch '^[0-9a-fA-F]{40}$') {
    throw "Impossible de determiner le commit HEAD de vcpkg dans: $VcpkgRoot"
}

$manifest = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
$existing = $manifest.PSObject.Properties['builtin-baseline']

if ($existing -and $existing.Value -eq $baseline) {
    Write-Host "[TradeTogether] vcpkg builtin-baseline deja correct: $baseline"
    exit 0
}

if ($existing) {
    $manifest.'builtin-baseline' = $baseline
} else {
    $manifest | Add-Member -NotePropertyName 'builtin-baseline' -NotePropertyValue $baseline
}

$json = $manifest | ConvertTo-Json -Depth 32
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine, $utf8NoBom)

Write-Host "[TradeTogether] builtin-baseline vcpkg configure sur: $baseline"
