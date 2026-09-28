param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
$Package = Join-Path $Root "package"
$Dist = Join-Path $Root "dist"
$FomodSource = Join-Path $Root "fomod"
$Plugins = Join-Path $Package "Data\SKSE\Plugins"
$Stage = [System.IO.Path]::GetFullPath((Join-Path $Build "fomod-stage"))

$ProviderPackages = @(
    @{ Stage = "10 Pubes Forever Female"; Root = "optional\PubesForeverFemale\package"; Marker = "PubesForeverFemale.enabled" },
    @{ Stage = "20 Pubes Forever Male"; Root = "optional\PubesForeverMale\package"; Marker = "PubesForeverMale.enabled" },
    @{ Stage = "30 Natural Pubic Hairstyles"; Root = "optional\NaturalPubicHairstyles\package"; Marker = "NaturalPubicHairstyles.enabled" },
    @{ Stage = "40 Natural Pubic Hairstyles UBE"; Root = "optional\NaturalPubicHairstylesUBE\package"; Marker = "NaturalPubicHairstylesUBE.enabled" },
    @{ Stage = "50 More Pubes for SlaveTats"; Root = "optional\MorePubesSlaveTats\package"; Marker = "MorePubesSlaveTats.enabled" },
    @{ Stage = "60 Nordic Warmaiden"; Root = "optional\NordicWarmaiden\package"; Marker = "NordicWarmaiden.enabled" },
    @{ Stage = "70 HIMBO Bodyhair"; Root = "optional\HIMBOBodyhair\package"; Marker = "HIMBOBodyhair.enabled" },
    @{ Stage = "80 OPubes NG"; Root = "optional\OPubes\package"; Marker = "OPubes.enabled" },
    @{ Stage = "90 The New Gentleman"; Root = "optional\TNG\package"; Marker = "TNG.enabled" }
)

if (-not $VcpkgRoot) { throw "VCPKG_ROOT n'est pas defini." }
$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) { throw "Toolchain vcpkg introuvable: $Toolchain" }

$ConfigureArguments = @("-S", $Root, "-B", $Build, "-DCMAKE_TOOLCHAIN_FILE=$Toolchain")
$Cache = Join-Path $Build "CMakeCache.txt"
if (Test-Path $Cache) {
    $CachedHome = Select-String -Path $Cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($CachedHome -and -not [string]::Equals([System.IO.Path]::GetFullPath($CachedHome.Matches[0].Groups[1].Value), [System.IO.Path]::GetFullPath($Root), [System.StringComparison]::OrdinalIgnoreCase)) {
        $ConfigureArguments = @("--fresh") + $ConfigureArguments
    }
}

& cmake @ConfigureArguments
if ($LASTEXITCODE -ne 0) { throw "La configuration CMake a echoue (code $LASTEXITCODE)." }

$GeneratedPlugin = Join-Path $Build "__MorphSyncTogetherPlugin.cpp"
if (Test-Path $GeneratedPlugin) {
    $content = Get-Content $GeneratedPlugin -Raw
    if ($content -notmatch "using namespace std::literals") {
        $content = "using namespace std::literals;`r`n" + $content
        Set-Content -Path $GeneratedPlugin -Value $content -Encoding UTF8
    }
}

& cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "La compilation CMake a echoue (code $LASTEXITCODE)." }

$dll = Get-ChildItem -Path $Build -Recurse -Filter "MorphSyncTogether.dll" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $dll) { throw "MorphSyncTogether.dll n'a pas ete trouve apres compilation." }

New-Item -ItemType Directory -Force -Path $Plugins | Out-Null
Copy-Item $dll.FullName (Join-Path $Plugins "MorphSyncTogether.dll") -Force

$CoreIni = Join-Path $Plugins "MorphSyncTogether.ini"
$RequiredPaths = @(
    $CoreIni,
    (Join-Path $FomodSource "ModuleConfig.xml"),
    (Join-Path $FomodSource "info.xml")
)
foreach ($Provider in $ProviderPackages) {
    $ProviderRoot = Join-Path $Root $Provider.Root
    $RequiredPaths += Join-Path $ProviderRoot ("Data\SKSE\Plugins\MorphSyncTogether\Providers\" + $Provider.Marker)
}
foreach ($RequiredPath in $RequiredPaths) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) { throw "Fichier FOMOD requis introuvable: $RequiredPath" }
}

try {
    [void][xml](Get-Content -LiteralPath (Join-Path $FomodSource "ModuleConfig.xml") -Raw)
    [void][xml](Get-Content -LiteralPath (Join-Path $FomodSource "info.xml") -Raw)
} catch { throw "Metadonnees FOMOD XML invalides: $($_.Exception.Message)" }

if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
$CoreStage = Join-Path $Stage "00 Core"
$FomodStage = Join-Path $Stage "fomod"
New-Item -ItemType Directory -Force -Path $CoreStage,$FomodStage | Out-Null
Copy-Item (Join-Path $Package "*") $CoreStage -Recurse -Force

foreach ($Provider in $ProviderPackages) {
    $ProviderRoot = Join-Path $Root $Provider.Root
    $ProviderStage = Join-Path $Stage $Provider.Stage
    New-Item -ItemType Directory -Force -Path $ProviderStage | Out-Null
    Copy-Item (Join-Path $ProviderRoot "*") $ProviderStage -Recurse -Force
}
Copy-Item (Join-Path $FomodSource "*") $FomodStage -Recurse -Force

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$zip = Join-Path $Dist "MorphSyncTogether-v0.4.2.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $zip -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $Entries = @($Archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    $RequiredEntries = @(
        "00 Core/Data/SKSE/Plugins/MorphSyncTogether.dll",
        "00 Core/Data/SKSE/Plugins/MorphSyncTogether.ini",
        "fomod/ModuleConfig.xml",
        "fomod/info.xml"
    )
    foreach ($Provider in $ProviderPackages) {
        $RequiredEntries += ($Provider.Stage + "/Data/SKSE/Plugins/MorphSyncTogether/Providers/" + $Provider.Marker)
    }
    foreach ($RequiredEntry in $RequiredEntries) {
        if ($Entries -notcontains $RequiredEntry) { throw "Entree FOMOD absente de l'archive: $RequiredEntry" }
    }
    if ($Entries -contains "fomod/ModuleImage.png") { throw "Le visuel FOMOD ne doit plus etre inclus." }
} finally {
    $Archive.Dispose()
}

Write-Host ""
Write-Host "OK - package cree et verifie :" -ForegroundColor Green
Write-Host $zip
