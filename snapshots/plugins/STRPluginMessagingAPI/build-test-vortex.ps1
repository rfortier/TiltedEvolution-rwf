param(
    [string]$Configuration = "Release",
    [string]$Version = "0.9.3"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Builder = Join-Path $Root "build-vortex.ps1"

if (-not (Test-Path -LiteralPath $Builder -PathType Leaf)) {
    throw "Builder principal introuvable: $Builder"
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " STR Plugin Messaging API v$Version - TEST FOMOD Build" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "Diagnostic client: INCLUDED" -ForegroundColor Yellow
Write-Host "Expected archive: STRPluginMessagingAPI-v$Version-test.zip" -ForegroundColor Yellow
Write-Host ""

& $Builder `
    -Configuration $Configuration `
    -Version $Version `
    -IncludeDiagnostic

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}