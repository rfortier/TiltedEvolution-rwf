param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
$Package = Join-Path $Root "package"
$Plugins = Join-Path $Package "Data\SKSE\Plugins"
$Scripts = Join-Path $Package "Data\Scripts"
$UIConsentPackage = Join-Path $Root "compat\OStimUIConsent\package"
$Dist = Join-Path $Root "dist"
$VersionFile = Join-Path $Root "VERSION"

if (-not (Test-Path $VersionFile)) {
    throw "VERSION introuvable: $VersionFile"
}

$Version = (Get-Content $VersionFile -Raw).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION invalide: '$Version'"
}

if (-not $VcpkgRoot) {
    throw "VCPKG_ROOT n'est pas defini."
}

$UIConsentOSKSE = Join-Path $UIConsentPackage "Data\Scripts\OSKSE.pex"
$UIConsentNative = Join-Path $UIConsentPackage "Data\Scripts\OStimTogetherNative.pex"
if (-not (Test-Path $UIConsentOSKSE) -or -not (Test-Path $UIConsentNative)) {
    throw "Patch Papyrus Add Actor obligatoire non compile.`nExecute compat\OStimUIConsent\compile-ui-consent.ps1 puis relance le build."
}

$Toolchain =
    Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $Toolchain)) {
    throw "Toolchain vcpkg introuvable: $Toolchain"
}

cmake -S $Root -B $Build `
    -DCMAKE_TOOLCHAIN_FILE="$Toolchain"

if ($LASTEXITCODE -ne 0) {
    throw "Configuration CMake echouee avec le code $LASTEXITCODE"
}

$GeneratedPlugin =
    Join-Path $Build "__OStimTogetherPlugin.cpp"

if (Test-Path $GeneratedPlugin) {
    $content =
        Get-Content $GeneratedPlugin -Raw

    if ($content -notmatch "using namespace std::literals") {
        $content =
            "using namespace std::literals;`r`n" + $content

        Set-Content `
            -Path $GeneratedPlugin `
            -Value $content `
            -Encoding UTF8

        Write-Host `
            "CommonLib generated-file workaround applied."
    }
}

cmake --build $Build --config $Configuration

if ($LASTEXITCODE -ne 0) {
    throw "Compilation CMake echouee avec le code $LASTEXITCODE"
}

$dll =
    Get-ChildItem `
        -Path $Build `
        -Recurse `
        -Filter "OStimTogether.dll" |
    Select-Object -First 1

if (-not $dll) {
    throw "OStimTogether.dll n'a pas ete trouve apres compilation."
}

New-Item `
    -ItemType Directory `
    -Force `
    -Path $Plugins | Out-Null

Copy-Item `
    $dll.FullName `
    (Join-Path $Plugins "OStimTogether.dll") `
    -Force

# Add Actor consent is core functionality as of 0.26.1. Always place both
# compiled Papyrus files in the normal package before creating any Core archive.
New-Item -ItemType Directory -Force -Path $Scripts | Out-Null
Copy-Item $UIConsentOSKSE (Join-Path $Scripts "OSKSE.pex") -Force
Copy-Item $UIConsentNative (Join-Path $Scripts "OStimTogetherNative.pex") -Force

New-Item `
    -ItemType Directory `
    -Force `
    -Path $Dist | Out-Null

$zip =
    Join-Path $Dist "OStimTogether-v$Version-Core-Vortex.zip"

if (Test-Path $zip) {
    Remove-Item $zip -Force
}

Compress-Archive `
    -Path (Join-Path $Package "*") `
    -DestinationPath $zip `
    -Force

Write-Host ""
Write-Host `
    "OK - OStim Together v$Version package Vortex cree :" `
    -ForegroundColor Green

Write-Host $zip
Write-Host "Add Actor consent gate inclus obligatoirement dans le Core." -ForegroundColor Green
