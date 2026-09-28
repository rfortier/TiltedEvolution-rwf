param(
    [string]$SkyrimDir = "C:\Games\Steam\steamapps\common\Skyrim Special Edition",
    [string]$OStimSourceDir = ""
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = Join-Path $Root "Data\Scripts\Source"
$StubDir = Join-Path $Root "Dependencies\Source"
$PackageScripts = Join-Path $Root "package\Data\Scripts"
$Compiler = Join-Path $SkyrimDir "Papyrus Compiler\PapyrusCompiler.exe"
$GameSources = Join-Path $SkyrimDir "Data\Source\Scripts"
$SKSESources = Join-Path $SkyrimDir "Data\Scripts\Source"
$Flags = Join-Path $GameSources "TESV_Papyrus_Flags.flg"

if (-not (Test-Path $Compiler)) { throw "PapyrusCompiler.exe introuvable: $Compiler" }
if (-not (Test-Path $Flags)) { throw "TESV_Papyrus_Flags.flg introuvable: $Flags" }
if (-not (Test-Path (Join-Path $SourceDir "OSKSE.psc"))) { throw "OSKSE.psc patch introuvable" }
if (-not (Test-Path (Join-Path $SourceDir "OStimTogetherNative.psc"))) { throw "OStimTogetherNative.psc introuvable" }

$RequiredStubs = @(
    "UIExtensions.psc",
    "UIListMenu.psc",
    "UIMenuBase.psc",
    "UITextEntryMenu.psc",
    "OUtils.psc",
    "OActorUtil.psc",
    "OSexIntegrationMain.psc"
)
foreach ($Stub in $RequiredStubs) {
    if (-not (Test-Path (Join-Path $StubDir $Stub))) {
        throw "Stub de compilation introuvable: $Stub"
    }
}

if (-not $OStimSourceDir) {
    $DefaultOStimSource = Join-Path $SkyrimDir "Data\Scripts\Source"
    if (Test-Path (Join-Path $DefaultOStimSource "OSKSE.psc")) {
        $OStimSourceDir = $DefaultOStimSource
    }
}

if (-not $OStimSourceDir -or -not (Test-Path $OStimSourceDir)) {
    throw "Sources Papyrus OStim introuvables. Passe -OStimSourceDir vers le dossier contenant les sources OStim."
}

# The compile-only stubs MUST precede the real OStim source directory. They
# intentionally shadow OUtils/OActorUtil/OSexIntegrationMain so compiling the
# small OSKSE patch cannot recursively pull OStim's bars and SkyUI widget tree.
# Only OSKSE.pex and OStimTogetherNative.pex are emitted/packaged.
$Includes = @(
    $SourceDir,
    $StubDir,
    $OStimSourceDir,
    $SKSESources,
    $GameSources
)
$Includes = $Includes | Select-Object -Unique
$IncludeArg = $Includes -join ";"

New-Item -ItemType Directory -Force -Path $PackageScripts | Out-Null

foreach ($Source in @("OStimTogetherNative.psc", "OSKSE.psc")) {
    $SourceFile = Join-Path $SourceDir $Source
    & $Compiler $SourceFile "-f=$Flags" "-i=$IncludeArg" "-o=$PackageScripts"
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation Papyrus echouee pour $Source (code $LASTEXITCODE)."
    }
}

foreach ($Pex in @("OStimTogetherNative.pex", "OSKSE.pex")) {
    $Path = Join-Path $PackageScripts $Pex
    if (-not (Test-Path $Path)) { throw "PEX non produit: $Path" }
}

Write-Host "OK - OStim UI consent patch compile dans $PackageScripts" -ForegroundColor Green
Write-Host "UIExtensions/OStim: stubs de compilation integres; les scripts reels ne sont pas recompiles." -ForegroundColor Green
Write-Host "Le fichier OSKSE.pex doit gagner le conflit face a OStim pour suspendre Add Actor avant consentement." -ForegroundColor Yellow
