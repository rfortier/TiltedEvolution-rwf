@echo off
setlocal EnableExtensions EnableDelayedExpansion

pushd "%~dp0"

for /f "tokens=2" %%V in ('findstr /R /C:"^[ ]*VERSION [0-9][0-9.]*" CMakeLists.txt') do set "VERSION=%%V"
if not defined VERSION (
    echo [ERROR] Could not read VERSION from CMakeLists.txt.
    popd
    exit /b 1
)

set "BUILD_DIR=build\release"
set "STAGE_DIR=build\package"
set "DIST_DIR=dist"
set "ZIP_NAME=DAVSyncTogether-%VERSION%.zip"

if not defined VCPKG_ROOT (
    echo [ERROR] VCPKG_ROOT is not defined.
    popd
    exit /b 1
)

if exist "%BUILD_DIR%" rmdir /S /Q "%BUILD_DIR%"
if exist "%STAGE_DIR%" rmdir /S /Q "%STAGE_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

cmake -S . -B "%BUILD_DIR%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 goto :error

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 goto :error

mkdir "%STAGE_DIR%\SKSE\Plugins" >nul 2>&1

set "DLL_PATH="
for /R "%BUILD_DIR%" %%F in (DAVSyncTogether.dll) do (
    if exist "%%F" set "DLL_PATH=%%F"
)

if not defined DLL_PATH (
    echo [ERROR] DAVSyncTogether.dll was not found after build.
    goto :error
)

echo [INFO] DLL found: !DLL_PATH!
copy /Y "!DLL_PATH!" "%STAGE_DIR%\SKSE\Plugins\DAVSyncTogether.dll" >nul
if errorlevel 1 goto :error

if not exist "%STAGE_DIR%\SKSE\Plugins\DAVSyncTogether.dll" (
    echo [ERROR] DLL was not copied into the staging directory.
    goto :error
)

for %%F in ("%STAGE_DIR%\SKSE\Plugins\DAVSyncTogether.dll") do (
    if %%~zF LEQ 0 (
        echo [ERROR] Staged DLL is empty.
        goto :error
    )
    echo [INFO] Staged DLL size: %%~zF bytes
)

if exist "%DIST_DIR%\%ZIP_NAME%" del /Q "%DIST_DIR%\%ZIP_NAME%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference = 'Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; $src = [System.IO.Path]::GetFullPath('%STAGE_DIR%'); $zip = [System.IO.Path]::GetFullPath('%DIST_DIR%\%ZIP_NAME%'); [System.IO.Compression.ZipFile]::CreateFromDirectory($src, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false); $archive = [System.IO.Compression.ZipFile]::OpenRead($zip); try { if ($archive.Entries.Count -eq 0) { throw 'Generated archive is empty.' }; Write-Host ('[INFO] Archive entries: ' + $archive.Entries.Count); foreach ($entry in $archive.Entries) { Write-Host ('       ' + $entry.FullName + ' (' + $entry.Length + ' bytes)') } } finally { $archive.Dispose() }"
if errorlevel 1 goto :error

if not exist "%DIST_DIR%\%ZIP_NAME%" (
    echo [ERROR] Release archive was not created.
    goto :error
)

for %%F in ("%DIST_DIR%\%ZIP_NAME%") do echo [INFO] ZIP size: %%~zF bytes

echo.
echo ============================================================
echo  DAVSync Together v%VERSION% - Release package ready
echo  %DIST_DIR%\%ZIP_NAME%
echo ============================================================

popd
endlocal
exit /b 0

:error
echo.
echo [ERROR] DAVSync Together release build failed.
popd
endlocal
exit /b 1
