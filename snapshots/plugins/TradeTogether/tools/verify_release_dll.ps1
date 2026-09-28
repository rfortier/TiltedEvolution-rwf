param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,

    [Parameter(Mandatory = $true)]
    [string]$ExpectedVersion
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $DllPath -PathType Leaf)) {
    throw "TradeTogether DLL not found: $DllPath"
}

$bytes = [System.IO.File]::ReadAllBytes($DllPath)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
$expectedMarker = "TradeTogether v$ExpectedVersion loading"

if (-not $ascii.Contains($expectedMarker)) {
    throw "Release DLL validation failed. Expected marker '$expectedMarker' was not found in: $DllPath"
}

if ($ExpectedVersion.EndsWith("-strpm") -and $ascii.Contains("Trade UDP started")) {
    throw "Release DLL validation failed. STRPM build unexpectedly contains the active UDP transport marker."
}

Write-Host "[TradeTogether] DLL validation OK: $ExpectedVersion" -ForegroundColor Green
