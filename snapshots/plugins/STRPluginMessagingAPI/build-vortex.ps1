param(
    [string]$Configuration = "Release",
    [string]$Version = "0.9.3",
    [switch]$IncludeDiagnostic
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = [System.IO.Path]::GetFullPath((Join-Path $Root "..\.build\STRPluginMessagingAPI"))
$PackageSuffix = if ($IncludeDiagnostic) { "-test" } else { "" }
$Stage = [System.IO.Path]::GetFullPath((Join-Path $Root "..\.package\STRPluginMessagingAPI-v$Version$PackageSuffix"))
$Dist = [System.IO.Path]::GetFullPath((Join-Path $Root "dist"))
$Zip = [System.IO.Path]::GetFullPath((Join-Path $Dist "STRPluginMessagingAPI-v$Version$PackageSuffix.zip"))
$Package = Join-Path $Root "package"
$RelaySource = Join-Path $Root "extras\str-server-resources\strpm-chat-relay"
$Ninja = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$VsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"

$AllowedRoot = [System.IO.Path]::GetFullPath((Join-Path $Root "..")).TrimEnd('\')
foreach ($Path in @($Build, $Stage, $Zip)) {
    $FullPath = [System.IO.Path]::GetFullPath($Path)
    if (-not (
            $FullPath.Equals($AllowedRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
            $FullPath.StartsWith("$AllowedRoot\", [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Chemin de sortie hors workspace: $FullPath"
    }
}

if (-not (Test-Path -LiteralPath $VsDevCmd -PathType Leaf)) {
    throw "VsDevCmd introuvable: $VsDevCmd"
}
if (-not (Test-Path -LiteralPath $Ninja -PathType Leaf)) {
    throw "Ninja introuvable: $Ninja"
}
if (-not (Test-Path -LiteralPath (Join-Path $RelaySource "main.lua") -PathType Leaf)) {
    throw "Relay STRPM introuvable: $RelaySource"
}
if (-not (Test-Path -LiteralPath (Join-Path $RelaySource "strpm-chat-relay.manifest") -PathType Leaf)) {
    throw "Manifest du relay STRPM introuvable: $RelaySource"
}

$Command = "`"$VsDevCmd`" -arch=x64 && cmake -S `"$Root`" -B `"$Build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$Ninja`" -DCMAKE_BUILD_TYPE=$Configuration -DSTRPM_ENABLE_UDP_BACKEND=OFF -DSTRPM_BUILD_STR180_BRIDGE_PROBE=ON && cmake --build `"$Build`" --config $Configuration"
cmd.exe /d /s /c $Command
if ($LASTEXITCODE -ne 0) {
    throw "Compilation CMake echouee avec le code $LASTEXITCODE."
}

$Dll = Join-Path $Build "STRPluginMessagingAPI.dll"
$BridgeDll = Join-Path $Build "STRPluginMessagingBridge.dll"
$DiagnosticDll = Join-Path $Build "STRPluginMessagingDiagnostic.dll"
$RequiredDlls = @($Dll, $BridgeDll)
if ($IncludeDiagnostic) {
    $RequiredDlls += $DiagnosticDll
}
foreach ($RequiredDll in $RequiredDlls) {
    if (-not (Test-Path -LiteralPath $RequiredDll -PathType Leaf)) {
        throw "DLL introuvable apres compilation: $RequiredDll"
    }
}

foreach ($Path in @($Stage, $Zip)) {
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path $Dist | Out-Null

Copy-Item -LiteralPath $Package -Destination $Stage -Recurse -Force

# Client payload used by the Client + Server and Client Only FOMOD choices.
$PluginDir = Join-Path $Stage "Data\SKSE\Plugins"
New-Item -ItemType Directory -Force -Path $PluginDir | Out-Null
Copy-Item -LiteralPath $Dll -Destination (Join-Path $PluginDir "STRPluginMessagingAPI.dll") -Force
Copy-Item -LiteralPath $BridgeDll -Destination (Join-Path $PluginDir "STRPluginMessagingBridge.dll") -Force

$PackagedDiagnostic = Join-Path $PluginDir "STRPluginMessagingDiagnostic.dll"
if ($IncludeDiagnostic) {
    Copy-Item -LiteralPath $DiagnosticDll -Destination $PackagedDiagnostic -Force
} elseif (Test-Path -LiteralPath $PackagedDiagnostic) {
    Remove-Item -LiteralPath $PackagedDiagnostic -Force
}

# Server payload is deliberately staged outside Data. The FOMOD maps this
# source folder into Data\SkyrimTogetherReborn, so "Client Only" does not
# accidentally deploy server files. STR 1.8.0 loads resources from its
# working-directory "resources" folder.
$ServerResourcesRoot = Join-Path $Stage "Server\SkyrimTogetherReborn\resources"
New-Item -ItemType Directory -Force -Path $ServerResourcesRoot | Out-Null
Copy-Item -LiteralPath $RelaySource -Destination $ServerResourcesRoot -Recurse -Force

# Fail the build if the FOMOD itself is malformed before producing an archive.
$FomodInfo = Join-Path $Stage "fomod\info.xml"
$FomodConfig = Join-Path $Stage "fomod\ModuleConfig.xml"
foreach ($XmlFile in @($FomodInfo, $FomodConfig)) {
    if (-not (Test-Path -LiteralPath $XmlFile -PathType Leaf)) {
        throw "Fichier FOMOD absent: $XmlFile"
    }
    try {
        [xml](Get-Content -LiteralPath $XmlFile -Raw) | Out-Null
    } catch {
        throw "XML FOMOD invalide: $XmlFile`n$($_.Exception.Message)"
    }
}

Compress-Archive `
    -Path (Join-Path $Stage "*") `
    -DestinationPath $Zip `
    -CompressionLevel Optimal `
    -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
try {
    $Entries = @($Archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    foreach ($RequiredEntry in @(
        "fomod/info.xml",
        "fomod/ModuleConfig.xml",
        "Data/SKSE/Plugins/STRPluginMessagingAPI.dll",
        "Data/SKSE/Plugins/STRPluginMessagingAPI.ini",
        "Data/SKSE/Plugins/STRPluginMessagingBridge.dll",
        "Server/SkyrimTogetherReborn/resources/strpm-chat-relay/main.lua",
        "Server/SkyrimTogetherReborn/resources/strpm-chat-relay/strpm-chat-relay.manifest"
    )) {
        if ($Entries -notcontains $RequiredEntry) {
            throw "Entree absente de l'archive: $RequiredEntry"
        }
    }

    $DiagnosticEntry = "Data/SKSE/Plugins/STRPluginMessagingDiagnostic.dll"
    if ($IncludeDiagnostic -and $Entries -notcontains $DiagnosticEntry) {
        throw "Le client diagnostic est absent du package de test v$Version."
    }
    if (-not $IncludeDiagnostic -and $Entries -contains $DiagnosticEntry) {
        throw "Le client diagnostic ne doit pas etre inclus dans le package normal v$Version."
    }
} finally {
    $Archive.Dispose()
}

Write-Host ""
Write-Host "OK - package FOMOD cree :" -ForegroundColor Green
Write-Host $Zip
Write-Host "FOMOD: Client + Server / Client Only / Server Files Only"
Write-Host "Server destination: Data\SkyrimTogetherReborn\resources\strpm-chat-relay"