param(
    [string]$SkyrimDir = "C:\Games\Steam\steamapps\common\Skyrim Special Edition",
    [string]$ExtraSourceDir = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceFile = Join-Path $Root "Data\Scripts\Source\OStimTogetherOCum.psc"
$StubDir = Join-Path $Root "Dependencies\Source"
$PackageScripts = Join-Path $Root "package\Data\Scripts"

$Compiler = Join-Path $SkyrimDir "Papyrus Compiler\PapyrusCompiler.exe"
$GameSources = Join-Path $SkyrimDir "Data\Source\Scripts"
$SKSESources = Join-Path $SkyrimDir "Data\Scripts\Source"
$Flags = Join-Path $GameSources "TESV_Papyrus_Flags.flg"

if (-not (Test-Path $Compiler)) {
    throw "PapyrusCompiler.exe introuvable: $Compiler"
}
if (-not (Test-Path $SourceFile)) {
    throw "Source introuvable: $SourceFile"
}
if (-not (Test-Path $Flags)) {
    throw "TESV_Papyrus_Flags.flg introuvable: $Flags"
}
if (-not (Test-Path (Join-Path $SKSESources "SKSE.psc"))) {
    throw "Sources Papyrus SKSE introuvables: $SKSESources"
}

New-Item -ItemType Directory -Force -Path $PackageScripts | Out-Null

$Includes = @(
    (Join-Path $Root "Data\Scripts\Source"),
    $StubDir,
    $SKSESources,
    $GameSources
)

if ($ExtraSourceDir) {
    $Includes += $ExtraSourceDir
}

$IncludeArg = $Includes -join ";"

& $Compiler `
    $SourceFile `
    "-f=$Flags" `
    "-i=$IncludeArg" `
    "-o=$PackageScripts"

if ($LASTEXITCODE -ne 0) {
    throw "Compilation Papyrus echouee avec le code $LASTEXITCODE"
}

$Pex = Join-Path $PackageScripts "OStimTogetherOCum.pex"
if (-not (Test-Path $Pex)) {
    throw "Le compilateur n'a pas produit $Pex"
}

Write-Host "OK - PEX cree: $Pex" -ForegroundColor Green
Write-Host "Ne compile/installe jamais Dependencies\Source\OActor.psc : c'est uniquement un stub de compilation."
