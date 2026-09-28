@echo off
REM Build the legacy (1.5.x) client runtime DLL: SkyrimTogetherRuntime_1_5.dll
REM One-shot pipeline: configure (release/windows/x64) + build the legacy target.
REM Usage: build_legacy_client.bat
setlocal
cd /d "%~dp0"

where xmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] xmake not found in PATH
    exit /b 1
)

echo [1/2] xmake f -p windows -m release
call xmake f -p windows -m release -y
if errorlevel 1 goto :fail

echo [2/2] xmake build SkyrimTogetherClientDllLegacy
call xmake build SkyrimTogetherClientDllLegacy
if errorlevel 1 goto :fail

echo.
echo [OK] Output: build\windows\x64\release\SkyrimTogetherRuntime_1_5.dll
echo      Deploy: copy it into Data\SkyrimTogetherRuntime\ and bump .str_version
exit /b 0

:fail
echo [ERROR] build failed
exit /b 1
